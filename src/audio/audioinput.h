// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#pragma once

#include <QString>

#include <atomic>
#include <memory>
#include <vector>

/**
 * Sound coming in, which this program has never had before.
 *
 * Everything Fretwork does with audio has until now gone one way: a score in,
 * a synthesiser, and PipeWire out. The tuner is the first thing that needs a
 * microphone or a guitar lead, and it will not be the last -- notes played in
 * from a pickup, a click somebody plays along to, and eventually recording
 * against the score all start here. So this is a general capture stream that a
 * tuner happens to be the first user of, rather than a tuner with a stream
 * inside it.
 *
 * **PipeWire directly, not through FluidSynth.** The output side borrows
 * FluidSynth's audio driver because FluidSynth is what makes the sound. There
 * is nothing on the input side to borrow: FluidSynth cannot record, so this
 * opens its own stream against the library the rest of the program is already
 * linked to. On a machine with no PipeWire it says so and does nothing, which
 * is the same bargain the player makes.
 *
 * **What runs in the callback.** The same rule as `Player`: the process
 * callback mixes the incoming frames to mono and advances one atomic. It does
 * not allocate, lock, or analyse -- pitch detection is several million
 * operations and belongs on whichever thread asked for the answer.
 *
 * The consumer therefore polls. `latest()` copies the most recent frames out
 * of a ring that holds well over a second, which is far more than any
 * detection window, so a reader that misses a tick simply gets fresher audio
 * next time rather than a gap.
 */
class AudioInput
{
public:
    struct Options {
        /** A PipeWire node name or object serial; empty means the desktop's own default. */
        QString device;
        /** What to ask for. The graph decides, and `sampleRate()` says what it decided. */
        int sampleRate = 48000;
        /** How much audio to keep. A detection window is a tenth of this. */
        double historySeconds = 1.5;
    };

    /**
     * No default argument, and not for want of one: a nested struct's own
     * defaults are not ready to be used inside the class that holds it.
     */
    explicit AudioInput(const Options &options);
    ~AudioInput();

    AudioInput(const AudioInput &) = delete;
    AudioInput &operator=(const AudioInput &) = delete;

    bool isValid() const;
    QString error() const;

    /** True once frames are actually arriving, which is not the same as connected. */
    bool isRunning() const;

    /** The rate the graph settled on, which may not be the rate that was asked for. */
    double sampleRate() const;

    /** How many channels the source has; they are mixed to one on the way in. */
    int channelCount() const;

    /** Frames captured since the stream opened, for "is anything happening at all". */
    qint64 framesCaptured() const;

    /**
     * The most recent `frames` samples, oldest first.
     *
     * Returns how many were written: `frames` when there is that much history,
     * and 0 when there is not yet, or when the writer overtook the read
     * mid-copy -- which at over a second of history means something has gone
     * badly wrong rather than that the reader was slow.
     */
    int latest(float *destination, int frames) const;

    /**
     * What the audio thread and the reader share.
     *
     * Public because PipeWire's callbacks are free functions handed a void*,
     * and a pimpl nobody outside can name is a pimpl those callbacks cannot
     * cast back to. Nothing else has any business with it.
     */
    struct Private;

private:
    std::unique_ptr<Private> d;
};
