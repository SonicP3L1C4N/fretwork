// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#pragma once

#include "score.h"

#include <QJsonObject>
#include <QList>
#include <QString>

/**
 * The notation half of a practice pack: what was written, rather than what is
 * heard.
 *
 * An arrangement file says a note starts at 12.4 seconds, lasts 0.3 of one and
 * is fret 5 on string 2. That is a performance, and it is what a practice
 * program marks against. A notation file says the same note is a dotted quaver
 * in the second bar of a piece in 4/4, which is what a reader draws. The two
 * are different documents about the same music and a pack carries both.
 *
 * This is **notation data, not engraving**. Nothing here decides where a note
 * head sits or how a beam slopes; it emits the durations, voices, bars and
 * pitches that something else lays out. The architecture calls a layout engine
 * "possibly never" and that has not changed -- what changed is that the piece
 * underneath it, pitch spelling, was built for the harmony work and is
 * therefore no longer in the way.
 *
 * The shape is measured, not invented. It comes from reading the one real
 * notation file on this machine, in the Für Elise pack, and the measurement is
 * worth knowing about because that file is a transcription of a *recording*:
 * its beats do not tile a bar, its rests are absent rather than written, and
 * its onsets are the performer's rather than the page's. A score read from a
 * `.gp` has none of those problems, so what this writes is better formed than
 * the sample it was learnt from. Where the two disagree the disagreement is
 * documented rather than resolved by imitation -- see `dot` in the source.
 */
namespace Notation
{
/**
 * Which clef a part is written in, spelled as a sign and a line the way the
 * sample pack spells it: `G2` is a treble clef, `F4` a bass.
 *
 * Decided by the instrument rather than by the tuning, which was the obvious
 * approach and is wrong. Measured across the corpus: a guitar in B standard
 * has its highest open string at MIDI 59, and a six-string bass has its at 64,
 * so the ranges overlap and a rule reading them would put a metal rhythm
 * guitar in the bass clef. The file says which instrument it is; there is no
 * need to guess from where its strings are tuned.
 *
 * A guitar's `G2` is the treble clef that sounds an octave lower than it is
 * written, which is how guitar music has always been set and is not something
 * this file has to say: the pitches written here are the ones that sound.
 */
QString clefFor(const Track &track);

/**
 * The notation document for one part.
 *
 * `staffId` names the one staff, and should be the same id the manifest uses
 * for the arrangement, so that a reader holding both can tell they are about
 * the same part. Passed in rather than worked out here, to keep this file from
 * having an opinion about how a pack names things.
 *
 * `order` is the played order of master bars, so a repeated section appears as
 * many times as it is heard. That is the same choice the arrangement file
 * makes, and for the same reason: a practice program follows a performance.
 */
QJsonObject documentFor(const Score &score, int track, const QList<int> &order,
                        const QString &staffId);
}
