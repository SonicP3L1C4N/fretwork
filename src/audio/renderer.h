// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#pragma once

#include "score.h"

#include <QList>
#include <QHash>
#include <QString>
#include <QStringList>

/**
 * Turning a score into sound: one synthesiser per track, each rendering to its
 * own audio.
 *
 * This is what the project is for. A tablature program that plays a score
 * mixes everything into one bus and hands you the result; giving each track a
 * synth of its own means a track can be soloed, sent through its own amplifier
 * simulation, and written out as an independent file -- and it means the
 * sixteen-channel limit that constrains the MIDI writer does not apply here at
 * all, because nothing is shared. Each guitar gets a channel per string with
 * fifteen to spare.
 *
 * **The rule this is built on:** the engine only knows how to fill a block of
 * frames at a sample position. It has no idea what time it is. Offline
 * rendering runs that loop as fast as the machine allows; live playback will
 * run the identical loop from an audio callback. An engine that knew about
 * wall-clock time would need a second implementation for one of the two, and
 * the second one would be the one with the bugs.
 */
namespace Render
{
struct Options {
    QString soundFont;              //< empty means "find a General MIDI one"
    int sampleRate = 48000;
    int blockFrames = 512;
    double gain = 0.4;              //< FluidSynth's own, before any mixing

    /**
     * How long to keep rendering after the last note ends.
     *
     * A score does not stop when its final note-off is sent: the sound decays,
     * and reverb outlasts that. Cutting at the last note-off is the standard
     * way a rendered file ends with an audible click.
     */
    double tailSeconds = 3.0;

    /**
     * Write a click as a stem of its own.
     *
     * Its own stem and not in the mix. Stems exist to be put back together
     * somewhere else, and a click baked into the mix is one nobody can take
     * out again -- whereas one sitting beside it as a file is a click anybody
     * can drag in.
     */
    bool click = false;

    /** An `.sfz` per track, by track number, for the parts that have one. */
    QHash<int, QString> samplers;

    /** An LV2 chain per track, as plugin URIs in order, nearest the instrument first. */
    QHash<int, QStringList> effects;
};

/** A General MIDI SoundFont on this machine, or empty if there is none. */
QString findSoundFont();

struct Written {
    QString path;
    float peak = 0;
    double seconds = 0;
};

/**
 * Renders each track to `directory` as its own WAV, and the sum of them as a
 * mix, in a single pass over the score.
 *
 * `trackIndex` of -1 renders every track. Returns false and sets `error` on
 * failure; `written` lists what reached the disk.
 */
bool stems(const Score &score, const QList<int> &order, const QString &directory,
           const Options &options, QString *error = nullptr,
           QList<Written> *written = nullptr);
}
