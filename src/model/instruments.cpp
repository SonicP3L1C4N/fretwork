// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "instruments.h"

#include <KLocalizedString>

QList<Instruments::Kind> Instruments::all()
{
    // Tunings are the standard ones, lowest string first, as MIDI pitches:
    // E2 A2 D3 G3 B3 E4 for a guitar, and E1 A1 D2 G2 for a bass.
    return {
        {QStringLiteral("electricGuitar"), i18n("Electric guitar"), 29,
         {40, 45, 50, 55, 59, 64}},
        {QStringLiteral("acousticGuitar"), i18n("Acoustic guitar"), 25,
         {40, 45, 50, 55, 59, 64}},
        {QStringLiteral("electricBass"), i18n("Bass guitar"), 33, {28, 33, 38, 43}},
        {QStringLiteral("drumKit"), i18n("Drum kit"), 0, {}},
        {QStringLiteral("piano"), i18n("Piano"), 0, {}},
    };
}

Instruments::Kind Instruments::byId(const QString &id)
{
    const QList<Kind> kinds = all();
    for (const Kind &kind : kinds) {
        if (kind.id == id) {
            return kind;
        }
    }
    // A score can hold a saxophone, and this list cannot make one. Asked about
    // an instrument it does not know, it answers with the one a tablature
    // program is for rather than with nothing.
    return kinds.first();
}
