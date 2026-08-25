// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#pragma once

#include "score.h"

#include <QString>

/**
 * Fretwork's own format: a ZIP holding JSON.
 *
 * `manifest.json` says what it is and which version of the format wrote it;
 * `score.json` is the document. Both are readable text, which is the point —
 * a bug report can arrive with a file attached and be understood by looking at
 * it, and a format that changes can be migrated with code somebody can follow.
 *
 * **What this does not claim.** A `.fw` holds Fretwork's model of a score, not
 * everything a Guitar Pro file contained. Lyrics, chord diagrams, engraving
 * preferences and Guitar Pro's own effects are read past on import and are not
 * here to be written out. Saving as `.fw` is therefore not a way to round-trip
 * a `.gp`, and Fretwork deliberately cannot write `.gp` at all: reading a
 * format nobody documented is one risk, and handing people files to open in
 * somebody else's program is a different and worse one.
 *
 * Unknown keys are ignored on reading rather than refused, and every optional
 * field defaults to what a blank one would mean — so a file written by a later
 * version opens in an earlier one with whatever it understands.
 */
namespace Fw
{
/** Bumped only when an older reader could not make sense of a newer file. */
constexpr int FormatVersion = 1;

/** The extension, without the dot. */
QString extension();

/** Writes `score` to `path`. Returns false and sets `error` on failure. */
bool write(const Score &score, const QString &path, QString *error = nullptr);

/** Reads a `.fw`. Returns an empty Score, and sets `error`, on failure. */
Score read(const QString &path, QString *error = nullptr);

/** Whether a path looks like one of ours, by extension alone. */
bool looksLikeOurs(const QString &path);
}
