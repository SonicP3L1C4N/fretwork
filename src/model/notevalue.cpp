// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "notevalue.h"

#include <KLocalizedString>

#include <QList>

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

int NoteValue::denominatorOf(const Rational &value)
{
    if (value.numerator <= 0) {
        return 0;
    }
    // The inverse of valueOf: four quarters to a semibreve, so the denominator
    // is four divided by the value -- and is only a denominator at all where
    // that division comes out whole.
    const Rational quarters = Rational(4) / value;
    return quarters.denominator == 1 ? int(quarters.numerator) : 0;
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

QString NoteValue::nameOf(const Rational &duration)
{
    const Written written = of(duration);
    // Named from the crotchet outwards, which is how the names themselves are
    // built: a quaver is half a crotchet and a semiquaver half of that.
    static const QList<QPair<Rational, const char *>> names = {
        {Rational(8), QT_TRANSLATE_NOOP("NoteValue", "breve")},
        {Rational(4), QT_TRANSLATE_NOOP("NoteValue", "semibreve")},
        {Rational(2), QT_TRANSLATE_NOOP("NoteValue", "minim")},
        {Rational(1), QT_TRANSLATE_NOOP("NoteValue", "crotchet")},
        {Rational(1, 2), QT_TRANSLATE_NOOP("NoteValue", "quaver")},
        {Rational(1, 4), QT_TRANSLATE_NOOP("NoteValue", "semiquaver")},
        {Rational(1, 8), QT_TRANSLATE_NOOP("NoteValue", "demisemiquaver")},
        {Rational(1, 16), QT_TRANSLATE_NOOP("NoteValue", "hemidemisemiquaver")},
    };

    QString name;
    for (const auto &known : names) {
        if (known.first == written.value) {
            name = i18nc("a note value", known.second);
            break;
        }
    }
    if (name.isEmpty()) {
        return i18n("odd value");
    }

    if (written.dots == 1) {
        name = i18n("dotted %1", name);
    } else if (written.dots > 1) {
        name = i18n("%1 with %2 dots", name, written.dots);
    }
    // What it was written as does not add up to what it lasts: something has
    // been divided into a tuplet, and saying only "quaver" would be wrong
    // about how long it is.
    if (!(durationOf(written) == duration)) {
        name = i18nc("a note value inside a tuplet", "%1 (tuplet)", name);
    }
    return name;
}
