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

/** The tempo changes, placed on the played timeline rather than the notated one. */
QList<TempoEvent> tempoMap(const Score &score, const QList<int> &order);

/** How long the performance is, in quarters. */
Rational length(const Score &score, const QList<int> &order);

/** How long the performance is, in seconds, tempo changes included. */
double seconds(const Score &score, const QList<int> &order);
}
