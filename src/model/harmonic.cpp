// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "harmonic.h"

#include <QHash>

#include <cmath>

namespace
{
/**
 * How far a stored node may sit from a real one and still be taken for it.
 *
 * The four values in the corpus miss their true nodes by 0.000, 0.025, 0.063
 * and 0.088 of a fret, gpif having stored them to one decimal place and not
 * rounded them consistently. This has to clear the worst of those, and 0.15
 * does it with room left over.
 *
 * It cannot be made large enough to be unambiguous, and picking a rounder
 * number would only hide that. Nodes crowd together as partials rise -- the
 * sixteenth and the fifteenth are 0.077 of a fret apart near the first fret --
 * and no tolerance tells those two apart. What settles it is taking the lowest
 * partial with a node in range, below, which is deterministic and is also what
 * the string does: where nodes coincide, the low partial is the one that rings.
 *
 * The crowding has a second consequence worth knowing. Sampling every tenth of
 * a fret over two octaves, 149 of 240 positions land within tolerance of some
 * node, so *most* of the neck names a partial and returning none is unusual.
 * That makes the zero case a guard against nonsense rather than a case real
 * files reach -- Guitar Pro writes values from its own menu, and every one of
 * them is a node.
 */
constexpr double Tolerance = 0.15;

/** Partials past this are not playable and not in any menu Guitar Pro offers. */
constexpr int HighestPartial = 16;
}

namespace Harmonic
{
int partialAt(double node)
{
    if (!(node > 0.0)) {
        return 0;
    }

    // The lowest partial with a node here wins: it is the one that sounds.
    for (int partial = 2; partial <= HighestPartial; ++partial) {
        for (int k = 1; k < partial; ++k) {
            const double where =
                12.0 * std::log2(double(partial) / double(partial - k));
            if (std::abs(where - node) <= Tolerance) {
                return partial;
            }
        }
    }
    return 0;
}

double offsetAbove(double node)
{
    const int partial = partialAt(node);
    return partial == 0 ? 0.0 : 12.0 * std::log2(double(partial));
}

int sounding(Type type, int openString, int fret, double node)
{
    const int fretted = openString + fret;
    if (type == Type::None) {
        return fretted;
    }

    const double offset = offsetAbove(node);
    if (offset == 0.0) {
        return fretted;
    }

    // A natural harmonic is a partial of the whole string, so the fretting
    // hand does not come into it. Every other kind is touched a fixed distance
    // above wherever that hand already is.
    const double from = type == Type::Natural ? double(openString) : double(fretted);
    return int(std::lround(from + offset));
}

Type typeFrom(const QString &name)
{
    static const QHash<QString, Type> known = {
        {QStringLiteral("natural"), Type::Natural},
        {QStringLiteral("artificial"), Type::Artificial},
        {QStringLiteral("pinch"), Type::Pinch},
        {QStringLiteral("tap"), Type::Tap},
        {QStringLiteral("semi"), Type::Semi},
        {QStringLiteral("feedback"), Type::Feedback},
    };
    return known.value(name.toLower(), Type::None);
}

QString nameOf(Type type)
{
    switch (type) {
    case Type::None:       return QString();
    case Type::Natural:    return QStringLiteral("natural");
    case Type::Artificial: return QStringLiteral("artificial");
    case Type::Pinch:      return QStringLiteral("pinch");
    case Type::Tap:        return QStringLiteral("tap");
    case Type::Semi:       return QStringLiteral("semi");
    case Type::Feedback:   return QStringLiteral("feedback");
    }
    return QString();
}
}
