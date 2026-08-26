// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#pragma once

#include "score.h"

#include <QList>
#include <QString>

/**
 * A tuner that knows what the score is in.
 *
 * This is the part no standalone tuner has, and the only reason to write
 * another tuner at all. A chromatic tuner tells you which note is nearest to
 * whatever you played, and leaves you to know that this piece is in drop C and
 * that the fifth string therefore wants to be a G rather than an A. Fretwork
 * already has the tuning written down -- it read it out of the file and prints
 * it at the top of every page -- so it can say "that is the fifth string, and
 * it is twelve cents flat" instead.
 *
 * The capo is included in the target pitch, because the target is what the
 * string will sound when it is plucked, and a capo at the second fret makes
 * that two semitones higher. A guitarist who wants the open pitches takes the
 * capo off, at which point the score's own capo is not what they are doing.
 *
 * Everything here is arithmetic on a frequency. Nothing in this file listens
 * to anything: `Pitch::Detector` hears, `AudioInput` records, and this decides
 * what the number means.
 */
namespace Tuner
{
/** A440, and the only assumption in here worth arguing about. */
inline constexpr double ConcertA = 440.0;

double hertzForMidi(double midi, double concertA = ConcertA);
double midiForHertz(double hertz, double concertA = ConcertA);

/** How far `hertz` is from `reference`, in cents. Positive is sharp. */
double cents(double hertz, double reference);

/** One string, and what it should sound. */
struct StringTarget {
    int index = -1;         //< which string, counting from the lowest at 0
    int midi = 0;           //< the pitch it should sound, capo included
    double hertz = 0;
    QString name;           //< "E2"
};

/**
 * The strings of a track, lowest first.
 *
 * Empty for a drum kit, which has no strings and no business in a tuner, and
 * for any track whose tuning the importer could not find.
 */
QList<StringTarget> targetsFor(const Track &track, double concertA = ConcertA);

/** EADGBE, for a tuner opened with no score in front of it. */
QList<StringTarget> standardGuitar(double concertA = ConcertA);

/** What was heard, and what to do about it. */
struct Reading {
    bool heard = false;
    double hertz = 0;
    double clarity = 0;

    /**
     * The nearest string, or -1 when what was played is too far from any of
     * them to be one of them.
     *
     * Too far is deliberately not "the nearest one wins". A guitar being tuned
     * from nothing can be a whole tone out, and a tuner that silently decided
     * a very flat B was a slightly sharp G would send somebody the wrong way
     * with confidence. The window is half the smallest gap between two
     * targets, so it can never reach far enough to be ambiguous -- and where
     * two strings are further apart than twice that, there is room between
     * them to be near neither, which is answered by naming the note and
     * leaving the choice alone.
     */
    int string = -1;
    double cents = 0;       //< from that string's target; positive is sharp

    /** The nearest chromatic note, always filled in, so there is always an answer. */
    int nearestMidi = 0;
    double nearestCents = 0;
    QString noteName;
};

/**
 * How far from a target still counts as that string, in cents.
 *
 * Half the smallest gap between two targets, so the answer can never be
 * ambiguous, and never more than 200 cents however oddly a score is tuned --
 * a string two whole tones out is a broken instrument rather than a tuning.
 */
double acceptanceCents(const QList<StringTarget> &targets);

/** What a detected frequency means against those targets. */
Reading read(double hertz, double clarity, const QList<StringTarget> &targets,
             double concertA = ConcertA);

/** "in tune", "flat", "very sharp" -- for a status line, already translated. */
QString describe(const Reading &reading);

/** Within this many cents, a string is in tune and the arrow stops moving. */
inline constexpr double InTuneCents = 3.0;
}
