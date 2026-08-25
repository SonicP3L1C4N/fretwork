// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#pragma once

#include "score.h"

#include <QList>
#include <QString>
#include <QStringList>

/**
 * A standard MIDI file, which is a convenience rather than the destination.
 *
 * Fretwork's playback gives every track a synth of its own and every string a
 * channel of that synth, so that bending one string does not bend the chord
 * under it. A MIDI file cannot hold that: the format has sixteen channels in
 * total, one of which is percussion, and a score with four guitars wants
 * twenty-four. Channels are therefore handed out greedily -- every track gets
 * one, and whatever is left over is spent widening tracks to a channel per
 * string, biggest instrument first -- and whatever had to be given up is
 * reported rather than quietly done.
 *
 * That compromise is a property of the file format and not of the program. It
 * disappears entirely at stem export, where each track is rendered by its own
 * synth and nothing is shared.
 */
namespace Midi
{
/** What a MIDI file could not express, in sentences fit to print. */
using Compromises = QStringList;

/**
 * Writes the whole score.
 *
 * `trackIndex` of -1 writes every track; anything else writes that one alone,
 * which is how a stem is made. Returns false and sets `error` on failure.
 */
bool write(const Score &score, const QList<int> &order, const QString &path,
           int trackIndex = -1, QString *error = nullptr,
           Compromises *compromises = nullptr);
}
