// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#pragma once

#include "key.h"
#include "score.h"

#include <QList>

#include <array>

/**
 * What key a piece is actually in, from the notes rather than from the page.
 *
 * These two are not the same question and in a tablature program they are
 * rarely the same answer. A key signature is something a transcriber has to go
 * out of their way to set and almost nobody does -- every score in the corpus
 * is sitting on Guitar Pro's default of no accidentals and major, and not one
 * of them is in C major. So the signature says nothing and the notes say
 * everything, which is the opposite of the situation printed music is in.
 *
 * The method is the standard one: how long each of the twelve pitch classes
 * sounds, correlated against a profile of what each key sounds like, best
 * match wins. Duration rather than note count, because a semibreve is not a
 * passing quaver and counting them the same is how a key gets decided by
 * ornaments.
 *
 * Everything here **describes and never corrects**. A note outside the key is
 * a decision somebody made and not a mistake; nothing in this file may refuse
 * an edit, mark a note wrong, or change anything. It is a reading of the
 * music, offered with the confidence it has and no more.
 */
namespace Analysis
{
/** How long each of the twelve pitch classes sounds, in quarters. */
using Weights = std::array<double, 12>;

/**
 * A run of beats in one voice of one track -- what a selection is.
 *
 * Bar and beat indices are the ones the cursor uses. It is deliberately not
 * `Editing::Range`: this is the model asking a question about the document,
 * and it should not need to know that an editor exists to ask it.
 */
struct Passage {
    int track = 0;
    int voice = 0;
    int firstBar = 0;
    int firstBeat = 0;
    int lastBar = 0;
    int lastBeat = 0;
};

/** One reading of the music, and how well it fits. */
struct Fit {
    Key::Signature key;

    /**
     * How well the music matches that key, from -1 to 1.
     *
     * A correlation and not a probability. It is worth comparing against the
     * other readings of the same passage and not worth reading as a percentage
     * of anything.
     */
    double fit = 0;
};

/** Every pitch in the score, weighed by how long it sounds. */
Weights weigh(const Score &score);

/** The same, over one run of beats. */
Weights weigh(const Score &score, const Passage &passage);

/**
 * Whether there is anything pitched in there at all.
 *
 * A passage of rests, or of nothing but a drum kit, has no key and no opinion
 * about one -- and every key fits it exactly as badly, so the answer must be
 * "nothing to say" rather than whichever key sorts first.
 */
bool isSilent(const Weights &weights);

/** All twenty-four keys, the best fitting first. */
QList<Fit> ranked(const Weights &weights);

/** The best fitting key, which is C major with a fit of nothing on silence. */
Fit best(const Weights &weights);

/**
 * Every note the analysis counts: pitched, and not on a drum kit.
 *
 * Here so that a caller wanting "how many of them" does not have to reproduce
 * the rules about what counts, and get them subtly different.
 */
QList<int> pitched(const Score &score);
QList<int> pitched(const Score &score, const Passage &passage);

/**
 * The notes that are not in the key, by id.
 *
 * The interesting half of an analysis, and the half that has to be handled
 * carefully: these are not errors. A chord borrowed from the parallel minor,
 * a chromatic passing note and a blues third are all "outside the key" and all
 * deliberate, which is why this counts them and says nothing about them.
 */
QList<int> outside(const Score &score, const Key::Signature &key);
QList<int> outside(const Score &score, const Passage &passage, const Key::Signature &key);

/**
 * The signature a key is written with, given its tonic and its mode.
 *
 * The inverse of [Key::tonicOf], and not quite a function: F sharp major and G
 * flat major are the same twelve notes spelled two ways, as are D sharp minor
 * and E flat minor. Where there is a choice this makes the one a musician
 * would -- the conventional spelling rather than the arithmetically neater
 * one -- from a table that can be checked against a music book.
 */
Key::Signature signatureFor(int tonicPitchClass, bool minor);
}
