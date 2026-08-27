// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#pragma once

#include "noises.h"

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

    /**
     * The other kind of round-robin: a range of a random draw.
     *
     * A library picks a number between nought and one for each note and plays
     * whichever region's range contains it. Karoryfer's guitars are written
     * this way and a sampler that ignored it would play all five takes at
     * once, which is not five times louder so much as wrong -- the same
     * recording five times over is a comb filter.
     */
    double lowRandom = 0;
    double highRandom = 1;

    /**
     * A keyswitch: which articulation this region belongs to.
     *
     * Notes in the switch range do not sound; they choose which of the
     * regions above them answer. `switchLast` of -1 means the region is not
     * part of any articulation and always answers.
     */
    int switchLow = -1;
    int switchHigh = -1;
    int switchLast = -1;
    int switchDefault = -1;

    /** When a region fires: on the note, or on letting it go. */
    enum class Trigger {
        Attack,
        Release,
        First,
        Legato,
    };
    Trigger trigger = Trigger::Attack;

    /** How long after the note this region waits, in seconds. */
    double delay = 0;

    /** Frames into the recording to start and stop; -1 is to the end of it. */
    qint64 offset = 0;
    qint64 end = -1;

    bool loops = false;
    qint64 loopStart = 0;
    qint64 loopEnd = -1;

    /** How long the note takes to die after it is released, in seconds. */
    double release = 0.05;

    /**
     * How much quieter a release recording is for every second the note was
     * held, in decibels.
     *
     * The one opcode a release sample cannot do without. A string let go after
     * a beat still has most of its energy and lets go audibly; the same string
     * after eight bars has almost none, and a library that played the same
     * recording for both would put a click on the end of every held note.
     * Emily and Growlybass both say 7.
     */
    double releaseDecayDb = 0;

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

/** An instrument found on the machine, and where it is. */
struct Library {
    QString collection;         //< the library it belongs to, as a folder name
    QString name;               //< the programme within it
    QString path;
};

/**
 * Every `.sfz` under these directories, by the name a person would call it.
 *
 * Because choosing a sample library through a file dialog every time is
 * choosing it through a file dialog every time: the libraries somebody has are
 * in a handful of folders and the program can go and look. A library is
 * usually a repository with the instrument a directory or two down, so this
 * descends -- but only so far, because pointing it at a home directory should
 * take a moment rather than a morning.
 *
 * Named after the folder the file is in where that says more than the file
 * does: "Emilyguitar" beats "Emilyguitar/Emilyguitar", and a bare "sustain.sfz"
 * beside forty others tells nobody anything.
 */
QList<Library> found(const QStringList &roots, int maximumDepth = 5);

/**
 * Which of this instrument's keys are noises rather than notes.
 *
 * Discovered rather than configured, because a per-library table is a table
 * that is wrong the first time somebody installs a library nobody has seen.
 * Two things have to be true of a region before its key is called a noise:
 * its sample is filed under a name that says what it is -- `noises/`,
 * `scrape/`, `fingering1_rr3.wav` -- and its key sits above the highest note
 * the instrument can actually play. Either test alone is too weak. A library
 * with a `mute/` folder of palm-muted notes across the whole neck passes the
 * first and fails the second, which is the point of having the second.
 *
 * Only regions that fire on the attack are considered. A release recording is
 * a noise by any ordinary meaning of the word and is not one here: it is
 * asked for by letting a note go, never by playing a key, so it has no key to
 * put in a map.
 */
Noises::Map noises(const Instrument &instrument);

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
