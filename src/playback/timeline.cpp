// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "timeline.h"

#include <QHash>

#include <algorithm>

namespace
{
/** A runaway repeat structure should stop, not fill memory. */
constexpr int MaximumPlayedBars = 100000;

int velocityFor(Dynamic dynamic)
{
    switch (dynamic) {
    case Dynamic::PPP: return 15;
    case Dynamic::PP:  return 31;
    case Dynamic::P:   return 47;
    case Dynamic::MP:  return 63;
    case Dynamic::MF:  return 79;
    case Dynamic::F:   return 95;
    case Dynamic::FF:  return 111;
    case Dynamic::FFF: return 127;
    }
    return 79;
}

/**
 * Which channel of this track's synth the note belongs on.
 *
 * One per string, so that a bend moves one string and not the chord under it.
 * Percussion has no strings and one channel, which must be 10 in MIDI terms;
 * that mapping belongs to whatever speaks MIDI, not here.
 */
int channelFor(const Track &track, const Note &note)
{
    if (track.isPercussion()) {
        return 0;
    }
    if (note.string < 0 || track.stringCount() <= 0) {
        return 0;
    }
    return std::min(note.string, track.stringCount() - 1);
}
}

QList<int> Timeline::playedOrder(const Score &score, bool expandRepeats)
{
    QList<int> order;
    if (!expandRepeats) {
        order.reserve(int(score.masterBars.size()));
        for (int index = 0; index < score.masterBars.size(); ++index) {
            order.append(index);
        }
        return order;
    }

    QHash<int, int> passes;
    int index = 0;
    int sectionStart = 0;
    while (index < score.masterBars.size() && order.size() < MaximumPlayedBars) {
        const MasterBar &bar = score.masterBars.at(index);
        if (bar.repeatStart) {
            sectionStart = index;
        }
        order.append(index);

        if (bar.repeatEnd) {
            const int played = ++passes[index];
            if (played < std::max(bar.repeatCount, 1)) {
                index = sectionStart;
                continue;
            }
        }
        ++index;
    }
    return order;
}

bool Timeline::hasAlternateEndings(const Score &score)
{
    return std::any_of(score.masterBars.begin(), score.masterBars.end(),
                       [](const MasterBar &bar) { return bar.alternateEndings; });
}

QList<Timeline::NoteEvent> Timeline::notesFor(const Score &score, int trackIndex,
                                              const QList<int> &order)
{
    QList<NoteEvent> events;
    if (trackIndex < 0 || trackIndex >= score.tracks.size()) {
        return events;
    }
    const Track &track = score.tracks.at(trackIndex);

    Rational position;
    for (const int barIndex : order) {
        const MasterBar &master = score.masterBars.at(barIndex);
        if (trackIndex >= master.bars.size()) {
            position += master.length();
            continue;
        }

        const Bar bar = score.bars.value(master.bars.at(trackIndex));
        for (const int voiceId : bar.voices) {
            if (voiceId < 0) {
                continue;
            }
            // Voices run at the same time, each from the start of the bar.
            Rational offset;
            for (const int beatId : score.voices.value(voiceId).beats) {
                const auto beat = score.beats.constFind(beatId);
                if (beat == score.beats.constEnd()) {
                    continue;
                }
                const Rational duration = score.rhythms.value(beat->rhythm, Rational(1));
                const int dynamic = velocityFor(beat->dynamic);

                for (const int noteId : beat->notes) {
                    const auto note = score.notes.constFind(noteId);
                    if (note == score.notes.constEnd() || note->midi < 0) {
                        continue;
                    }
                    const Rational start = position + offset;

                    // A tie does not restrike: it lengthens what is already
                    // ringing. Searching backwards finds the most recent
                    // candidate first, which is the one meant.
                    if (note->tieDestination) {
                        for (auto held = events.rbegin(); held != events.rend(); ++held) {
                            if (held->pitch == note->midi && held->end == start) {
                                held->end += duration;
                                break;
                            }
                        }
                        continue;
                    }

                    NoteEvent event;
                    event.start = start;
                    event.end = start + duration;
                    event.pitch = note->midi;
                    event.string = note->string;
                    event.channel = channelFor(track, *note);

                    int velocity = dynamic;
                    if (note->accent) {
                        velocity = std::min(127, velocity + 16);
                    }
                    if (note->ghost) {
                        velocity = std::max(1, velocity / 2);
                    }
                    if (note->muted) {
                        // A dead note is a click at no particular pitch.
                        event.end = start + std::min(duration, Rational(1, 8));
                        velocity = std::max(1, velocity - 20);
                    } else if (note->palmMuted) {
                        event.end = start + duration * Rational(1, 2);
                    }
                    event.velocity = velocity;

                    events.append(event);
                }
                offset += duration;
            }
        }
        position += master.length();
    }

    std::stable_sort(events.begin(), events.end(),
                     [](const NoteEvent &a, const NoteEvent &b) {
                         return a.start == b.start ? a.pitch < b.pitch : a.start < b.start;
                     });
    return events;
}

QList<Timeline::TempoEvent> Timeline::tempoMap(const Score &score, const QList<int> &order)
{
    if (score.tempos.isEmpty()) {
        return {{Rational(0), 120.0}};
    }

    // Where each notated bar first falls on the played timeline. First pass
    // wins: a tempo written inside a repeated section takes effect the first
    // time through, and stays in force afterwards.
    QHash<int, Rational> starts;
    Rational position;
    for (const int barIndex : order) {
        if (!starts.contains(barIndex)) {
            starts.insert(barIndex, position);
        }
        position += score.masterBars.at(barIndex).length();
    }

    QList<TempoEvent> events;
    for (const TempoChange &change : score.tempos) {
        const auto start = starts.constFind(change.bar);
        if (start == starts.constEnd()) {
            continue;
        }
        // Positions are written in quarters and are rarely finer than a
        // sixty-fourth, which is what this rounds them to.
        events.append({*start + Rational(qint64(change.position * 64), 64),
                       change.quarterBpm});
    }

    std::sort(events.begin(), events.end(), [](const TempoEvent &a, const TempoEvent &b) {
        return a.at < b.at;
    });
    return events.isEmpty() ? QList<TempoEvent>{{Rational(0), 120.0}} : events;
}

Rational Timeline::length(const Score &score, const QList<int> &order)
{
    Rational total;
    for (const int barIndex : order) {
        total += score.masterBars.at(barIndex).length();
    }
    return total;
}

double Timeline::seconds(const Score &score, const QList<int> &order)
{
    const QList<TempoEvent> tempos = tempoMap(score, order);
    const Rational total = length(score, order);

    double elapsed = 0;
    for (int index = 0; index < tempos.size(); ++index) {
        const Rational from = tempos.at(index).at;
        const Rational to = index + 1 < tempos.size() ? tempos.at(index + 1).at : total;
        if (to < from) {
            continue;
        }
        const double quarters = to.toDouble() - from.toDouble();
        elapsed += quarters * 60.0 / std::max(tempos.at(index).quarterBpm, 1.0);
    }
    return elapsed;
}
