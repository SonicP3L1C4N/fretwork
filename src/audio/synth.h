// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#pragma once

#include <QtGlobal>

/**
 * Whatever makes a part's sound.
 *
 * There are two: a FluidSynth playing a General MIDI programme, and a sampler
 * playing recordings. A part is one or the other and nothing downstream is
 * told which -- the renderer and the player both hold a list of these and ask
 * each for a block of frames, which is the one rule this engine has ever had.
 *
 * The interface is that rule and nothing else. Anything a caller could want to
 * know about *how* a sound is made would be a caller that had to ask, and then
 * there would be two kinds of track everywhere instead of here.
 */
class Synth
{
public:
    virtual ~Synth() = default;

    virtual bool isValid() const = 0;

    /**
     * Fill `frames` of audio starting at sample `at`.
     *
     * Positions arrive in order. `seek` is how they stop doing so, and is
     * safe from an audio callback: it silences and repositions a cursor and
     * allocates nothing.
     */
    virtual void fill(float *left, float *right, int frames, qint64 at) = 0;
    virtual void seek(qint64 sample) = 0;

    /** The sample the last event falls on, which is where a render may stop. */
    virtual qint64 lastEventSample() const = 0;
};
