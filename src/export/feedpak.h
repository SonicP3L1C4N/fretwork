// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#pragma once

#include "score.h"

#include <QJsonObject>
#include <QList>
#include <QString>

/**
 * A practice pack, written out of a score.
 *
 * fee[dB]ack reads `.feedpak` files: a ZIP holding a manifest, one arrangement
 * of notes per playable part, and audio. The fit is unusually close, and worth
 * saying why rather than treating it as luck -- a `.fw` is already a ZIP of
 * readable JSON, this program already renders one audio file per track, and
 * its timeline already turns notated durations into seconds with tempo changes
 * and swing applied, which is exactly the arithmetic a pack's `t` and `sus`
 * are. Nothing else on Linux holds both halves: a tab editor has the notation
 * and cannot render stems per part, and a DAW has the stems and knows nothing
 * about frets.
 *
 * This is the walking skeleton the roadmap asks for. A manifest, an
 * arrangement per fretted part, stems from the existing renderer, no notation
 * file, and **only the techniques this program can honestly claim**. The
 * missing ones are named in `docs/roadmap.md` and are the same ones the README
 * already admits to; a practice program is the worst place to guess, because a
 * learner is being marked against what the pack says is there.
 *
 * Deliberately one-way. Reading a pack back is a separate question and
 * probably an unnecessary one: a pack is closer to a render than to a save.
 */
namespace Feedpak
{
struct Options {
    /** Empty finds a General MIDI SoundFont, as the renderer does. */
    QString soundFont;

    /**
     * Whether to render the audio.
     *
     * A pack without stems is not a pack anybody can practise to, but it is
     * exactly what a test wants: the notes and the manifest are the part with
     * decisions in them, and rendering four minutes of audio to check a JSON
     * field is a slow way to learn nothing.
     */
    bool stems = true;

    int sampleRate = 48000;
};

/**
 * The manifest, as YAML.
 *
 * `duration` is the played length in seconds, which the caller has because it
 * has the timeline. Tuning is written as **semitone offsets from standard**,
 * not as pitches: every pack on this machine is in standard tuning and writes
 * six zeros, and six zeros cannot be pitches. Stated as the inference it is,
 * since no pack here settles it.
 */
QByteArray manifestFor(const Score &score, double duration);

/**
 * One part's notes, in seconds, with the techniques that survive the trip.
 *
 * String numbering is the same as this program's -- nought is the lowest --
 * which was measured rather than assumed: the sample pack's tuning is all
 * zeros and says nothing, so the check was to read a known melody off the
 * numbers. Ode to Joy comes out as F#, F#, G, A only if nought is the low
 * string; the other way round it is a tune nobody wrote.
 */
QJsonObject arrangementFor(const Score &score, int track, const QList<int> &order);

/** Which parts become arrangements: the fretted ones, in score order. */
QList<int> playableParts(const Score &score);

/** What a part's stem is called, so the manifest and the audio agree. */
QString stemIdFor(const Score &score, int track);

/** Writes the pack. Returns false and sets `error` on failure. */
bool write(const Score &score, const QList<int> &order, const QString &path,
           const Options &options, QString *error = nullptr);
}
