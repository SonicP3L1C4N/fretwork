// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#pragma once

#include <QList>
#include <QString>

#include <memory>

/**
 * A chain of LV2 effects on one track's audio.
 *
 * This is the differentiator the whole project was written for. A tablature
 * program that plays a score is common; one where the guitar part goes through
 * an amplifier simulation of its own, the bass through a different one, and
 * both come out as separate stems is not. Everything before this -- a synth
 * per track, ports per track, stems -- was building the place to put it.
 *
 * **In-process with lilv, not delegated to Carla.** That was the architecture's
 * open question and this is the answer. Handing a chain to another process
 * means the audio leaving the callback and coming back: a second clock and a
 * buffer of latency per track, in a program whose ports are one node precisely
 * because there is one clock. Having argued that a node per part was wrong, a
 * process per chain would be the same mistake with more moving parts.
 *
 * **What runs in the callback.** `process` connects buffers and calls each
 * plugin's own run. It allocates nothing: every buffer a chain needs is sized
 * when it is built, for the largest block a graph will ever ask for.
 *
 * **Mono plugins are instantiated twice.** A great many guitar effects are one
 * in and one out, and a stereo track needs both sides treated. Two copies of a
 * mono plugin is what every host does and what a player would do with two
 * pedals; a plugin that is neither mono nor stereo is refused by name rather
 * than guessed at.
 */
namespace Lv2
{
/** One plugin as the world describes it, before anything is instantiated. */
struct Description {
    QString uri;
    QString name;
    int audioInputs = 0;
    int audioOutputs = 0;

    /** Whether this chain can use it: one in and one out, or two and two. */
    bool usable() const
    {
        return (audioInputs == 1 && audioOutputs == 1)
            || (audioInputs == 2 && audioOutputs == 2);
    }
};

/**
 * Every plugin installed, read once.
 *
 * Scanning the world is slow enough to be worth doing once and holding: lilv
 * parses every manifest on the machine, and there are a hundred and eighteen
 * bundles on an ordinary desktop.
 */
QList<Description> installed();

/** One plugin found by its URI, or a description with an empty uri. */
Description describe(const QString &uri);

class Chain
{
public:
    struct Options {
        int sampleRate = 48000;
        /** The largest block this will ever be asked for. */
        int maximumFrames = 8192;
    };

    /** Builds the chain in order; the first plugin is nearest the instrument. */
    Chain(const QStringList &uris, const Options &options);
    ~Chain();

    Chain(const Chain &) = delete;
    Chain &operator=(const Chain &) = delete;

    bool isValid() const;
    QString error() const;

    /** What actually loaded, in order. */
    QStringList loaded() const;

    /** Runs the chain over a block, in place. Safe in an audio callback. */
    void process(float *left, float *right, int frames);

private:
    struct Private;
    std::unique_ptr<Private> d;
};
}
