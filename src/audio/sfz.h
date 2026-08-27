// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#pragma once

#include <QList>
#include <QString>

/**
 * SFZ: a text file that says which recording to play for which note.
 *
 * This is what a guitar that sounds like a guitar is made of. A General MIDI
 * patch has one recording of a nylon string stretched across the neck; a
 * sampled instrument has a recording per fret, several per fret at different
 * strengths, and several of *those* so that the same note twice is not the
 * identical waveform twice -- which is the thing an ear notices immediately
 * and cannot name.
 *
 * **A subset, deliberately.** SFZ has several hundred opcodes and describes
 * synthesisers as well as samplers. What is here is what a plucked-string
 * library uses: where the sample is, which notes and strengths it answers to,
 * how to pitch it, how loud, how wide, where it loops, and which of a run of
 * takes it is. Anything else is skipped rather than refused -- a library that
 * would not load because it mentioned a filter cutoff would be a library
 * nobody could use, and the ignored opcode does no harm.
 *
 * Nothing here reads audio or makes any. It turns a file into a list of
 * regions; `Sampler` is what plays them.
 */
namespace Sfz
{
/** One recording, and the circumstances it answers to. */
struct Region {
    QString sample;             //< an absolute path, resolved against the file

    int lowKey = 0;
    int highKey = 127;
    int lowVelocity = 1;
    int highVelocity = 127;

    /** The note the recording was made at, which is what pitching works from. */
    int keyCentre = 60;
    int tuneCents = 0;

    double volumeDb = 0;
    /** -100 hard left to 100 hard right. */
    double pan = 0;

    /**
     * Which of a run of takes this is, and how many there are.
     *
     * The round-robin: four regions with `seq_length=4` and positions one to
     * four are four recordings of the same note, played in turn. It is the
     * cheapest thing in sampling that stops a repeated note sounding like a
     * machine, and the reason this format is worth reading at all.
     */
    int sequenceLength = 1;
    int sequencePosition = 1;

    /** Frames into the recording to start and stop; -1 is to the end of it. */
    qint64 offset = 0;
    qint64 end = -1;

    bool loops = false;
    qint64 loopStart = 0;
    qint64 loopEnd = -1;

    /** How long the note takes to die after it is released, in seconds. */
    double release = 0.05;

    /**
     * A region in a group can silence the others in it.
     *
     * What a guitar needs it for: two notes on one string cannot sound at
     * once, and a library says so by putting a string's regions in a group
     * that turns itself off.
     */
    int group = 0;
    int offBy = 0;
};

struct Instrument {
    QList<Region> regions;

    bool isEmpty() const
    {
        return regions.isEmpty();
    }
};

/** Reads an `.sfz`. Empty, with `error` set, where it cannot. */
Instrument read(const QString &path, QString *error = nullptr);

/**
 * Parses text already in hand, resolving samples against `directory`.
 *
 * Separate from `read` so that the parser can be tested without a file, and so
 * that a malformed instrument in a bug report can be pasted into a test.
 */
Instrument parse(const QString &text, const QString &directory, QString *error = nullptr);
}
