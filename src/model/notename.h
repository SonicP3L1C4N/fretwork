// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#pragma once

#include <QString>

/**
 * What a MIDI pitch is called.
 *
 * Sharps rather than flats, because a tablature program has no key signature
 * to decide the question with: a score's tuning is a list of pitches and
 * nothing in the document says whether the fourth string is a D# or an E flat.
 * Guessing would be wrong half the time; picking one and saying so is wrong
 * never, and is what a tuner display needs.
 *
 * That reasoning holds for what this is for -- naming a string, which has no
 * key -- and stops holding where a score does have one. [Key] is the other
 * side of that line: it spells a note as the key writes it, so it answers D
 * sharp or E flat where this always answers D sharp. The two disagreeing about
 * MIDI 63 is them being right about different questions, and neither should
 * grow into the other.
 *
 * Octave numbers are scientific pitch notation, where middle C is C4 and MIDI
 * 60. That is the convention printed on a guitar's own literature -- the low
 * string of a guitar in standard tuning is E2 -- and not the one Yamaha uses,
 * which would call it E1.
 */
namespace NoteName
{
/** "C", "C#", "D" -- the pitch class, with no octave. */
QString pitchClass(int midi);

/** "E2", "A#3" -- the pitch class and the octave it is in. */
QString of(int midi);

/**
 * A name read back into a pitch: "E2", "A#3", "Eb2", or a plain number.
 *
 * Flats are accepted although they are never written: somebody typing a
 * tuning in has a name in their head and it is as likely to be E flat as D
 * sharp, and refusing half of the ways to say one note would be a refusal
 * about spelling rather than about music. A bare number is accepted too,
 * because the program prints tunings as numbers in `--info` and anything it
 * prints it should be able to read.
 *
 * Returns -1 for anything it cannot make sense of.
 */
int parse(const QString &name);
}
