// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "notevalue.h"

namespace
{
/** Every note value is a power of two quarters, up or down. */
bool isPowerOfTwo(qint64 value)
{
    return value > 0 && (value & (value - 1)) == 0;
}

/** A dot adds half of what came before it; a second adds half of that. */
Rational dotting(int dots)
{
    switch (dots) {
    case 1:
        return Rational(3, 2);
    case 2:
        return Rational(7, 4);
    default:
        return Rational(1);
    }
}
}

bool NoteValue::isValue(const Rational &duration)
{
    if (duration.numerator <= 0) {
        return false;
    }
    return (duration.denominator == 1 && isPowerOfTwo(duration.numerator))
        || (duration.numerator == 1 && isPowerOfTwo(duration.denominator));
}

NoteValue::Written NoteValue::of(const Rational &duration)
{
    for (int dots = 0; dots < 3; ++dots) {
        const Rational multiplier = dotting(dots);
        const Rational value(duration.numerator * multiplier.denominator,
                             duration.denominator * multiplier.numerator);
        if (isValue(value) && !(value < Shortest) && !(Longest < value)) {
            return {value, dots};
        }
    }

    // A tuplet. Nothing about how it was written survives in the duration, so
    // it is drawn as the next value up -- which is how a triplet is written:
    // three quavers in the time of two, each still a quaver on the page.
    Rational value = Shortest;
    while (value < duration && value < Longest) {
        value = value * Rational(2);
    }
    return {value, 0};
}

Rational NoteValue::durationOf(const Written &written)
{
    return written.value * dotting(written.dots);
}

Rational NoteValue::valueOf(int denominator)
{
    if (denominator <= 0 || !isPowerOfTwo(denominator)) {
        return {};
    }
    // A crotchet is a quarter of a semibreve and one quarter long, which is
    // the whole of the arithmetic.
    const Rational value(4, denominator);
    return (value < Shortest || Longest < value) ? Rational() : value;
}

int NoteValue::beamsOf(const Rational &value)
{
    // A quaver is half a quarter and carries one beam; every halving adds one.
    int beams = 0;
    Rational remaining = value;
    while (remaining < Rational(1) && beams < 6) {
        remaining = remaining * Rational(2);
        ++beams;
    }
    return beams;
}
