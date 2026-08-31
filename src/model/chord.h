// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#pragma once

#include "fretboard.h"
#include "key.h"

#include <QList>
#include <QString>

/**
 * Chords: what a key is made of, and where they are on the neck.
 *
 * Two halves that are worth keeping apart. A chord as *harmony* is a root and
 * a quality -- C minor is three pitch classes and no more -- and that is what
 * a key's chords, a degree number and a chord's name are all about. A chord as
 * a *shape* is six strings with frets on some of them, which is a fact about
 * an instrument in a tuning with a capo on it, and the same C minor is a
 * different shape on every one.
 *
 * This is the layer that can put a wrong note in somebody's score, and it is
 * built last for that reason. What keeps it honest is that it only ever writes
 * when asked: nothing here inspects a score, judges it, or corrects it. A
 * chord outside the key is a decision and not a mistake.
 */
namespace Chord
{
/**
 * What kind of chord it is.
 *
 * The seven a key is actually built from, and no more. Ninths, elevenths,
 * suspensions and alterations are all real and all a different feature: this
 * one is "the chords of a key, and the ones next door", and a list that ran to
 * forty qualities would be a chord dictionary rather than a circle of fifths.
 */
enum class Quality {
    Major,
    Minor,
    Diminished,
    Augmented,
    Dominant7,
    Major7,
    Minor7,
    HalfDiminished7,
};

/** A chord as harmony: a root, and what is built on it. */
struct Named {
    int root = 0;               //< a pitch class, 0 to 11
    Quality quality = Quality::Major;
};

bool operator==(const Named &left, const Named &right);
bool operator!=(const Named &left, const Named &right);

/** The semitones above the root that make the quality. */
QList<int> intervalsOf(Quality quality);

/** The pitch classes it is made of, root first. */
QList<int> pitchClassesOf(const Named &chord);

/**
 * "Cm", "G7", "B°" -- the name, spelled as the key spells it.
 *
 * The key is what decides between A flat and G sharp, which is why it is asked
 * for: the same chord written in two keys is two different names for one
 * sound, and picking one without being told is how a flat key ends up full of
 * sharps.
 */
QString nameOf(const Named &chord, const Key::Signature &key);

/**
 * "I", "ii", "V" -- which degree of the key it is, or empty where it is none.
 *
 * Case carries the quality, as it has in every harmony book for two hundred
 * years: upper for major, lower for minor, and a ring after a diminished one.
 */
QString degreeOf(const Named &chord, const Key::Signature &key);

/** The seven chords the key is built from, the tonic first. */
QList<Named> diatonic(const Key::Signature &key);

/**
 * The ones borrowed from the parallel key -- the same tonic, the other mode.
 *
 * Where most of the chords that are "outside the key" in real music actually
 * come from, and the reason this is offered rather than left to be typed: a
 * flat sixth in a major key is a normal thing to want and an odd thing to work
 * out from a circle.
 */
QList<Named> borrowed(const Key::Signature &key);

/**
 * The chord as a shape, on this instrument, under a hand at this fret.
 *
 * The rule is the one a guitarist uses without saying it: find the lowest
 * string that can sound the *root* under the hand, and from there take the
 * lowest chord tone in reach on every string above it. Strings below the root
 * are left out, because a chord whose lowest note is its third is a different
 * chord and not the one that was asked for.
 *
 * That rule is not a heuristic dressed up -- it produces the shapes that are
 * actually in the books. A C major with the hand at the nut comes out x32010,
 * G major 320003, A minor x02210, and an F with the hand at the first fret
 * comes out 133211, which is the barre.
 *
 * Empty where the chord cannot be played there at all: no string can sound the
 * root under that hand, or too few of its notes are in reach to be the chord.
 */
QList<Fretboard::Position> shapeOn(const Fretboard::Instrument &instrument, const Named &chord,
                                   const Fretboard::Hand &hand);

/**
 * The first fret at which the chord can be played whole, from the nut up.
 *
 * What "give me a C major" means when nobody has said where: the shape nearest
 * the nut, which is the one a player already knows.
 */
QList<Fretboard::Position> shapeOf(const Fretboard::Instrument &instrument, const Named &chord);

/**
 * The shape to use when a hand is already somewhere.
 *
 * A chord asked for while working at the seventh fret should not arrive at the
 * nut. Where the hand is nowhere in particular -- nothing under the caret, or
 * an open string, which says nothing about where a hand is -- this is
 * `shapeOf`, the shape nearest the nut. Where the chord cannot be held under
 * that hand at all it is `shapeOf` too, because a chord somewhere is worth
 * more than no chord in the right place.
 */
QList<Fretboard::Position> shapeNear(const Fretboard::Instrument &instrument, const Named &chord,
                                     int handFret);
}
