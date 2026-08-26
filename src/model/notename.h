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
}
