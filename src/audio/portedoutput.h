// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#pragma once

#include <QString>
#include <QStringList>

#include <memory>

/**
 * Every track as a pair of ports in the audio graph.
 *
 * This is the point of the project made true outside its own window. Fretwork
 * has always rendered a track to its own audio bus, and until now the only way
 * to get one anywhere else was to write a WAV and import it. With ports, a DAW
 * records the stems as they play: Ardour or Reaper arms eight tracks, links
 * them to these, and presses record.
 *
 * **One node with many ports, not many nodes.** A node per track would be a
 * node per track in the graph and, worse, a clock per track: PipeWire drives
 * each stream's callback on its own, and eight callbacks filling eight synths
 * from eight ideas of "now" is eight things to drift apart. One filter with
 * sixteen ports has one callback, one position, and one answer to what time it
 * is -- which is also what a hardware multitrack interface looks like from the
 * other end of a cable.
 *
 * **What runs in the callback.** The same rule as everywhere else in this
 * program: fill buffers, read atomics, and nothing that allocates, locks or
 * logs. A port nobody has linked hands back no buffer, so the filler is given
 * a scratch one instead -- the synth behind it has to run either way, or a
 * track that was linked halfway through a piece would play everything it slept
 * through into the block where it was plugged in.
 */
class PortedOutput
{
public:
    struct Options {
        /** What the node is called in the graph. */
        QString name = QStringLiteral("Fretwork");

        /** One stereo pair per entry, named after it. */
        QStringList ports;

        int sampleRate = 48000;

        /**
         * Whether to ask the graph to plug this in to the speakers.
         *
         * On, so that playing with ports open still makes a noise: a transport
         * that went silent because the audio had become more useful would be a
         * poor trade. A DAW links what it wants regardless.
         */
        bool autoConnect = true;
    };

    /**
     * Called from the audio thread with a buffer per port pair.
     *
     * `left` and `right` hold `Options::ports.size()` pointers each, in the
     * order the ports were named.
     */
    using Process = void (*)(void *data, int frames, float *const *left, float *const *right);

    PortedOutput(const Options &options, Process process, void *data);
    ~PortedOutput();

    PortedOutput(const PortedOutput &) = delete;
    PortedOutput &operator=(const PortedOutput &) = delete;

    bool isValid() const;
    QString error() const;

    /** How many stereo pairs are in the graph. */
    int pairCount() const;

    /**
     * What the audio thread and the rest of the class share.
     *
     * Public for the same reason `AudioInput`'s is: PipeWire's callbacks are
     * free functions handed a void*, and a pimpl nobody outside can name is
     * one they cannot cast back to.
     */
    struct Private;

private:
    std::unique_ptr<Private> d;
};
