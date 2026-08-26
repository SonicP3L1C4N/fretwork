// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#pragma once

#include "score.h"

/**
 * The score as played, rather than as written.
 *
 * Two ideas keep this a separate step from both the document and the engine.
 *
 * The first is that repeats, alternate endings and da capos are a property of
 * the notation, not of the music: they are flattened here, once, into a list of
 * bars in the order they are heard. Nothing downstream ever reasons about a
 * jump, and in particular the audio thread never does.
 *
 * The second is that a channel is per string. MIDI pitch bend applies to a
 * whole channel, so a guitar whose six strings share one channel cannot bend a
 * note while another rings -- every held note bends with it. Guitar Pro solves
 * this the same way and its file format says so out loud, in a track flag
 * called UseOneChannelPerString. Channels here are numbered inside the track's
 * own synth, because the design gives each track a synth of its own; sixteen
 * channels is a limit of a MIDI cable, and this is not one.
 */
namespace Timeline
{
/**
 * A point on a bend curve: how far into the note, and how far from the written
 * pitch in cents. 100 is a semitone, 200 a whole tone -- which is what gpif
 * means by its hundredths of a semitone, at the same scale.
 */
struct BendPoint {
    Rational at;        //< quarters from the start of the note
    int cents = 0;
};

/** One sounding note, in quarters from the start of the performance. */
struct NoteEvent {
    Rational start;
    Rational end;
    int pitch = 0;
    int velocity = 0;
    int channel = 0;    //< inside this track's own synth, not a global MIDI bus
    int string = -1;

    /**
     * Empty for the great majority of notes. Where it is not, the first point
     * is where the bend starts and the last is where it ends, and whatever
     * plays this is expected to move continuously between them.
     */
    QList<BendPoint> bend;

    /**
     * Struck by the fretting hand rather than picked -- a hammer-on, a pull-off
     * or a legato slide. A sampler with legato articulations would play a
     * different sample; until P4 there is one, and it is quieter.
     */
    bool legato = false;
};

struct TempoEvent {
    Rational at;
    double quarterBpm = 120;
};

/** What a synth is actually told, once a note has become a performance. */
enum class MessageKind {
    NoteOn,
    NoteOff,
    PitchBend,      //< data1 carries the whole 14-bit value, 8192 being centre
    BendRange,      //< data1 is semitones; whatever plays this sets it up its own way
};

/**
 * One instruction to one channel of one track's synth.
 *
 * The same list drives the MIDI writer and the audio renderer, which is the
 * point: two implementations of "when does the bend move" would disagree
 * eventually, and the one nobody listened to would be the wrong one.
 */
struct Message {
    Rational at;
    MessageKind kind = MessageKind::NoteOn;
    int channel = 0;    //< inside this track's own synth
    int data1 = 0;      //< pitch, or bend value, or semitones
    int data2 = 0;      //< velocity
};

/**
 * Turns a position in quarters into a position in seconds.
 *
 * Built once from the tempo map and asked many times, because a renderer needs
 * this for every note and the tempo map is a list to be walked.
 */
class Clock
{
public:
    Clock(const Score &score, const QList<int> &order);

    double secondsAt(const Rational &quarters) const;
    double totalSeconds() const;

private:
    QList<TempoEvent> m_tempos;
    QList<double> m_secondsAtTempo;     //< when each tempo section begins
    Rational m_length;
};

/**
 * The master bar indices in the order they are heard.
 *
 * Simple start/end repeats are expanded. Alternate endings are not yet, and a
 * score using them is reported rather than quietly played wrongly -- see
 * hasAlternateEndings().
 */
QList<int> playedOrder(const Score &score, bool expandRepeats = true);

/** Whether flattening this score is currently approximate. */
bool hasAlternateEndings(const Score &score);

/** Every note of one track, already tied together and sorted by start. */
QList<NoteEvent> notesFor(const Score &score, int trackIndex, const QList<int> &order);

/**
 * The same notes as instructions, with their bends drawn as curves.
 *
 * Sorted by time, and by kind where two land together: a channel is told its
 * bend range before it is asked to play, and returned to centre after a bent
 * note rather than during the next one.
 */
QList<Message> messagesFor(const Score &score, int trackIndex, const QList<int> &order);

/** The tempo changes, placed on the played timeline rather than the notated one. */
QList<TempoEvent> tempoMap(const Score &score, const QList<int> &order);

/** How long the performance is, in quarters. */
Rational length(const Score &score, const QList<int> &order);

/** How long the performance is, in seconds, tempo changes included. */
double seconds(const Score &score, const QList<int> &order);

/**
 * Which bar is sounding at `seconds`, as an index into `order`.
 *
 * Two answers are wanted here and they are different: `order[result]` is the
 * bar as the score writes it, which is what a reader wants highlighted, while
 * `result` itself is the pass through it, which is what a playhead is really
 * on. Returns -1 before the beginning and after the end.
 */
int barAt(const Score &score, const QList<int> &order, const Clock &clock, double seconds);

/**
 * When a pass through the played order begins, in seconds.
 *
 * The other direction of `barAt`, and indexed the same way: a pass rather than
 * a bar, because a bar inside a repeat happens more than once and "when does
 * bar 12 start" has as many answers as there are times through it.
 */
double secondsAtPass(const Score &score, const QList<int> &order, const Clock &clock,
                     int pass);
}
