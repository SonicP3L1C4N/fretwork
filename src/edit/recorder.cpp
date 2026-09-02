// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "recorder.h"

#include "notevalue.h"
#include "timeline.h"

#include <algorithm>
#include <cmath>

Recorder::Recorder(const Score &score, const QList<int> &order, const Options &options)
    : m_score(score)
    , m_order(order)
    , m_options(options)
{
    if (!(Rational(0) < m_options.grid)) {
        m_options.grid = Rational(1, 4);
    }
}

Rational Recorder::snapped(double quarters) const
{
    const double cells = std::max(0.0, quarters) / m_options.grid.toDouble();
    return m_options.grid * Rational(qint64(std::llround(cells)));
}

bool Recorder::enter(const Rational &at)
{
    const int pass = Timeline::passAt(m_score, m_order, at);
    if (pass < 0) {
        return false;
    }
    if (pass != m_pass) {
        leave();
        m_pass = pass;
        m_bar = m_order.at(pass);
        m_barStart = Timeline::quartersAtPass(m_score, m_order, pass);
        m_barLength = m_score.masterBars.at(m_bar).length();
    }
    return true;
}

Recorder::Take Recorder::noteOn(int pitch, double quarters)
{
    const Rational at = snapped(quarters);
    if (!enter(at)) {
        return {};
    }
    Played played;
    played.onset = at - m_barStart;
    played.struck = quarters - m_barStart.toDouble();
    played.pitch = pitch;
    m_played.append(played);
    return rebuild();
}

Recorder::Take Recorder::noteOff(int pitch, double quarters)
{
    if (m_pass < 0) {
        return {};
    }
    // The most recent one, because a key struck twice and released once is
    // the second strike being released.
    for (int index = m_played.size() - 1; index >= 0; --index) {
        Played &played = m_played[index];
        if (played.pitch == pitch && played.off < 0) {
            played.off = std::max(quarters - m_barStart.toDouble(), played.onset.toDouble());
            return rebuild();
        }
    }
    return {};
}

void Recorder::leave()
{
    if (m_pass >= 0) {
        m_handAtStart = m_hand;
    }
    m_pass = -1;
    m_bar = -1;
    m_played.clear();
    m_reported.clear();
}

int Recorder::pass() const
{
    return m_pass;
}

QList<int> Recorder::unplayable()
{
    const QList<int> pitches = m_unplayable;
    m_unplayable.clear();
    return pitches;
}

QList<Rational> Recorder::written(const Rational &duration)
{
    // Every value the page can draw, dotted and plain, longest first. A
    // dotted value is tried at its own place in the order rather than after
    // every plain one, because a dotted quaver is one note and a quaver tied
    // to a semiquaver is two.
    static const QList<Rational> values = [] {
        QList<Rational> all;
        for (Rational value = NoteValue::Longest; !(value < NoteValue::Shortest);
             value = value / Rational(2)) {
            all.append(value * Rational(3, 2));
            all.append(value);
        }
        std::sort(all.begin(), all.end(), [](const Rational &a, const Rational &b) {
            return b < a;
        });
        return all;
    }();

    QList<Rational> parts;
    Rational left = duration;
    while (Rational(0) < left) {
        bool found = false;
        for (const Rational &value : values) {
            if (!(left < value)) {
                parts.append(value);
                left = left - value;
                found = true;
                break;
            }
        }
        if (!found) {
            // Shorter than anything the page can draw. Kept rather than lost,
            // so the bar still adds up; the layout draws it as best it can.
            parts.append(left);
            break;
        }
    }
    return parts;
}

Recorder::Take Recorder::rebuild()
{
    Take take;
    take.pass = m_pass;
    take.bar = m_bar;

    // The keys grouped by the line they landed on, in order. Sorted here
    // rather than kept sorted, because a release arrives out of order with
    // the strikes and the list is a bar's worth at the outside.
    struct Group {
        Rational onset;
        double struck = 0;      //< the earliest key, as played
        QList<int> pitches;
        bool held = false;
        double off = 0;         //< the latest release, as played
    };
    QList<Played> played = m_played;
    std::stable_sort(played.begin(), played.end(), [](const Played &a, const Played &b) {
        return a.onset < b.onset;
    });
    QList<Group> groups;
    for (const Played &key : played) {
        if (groups.isEmpty() || !(groups.last().onset == key.onset)) {
            groups.append(Group{key.onset, key.struck, {}, false, 0});
        }
        Group &group = groups.last();
        group.struck = std::min(group.struck, key.struck);
        if (!group.pitches.contains(key.pitch)) {
            group.pitches.append(key.pitch);
        }
        if (key.off < 0) {
            group.held = true;
        } else {
            group.off = std::max(group.off, key.off);
        }
    }

    const Rational grid = m_options.grid;
    const auto rests = [&take](const Rational &length) {
        for (const Rational &value : written(length)) {
            take.beats.append(Beat{value, {}, false});
        }
    };

    m_hand = m_handAtStart;
    Rational at;
    for (int index = 0; index < groups.size(); ++index) {
        const Group &group = groups.at(index);
        if (at < group.onset) {
            rests(group.onset - at);
            at = group.onset;
        }
        const Rational next = index + 1 < groups.size() ? groups.at(index + 1).onset : m_barLength;

        // How long it sounds: to the next onset, unless every key came up at
        // least a cell before it, in which case to the release -- rounded,
        // and never shorter than a cell, because a note shorter than the grid
        // is a note the grid cannot say. The gap is measured between the
        // release and the next key *as played*, not the line the next key
        // rounded to: a player who is consistently a little early would
        // otherwise be given a rest before every note, for playing early.
        const double nextStruck =
            index + 1 < groups.size() ? groups.at(index + 1).struck : m_barLength.toDouble();
        Rational end = next;
        const bool released = !group.held && nextStruck - group.off >= grid.toDouble() - 1e-9;
        if (released) {
            end = snapped(group.off);
            if (end < group.onset + grid) {
                end = group.onset + grid;
            }
            if (next < end) {
                end = next;
            }
        }

        // Which strings. One key asks the solver for a position; more ask it
        // for a shape, because three notes on six strings is a question about
        // the hand and not three questions about pitches.
        QList<int> pitches = group.pitches;
        std::sort(pitches.begin(), pitches.end());
        QList<Fretboard::Position> shape;
        if (pitches.size() == 1) {
            const Fretboard::Position position =
                Fretboard::choose(m_options.instrument, pitches.first(), m_hand);
            if (position.isValid()) {
                shape.append(position);
            }
        } else {
            shape = Fretboard::chord(m_options.instrument, pitches, m_hand);
        }
        if (shape.size() != pitches.size()) {
            // The shape could not be found whole, so each key on its own,
            // keeping off the strings already taken and leaving out what
            // still cannot be reached.
            shape.clear();
            QList<int> occupied;
            for (const int pitch : pitches) {
                const Fretboard::Position position =
                    Fretboard::choose(m_options.instrument, pitch, m_hand, occupied);
                if (position.isValid()) {
                    shape.append(position);
                    occupied.append(position.string);
                } else if (!m_reported.contains(pitch)) {
                    m_reported.insert(pitch);
                    m_unplayable.append(pitch);
                }
            }
        }
        for (const Fretboard::Position &position : shape) {
            m_hand = m_hand.after(position);
        }

        if (shape.isEmpty()) {
            rests(next - at);
            at = next;
            continue;
        }
        const QList<Rational> values = written(end - group.onset);
        for (int part = 0; part < values.size(); ++part) {
            take.beats.append(Beat{values.at(part), shape, part + 1 < values.size()});
        }
        if (end < next) {
            rests(next - end);
        }
        at = next;
    }
    if (at < m_barLength) {
        rests(m_barLength - at);
    }
    return take;
}
