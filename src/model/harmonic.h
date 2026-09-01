// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#pragma once

#include <QString>

/**
 * What a harmonic sounds, as opposed to where it is played.
 *
 * Every other technique in this model leaves the pitch alone. A harmonic does
 * not: the finger touches the string without pressing it, the fundamental is
 * cancelled, and what sounds is a partial two or three octaves above the note
 * anybody would say was being played. The gap between the two is the whole
 * reason this file exists.
 *
 * It matters because gpif does not close that gap for us. A harmonic note's
 * `Midi` carries the *fretted* pitch -- measured across the corpus on
 * 2026-09-01, where `Midi` equals `tuning[String] + Fret` for all twelve
 * harmonics in it, exactly as it does for all 32,140 plain notes. A program
 * that trusts `Midi` therefore plays a harmonic at the pitch of the note under
 * the finger, confidently and in the wrong octave, and nothing about the
 * output looks like a bug: it looks like a bad transcription.
 *
 * The arithmetic below is physics rather than convention, which is the useful
 * thing about it. Where you touch a string decides which partial rings, and
 * that is true of a guitar and not merely true of a file format. So a natural
 * harmonic can be worked out from first principles and checked against the
 * numbers a real file happens to carry, rather than the other way round.
 */
namespace Harmonic
{
/**
 * The kinds gpif distinguishes, spelled as it spells them.
 *
 * Only `Natural` and `Semi` have ever been seen in a real file here -- seven
 * of the first in Sharp Dressed Man and five of the second in Twilight Of The
 * Thunder God -- and the rest are listed because the format lists them.
 */
enum class Type {
    None,
    Natural,     //< touched on the open string; the fretting hand is not involved
    Artificial,
    Pinch,
    Tap,
    Semi,
    Feedback,
};

/**
 * Which partial rings when a string is touched `node` frets along it, or 0
 * where no partial has a node near enough to be the one meant.
 *
 * A partial n has nodes at 12*log2(n/(n-k)) frets for k in 1..n-1, and several
 * partials share a node: the twelfth fret is a node for every even one. The
 * lowest is returned, because the lowest is the one that actually sounds -- a
 * finger at the twelfth fret gives the octave, not the fourth partial.
 *
 * gpif stores these to one decimal place and does not round them consistently
 * (5.8 for 5.83, 8.2 for 8.14, 9.6 for 9.69), so the match is the nearest node
 * within a tolerance rather than an equality.
 */
int partialAt(double node);

/**
 * How far above its open string a natural harmonic sounds, in semitones, or 0
 * where the node names no partial.
 *
 * This is not a whole number and is not rounded to one. The seventh partial is
 * 33.69 semitones up -- 31 cents flat of a minor seventh, which is why it
 * sounds the way it does and why a transcription that writes it as a minor
 * seventh is telling a small lie. Rounding belongs at the edge where a MIDI
 * note number is finally needed, not here.
 */
double offsetAbove(double node);

/**
 * What the note actually sounds, as a MIDI note number.
 *
 * `openString` is the pitch of the string unstopped, including any capo;
 * `fret` is where the fretting hand is, which a natural harmonic ignores and
 * every other kind is measured from; `node` is gpif's HarmonicFret.
 *
 * The distinction is the one piece here that is inference rather than
 * measurement, and it is worth being plain about which is which. That a
 * natural harmonic is a partial of the open string is physics. That the other
 * kinds are the same arithmetic relative to the fretted note is a reading of
 * five notes in one file, whose HarmonicFret is 12.0 while the hand is at
 * frets 7 to 11 -- consistent with a pinch harmonic an octave above the note,
 * which is what that passage sounds like, but consistent is not the same as
 * confirmed.
 *
 * Returns the fretted pitch unchanged for Type::None, and for any harmonic
 * whose node names no partial -- an unreadable harmonic is better played in
 * the wrong register than dropped, because at least it is still a note.
 */
int sounding(Type type, int openString, int fret, double node);

/** The gpif spelling, as it appears in HarmonicType. Unknown names are None. */
Type typeFrom(const QString &name);

/** The same spelling back, for the .fw format. Empty for None. */
QString nameOf(Type type);
}
