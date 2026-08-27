// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "timeline.h"

#include "swing.h"

#include <QHash>
#include <QSet>

#include <algorithm>
#include <cmath>

namespace
{
/** A runaway repeat structure should stop, not fill memory. */
constexpr int MaximumPlayedBars = 100000;

/**
 * How far a pitch bend reaches, in semitones.
 *
 * Two is the General MIDI default and cannot express a whole-tone bend on a
 * held note, let alone a whammy dive, so every channel is told otherwise
 * before it plays anything. Twelve is what Guitar Pro uses.
 */
constexpr int BendRangeSemitones = 12;
constexpr int BendCentre = 8192;

/** A bend is redrawn at least this often, so a slow one is not a staircase. */
const Rational BendStep = Rational(1, 16);
constexpr int MaximumBendSteps = 128;

int bendValueFor(int cents)
{
    const double range = BendRangeSemitones * 100.0;
    const double scaled = std::clamp(cents / range, -1.0, 1.0);
    return BendCentre + int(scaled * (BendCentre - 1));
}

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
 * The bend curve gpif describes, as points along the note.
 *
 * Its offsets are percentages of the note's length and its values are
 * hundredths of a semitone, which is cents by another name. Middle points are
 * optional and marked absent with -1; a bend that starts where it ends is
 * still worth carrying, because a whammy dive can be flat and long.
 */
QList<Timeline::BendPoint> bendCurve(const Note &note, const Rational &duration)
{
    if (!note.bended) {
        return {};
    }
    const auto at = [&duration](int percent) {
        return duration * Rational(std::clamp(percent, 0, 100), 100);
    };

    QList<Timeline::BendPoint> points;
    points.append({at(note.bendOriginOffset), note.bendOriginValue});
    if (note.bendMiddleOffset1 >= 0) {
        points.append({at(note.bendMiddleOffset1), note.bendMiddleValue});
    }
    if (note.bendMiddleOffset2 >= 0) {
        points.append({at(note.bendMiddleOffset2), note.bendMiddleValue});
    }
    points.append({at(note.bendDestinationOffset), note.bendDestinationValue});

    std::stable_sort(points.begin(), points.end(),
                     [](const Timeline::BendPoint &a, const Timeline::BendPoint &b) {
                         return a.at < b.at;
                     });
    return points;
}

/**
 * Let ring: the note keeps sounding until that string is used again.
 *
 * Which is what the instruction means on a guitar -- the string is left to
 * decay rather than damped -- and why it can only be resolved once the whole
 * track is laid out, since the note that stops it may be bars away.
 */
void applyLetRing(QList<Timeline::NoteEvent> &events, const QList<bool> &ringing,
                  const Rational &finish)
{
    for (int index = 0; index < events.size(); ++index) {
        if (!ringing.at(index)) {
            continue;
        }
        Rational until = finish;
        for (int later = index + 1; later < events.size(); ++later) {
            if (events.at(later).string != events.at(index).string) {
                continue;
            }
            if (events.at(index).start < events.at(later).start) {
                until = events.at(later).start;
                break;
            }
        }
        if (events.at(index).end < until) {
            events[index].end = until;
        }
    }
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
    QList<bool> ringing;
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

        // A shuffle is a warp of the bar's own time rather than a change to
        // the notes in it. Every position below is written down first and
        // moved once, here, so that nothing further on has to know which notes
        // were meant to be a swung pair.
        const auto swung = [&](const Rational &within) {
            return position + Swing::played(within, master.tripletFeel, master.length());
        };

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
                    const Rational start = swung(offset);
                    // How long the note lasts once the bar has been warped,
                    // which is what a bend has to be drawn across and what a
                    // palm mute takes half of.
                    const Rational sounding = swung(offset + duration) - start;

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
                    event.end = start + sounding;
                    event.pitch = note->midi;
                    event.string = note->string;
                    event.channel = channelFor(track, *note);
                    event.bend = bendCurve(*note, sounding);
                    // A legato slide is fretted rather than picked, the same as
                    // the hammer-ons and pull-offs gpif calls Hopo.
                    event.legato = note->hammerDestination
                        || note->slide == SlideType::Legato;

                    int velocity = dynamic;
                    if (event.legato) {
                        // Not silent: a hammer-on is quieter than a picked
                        // note, not absent. Sounding it at full strength is
                        // the more audible of the two mistakes available.
                        velocity = velocity * 2 / 3;
                    }
                    if (note->accent) {
                        velocity = std::min(127, velocity + 16);
                    }
                    if (note->ghost) {
                        velocity = std::max(1, velocity / 2);
                    }
                    if (note->muted) {
                        // A dead note is a click at no particular pitch.
                        event.end = start + std::min(sounding, Rational(1, 8));
                        velocity = std::max(1, velocity - 20);
                    } else if (note->palmMuted) {
                        event.end = start + sounding * Rational(1, 2);
                    }
                    event.velocity = velocity;

                    events.append(event);
                    ringing.append(note->letRing);
                }
                offset += duration;
            }
        }
        position += master.length();
    }

    // Sorted before let ring is resolved, so "the next note on this string"
    // means the next one in time rather than the next one written down.
    QList<int> byTime;
    byTime.reserve(int(events.size()));
    for (int index = 0; index < events.size(); ++index) {
        byTime.append(index);
    }
    std::stable_sort(byTime.begin(), byTime.end(), [&events](int a, int b) {
        const NoteEvent &first = events.at(a);
        const NoteEvent &second = events.at(b);
        return first.start == second.start ? first.pitch < second.pitch
                                           : first.start < second.start;
    });

    QList<NoteEvent> sorted;
    QList<bool> sortedRinging;
    sorted.reserve(int(byTime.size()));
    sortedRinging.reserve(int(byTime.size()));
    for (const int index : std::as_const(byTime)) {
        sorted.append(events.at(index));
        sortedRinging.append(ringing.at(index));
    }

    applyLetRing(sorted, sortedRinging, position);
    return sorted;
}

namespace
{
/**
 * One sound at two levels, which is what a metronome is.
 *
 * General MIDI 75, the claves: a bright, short crack that carries over a band
 * where a wood block does not. Measured against a rendered mix, a plain click
 * on claves sits about six decibels under the music and one on a wood block
 * disappeared into it, which is the difference between a metronome and a
 * decoration.
 *
 * The same pitch for both beats and not a high and a low block, because an
 * accent is emphasis and not a different instrument -- a hardware metronome
 * leans on the first beat, it does not change what it is hitting. The lean is
 * about three decibels, which reads as a downbeat without turning the other
 * three into afterthoughts.
 */
constexpr int ClickPitch = 75;
constexpr int AccentVelocity = 127;
constexpr int PlainVelocity = 105;

/** Short: a wood block has decayed long before this matters, and it is tidy. */
const Rational ClickLength(1, 8);
}

Rational Timeline::beatOf(const MasterBar &bar)
{
    const Rational written(4, std::max(1, bar.denominator));
    // Compound time is counted in dotted beats: 6/8 is two, not six. Not 3/8,
    // which has a numerator equal to three rather than a multiple of it, and
    // which everybody counts in quavers.
    if (bar.denominator >= 8 && bar.numerator > 3 && bar.numerator % 3 == 0) {
        return written * Rational(3);
    }
    return written;
}

Track Timeline::clickTrack()
{
    Track click;
    click.name = QStringLiteral("Click");
    click.instrumentType = QStringLiteral("drumKit");
    return click;
}

QList<Timeline::Message> Timeline::clickFor(const Score &score, const QList<int> &order)
{
    QList<Message> messages;
    Rational position;
    for (const int barIndex : order) {
        if (barIndex < 0 || barIndex >= score.masterBars.size()) {
            continue;
        }
        const MasterBar &master = score.masterBars.at(barIndex);
        const Rational length = master.length();
        const Rational beat = beatOf(master);

        // Counted from the bar line rather than accumulated across the piece,
        // so a bar that does not divide evenly by its own beat -- which is
        // what a pickup bar is -- does not push every beat after it sideways.
        bool first = true;
        for (Rational at; at < length; at += beat) {
            const int velocity = first ? AccentVelocity : PlainVelocity;
            messages.append({position + at, MessageKind::NoteOn, 0, ClickPitch, velocity});
            messages.append(
                {position + at + ClickLength, MessageKind::NoteOff, 0, ClickPitch, 0});
            first = false;
        }
        position += length;
    }
    return messages;
}

double Timeline::tempoAtBar(const Score &score, int bar)
{
    // 120 where a score says nothing at all, which is the same default the
    // rest of the program falls back to and the one every sequencer uses.
    double bpm = 120;
    int bestBar = -1;
    double bestPosition = -1;
    for (const TempoChange &tempo : score.tempos) {
        // The latest change at or before this bar, found by looking at all of
        // them rather than by stopping at the first one that is too late: the
        // editor keeps this list in order and an importer is not obliged to.
        if (tempo.bar > bar) {
            continue;
        }
        if (tempo.bar > bestBar || (tempo.bar == bestBar && tempo.position > bestPosition)) {
            bestBar = tempo.bar;
            bestPosition = tempo.position;
            bpm = tempo.quarterBpm;
        }
    }
    return bpm;
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


Timeline::Clock::Clock(const Score &score, const QList<int> &order)
    : m_tempos(tempoMap(score, order))
    , m_length(length(score, order))
{
    double elapsed = 0;
    for (int index = 0; index < m_tempos.size(); ++index) {
        m_secondsAtTempo.append(elapsed);
        if (index + 1 < m_tempos.size()) {
            const double quarters =
                m_tempos.at(index + 1).at.toDouble() - m_tempos.at(index).at.toDouble();
            elapsed += quarters * 60.0 / std::max(m_tempos.at(index).quarterBpm, 1.0);
        }
    }
}

double Timeline::Clock::secondsAt(const Rational &quarters) const
{
    if (m_tempos.isEmpty()) {
        return quarters.toDouble() * 60.0 / 120.0;
    }

    int section = 0;
    while (section + 1 < m_tempos.size() && m_tempos.at(section + 1).at < quarters) {
        ++section;
    }
    const double into = quarters.toDouble() - m_tempos.at(section).at.toDouble();
    return m_secondsAtTempo.at(section)
        + std::max(0.0, into) * 60.0 / std::max(m_tempos.at(section).quarterBpm, 1.0);
}

double Timeline::Clock::totalSeconds() const
{
    return secondsAt(m_length);
}

QList<Timeline::Message> Timeline::messagesFor(const Score &score, int trackIndex,
                                               const QList<int> &order)
{
    QList<Message> messages;
    if (trackIndex < 0 || trackIndex >= score.tracks.size()) {
        return messages;
    }

    const QList<NoteEvent> notes = notesFor(score, trackIndex, order);

    // Every channel this track will use, told its bend range before anything
    // plays. A synth that has not been told assumes two semitones, and every
    // bend comes out a sixth of its written size.
    QSet<int> channels;
    for (const NoteEvent &note : notes) {
        channels.insert(note.channel);
    }
    QList<int> sorted(channels.constBegin(), channels.constEnd());
    std::sort(sorted.begin(), sorted.end());
    for (const int channel : std::as_const(sorted)) {
        messages.append({Rational(0), MessageKind::BendRange, channel, BendRangeSemitones, 0});
    }

    for (const NoteEvent &note : notes) {
        if (!note.bend.isEmpty()) {
            messages.append({note.start, MessageKind::PitchBend, note.channel,
                             bendValueFor(note.bend.first().cents), 0});

            for (int point = 1; point < note.bend.size(); ++point) {
                const BendPoint &from = note.bend.at(point - 1);
                const BendPoint &to = note.bend.at(point);
                const double span = to.at.toDouble() - from.at.toDouble();
                if (span <= 0) {
                    messages.append({note.start + to.at, MessageKind::PitchBend,
                                     note.channel, bendValueFor(to.cents), 0});
                    continue;
                }
                // Straight lines between the written points, in steps small
                // enough to hear as movement rather than as arrival.
                const int steps = std::clamp(int(span / BendStep.toDouble()), 1,
                                             MaximumBendSteps);
                for (int step = 1; step <= steps; ++step) {
                    const Rational at =
                        from.at + (to.at + from.at * Rational(-1)) * Rational(step, steps);
                    const int cents =
                        from.cents + (to.cents - from.cents) * step / steps;
                    messages.append({note.start + at, MessageKind::PitchBend,
                                     note.channel, bendValueFor(cents), 0});
                }
            }
        }

        messages.append({note.start, MessageKind::NoteOn, note.channel,
                         note.pitch, note.velocity});
        messages.append({note.end, MessageKind::NoteOff, note.channel, note.pitch, 0});

        if (!note.bend.isEmpty()) {
            // Back to centre once the note is done, or the next note on this
            // string inherits the bend -- the exact failure the per-string
            // channels exist to prevent, reintroduced at the last moment.
            messages.append({note.end, MessageKind::PitchBend, note.channel, BendCentre, 0});
        }
    }

    // Where two land together: set up before playing, release before the
    // recentre that follows it, and never recentre before a note-on.
    const auto rank = [](MessageKind kind) {
        switch (kind) {
        case MessageKind::BendRange: return 0;
        case MessageKind::NoteOff:   return 1;
        case MessageKind::PitchBend: return 2;
        case MessageKind::NoteOn:    return 3;
        }
        return 4;
    };
    std::stable_sort(messages.begin(), messages.end(),
                     [&rank](const Message &a, const Message &b) {
                         if (a.at == b.at) {
                             return rank(a.kind) < rank(b.kind);
                         }
                         return a.at < b.at;
                     });
    return messages;
}


double Timeline::secondsAtPass(const Score &score, const QList<int> &order,
                               const Clock &clock, int pass)
{
    // Walked rather than searched, and for the same reason barAt walks: a
    // score is a few hundred bars, and two ways of adding them up would
    // eventually disagree about where a bar begins.
    Rational position;
    for (int index = 0; index < order.size() && index < pass; ++index) {
        position += score.masterBars.at(order.at(index)).length();
    }
    return clock.secondsAt(position);
}

int Timeline::barAt(const Score &score, const QList<int> &order, const Clock &clock,
                    double seconds)
{
    if (order.isEmpty() || seconds < 0) {
        return -1;
    }

    // Walked rather than searched: a score is a few hundred bars, this is
    // asked a few times a second, and a walk cannot disagree with the layout
    // about where a bar begins.
    Rational position;
    for (int index = 0; index < order.size(); ++index) {
        const Rational next = position + score.masterBars.at(order.at(index)).length();
        if (seconds < clock.secondsAt(next)) {
            return index;
        }
        position = next;
    }
    return -1;
}
