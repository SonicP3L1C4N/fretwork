// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#pragma once

#include "score.h"

#include <QList>
#include <QString>

/**
 * The instruments a new track can be, and what each one is.
 *
 * A short list rather than the whole General MIDI bank. A tablature program
 * writes for things with frets on them and for the kit sitting behind them,
 * and offering a hundred and twenty-eight programmes would be offering a
 * hundred and twenty of them to nobody. Anything else a score arrives holding
 * is kept exactly as it was read -- this list is what Fretwork can *make*, not
 * what it can open.
 *
 * The identifiers are gpif's own spellings, because that is what the importer
 * writes into `Track::instrumentType` and a second vocabulary for the same
 * five things would eventually disagree with the first.
 */
namespace Instruments
{
struct Kind {
    QString id;             //< gpif's own name: "electricGuitar", "drumKit"
    QString name;           //< what to call it in front of somebody
    int program = 0;        //< General MIDI programme
    QList<int> tuning;      //< empty for a kit, which has no strings
};

/** Everything a new track may be, in the order worth offering it. */
QList<Kind> all();

/** One by its gpif identifier, or a guitar where the identifier is unknown. */
Kind byId(const QString &id);
}
