// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#pragma once

#include "rational.h"

#include <QString>

/**
 * What a duration was written as.
 *
 * The document does not record it. `Score::rhythms` holds durations with their
 * dots and tuplets already multiplied in, which is exactly what playback wants
 * -- how long does this last -- and no use at all to anything that has to deal
 * in symbols. A page has to draw a dotted crotchet as a dotted crotchet, and an
 * editor asked for one dot more has to know how many there are now.
 *
 * Both want the same answer, so there is one of these rather than two that can
 * disagree.
 */
namespace NoteValue
{
/** A written note value in quarters, and the dots after it. */
struct Written {
    Rational value = Rational(1);
    int dots = 0;

    bool operator==(const Written &other) const
    {
        return value == other.value && dots == other.dots;
    }
};

/** The shortest and longest worth writing: a hemidemisemiquaver and a breve. */
inline const Rational Shortest(1, 16);
inline const Rational Longest(8);

/** A duration is a note value when it is a power of two quarters, up or down. */
bool isValue(const Rational &duration);

/**
 * The symbol a duration was written as.
 *
 * There is only one way three quarters of a crotchet can have been written
 * down, so asking gets it back. A tuplet cannot be recovered this way -- two
 * thirds of a crotchet is no dotted anything -- and comes back as the value it
 * is written as, which for a triplet quaver is a quaver, and is what a reader
 * sees on the page in any case.
 */
Written of(const Rational &duration);

/** How long that symbol lasts. The inverse of `of`, for everything but tuplets. */
Rational durationOf(const Written &written);

/** The value musicians name by its denominator: 4 is a crotchet, 8 a quaver. */
Rational valueOf(int denominator);

/**
 * The denominator back again, which is what every format writes.
 *
 * A pack says `dur: 8` and MusicXML says `<type>eighth</type>`; both are this
 * number, and both exporters were about to work it out for themselves. Nought
 * for anything that is not a written value, which a caller should treat as a
 * duration it cannot name rather than as one lasting no time.
 */
int denominatorOf(const Rational &value);

/** How many beams a value carries: none for a crotchet, one for a quaver. */
int beamsOf(const Rational &value);

/**
 * What to call it: "quaver", "dotted crotchet", "triplet quaver".
 *
 * For saying out loud -- a status bar, a tooltip -- rather than for drawing.
 * Anything that is not a written value is named by the value it is written as
 * and marked as a tuplet, because "five sixths of a crotchet" is not a thing
 * anybody has ever said about a piece of music.
 */
QString nameOf(const Rational &duration);
}
