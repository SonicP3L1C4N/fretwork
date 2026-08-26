// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "swing.h"

#include <KLocalizedString>

namespace
{
struct Named {
    TripletFeel feel;
    const char *token;          //< gpif's spelling
    const char *name;           //< a musician's
};

/** One table, because two would eventually disagree about Scottish16th. */
const Named Feels[] = {
    {TripletFeel::Triplet8th, "Triplet8th", QT_TRANSLATE_NOOP("Swing", "triplet quavers")},
    {TripletFeel::Triplet16th, "Triplet16th",
     QT_TRANSLATE_NOOP("Swing", "triplet semiquavers")},
    {TripletFeel::Dotted8th, "Dotted8th", QT_TRANSLATE_NOOP("Swing", "dotted quavers")},
    {TripletFeel::Dotted16th, "Dotted16th", QT_TRANSLATE_NOOP("Swing", "dotted semiquavers")},
    {TripletFeel::Scottish8th, "Scottish8th", QT_TRANSLATE_NOOP("Swing", "snapped quavers")},
    {TripletFeel::Scottish16th, "Scottish16th",
     QT_TRANSLATE_NOOP("Swing", "snapped semiquavers")},
};
}

bool Swing::isSwung(TripletFeel feel)
{
    return feel != TripletFeel::None;
}

Rational Swing::unitOf(TripletFeel feel)
{
    switch (feel) {
    case TripletFeel::Triplet8th:
    case TripletFeel::Dotted8th:
    case TripletFeel::Scottish8th:
        return Rational(1);             // a pair of quavers is a crotchet
    case TripletFeel::Triplet16th:
    case TripletFeel::Dotted16th:
    case TripletFeel::Scottish16th:
        return Rational(1, 2);          // a pair of semiquavers is a quaver
    case TripletFeel::None:
        break;
    }
    return Rational(1);
}

Rational Swing::shareOf(TripletFeel feel)
{
    switch (feel) {
    case TripletFeel::Triplet8th:
    case TripletFeel::Triplet16th:
        // The long note is two of a triplet's three, which is what everybody
        // means by a shuffle and is nearer to even than it is written.
        return Rational(2, 3);
    case TripletFeel::Dotted8th:
    case TripletFeel::Dotted16th:
        return Rational(3, 4);
    case TripletFeel::Scottish8th:
    case TripletFeel::Scottish16th:
        // The short note first, which is the whole of what a snap is.
        return Rational(1, 4);
    case TripletFeel::None:
        break;
    }
    return Rational(1, 2);
}

Rational Swing::played(const Rational &within, TripletFeel feel, const Rational &barLength)
{
    if (!isSwung(feel) || within < Rational(0)) {
        return within;
    }

    const Rational unit = unitOf(feel);
    const qint64 pairs = within.dividedBy(unit);
    const Rational begins = unit * Rational(pairs);

    // A pair that runs off the end of the bar is not a pair. Leaving it alone
    // keeps the warp continuous -- it is the identity at `begins` either way --
    // and keeps the bar exactly as long as it was.
    if (barLength < begins + unit) {
        return within;
    }

    const Rational into = within - begins;
    const Rational half = unit * Rational(1, 2);
    const Rational share = shareOf(feel);

    // Two straight lines meeting at the middle of the pair: the first half of
    // the written pair is stretched onto the first `share` of it, and the
    // second half onto what is left.
    const Rational moved = into < half
        ? into * share * Rational(2)
        : unit * share + (into - half) * (Rational(1) - share) * Rational(2);
    return begins + moved;
}

QString Swing::nameOf(TripletFeel feel)
{
    for (const Named &known : Feels) {
        if (known.feel == feel) {
            return i18nc("how a bar swings", known.name);
        }
    }
    return QString();
}

QString Swing::tokenOf(TripletFeel feel)
{
    for (const Named &known : Feels) {
        if (known.feel == feel) {
            return QString::fromLatin1(known.token);
        }
    }
    return QString();
}

TripletFeel Swing::fromToken(const QString &token)
{
    for (const Named &known : Feels) {
        if (token == QLatin1String(known.token)) {
            return known.feel;
        }
    }
    return TripletFeel::None;
}
