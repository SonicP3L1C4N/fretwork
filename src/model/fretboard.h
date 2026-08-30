// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#pragma once

#include <QList>

/**
 * Where on the neck a pitch is played.
 *
 * The score stores every note three times over -- the sounding pitch, the
 * string and the fret -- and the arithmetic between them is an identity the
 * whole program holds to: the open string, plus the capo, plus the fret, is
 * the note that sounds. Read in that direction it is one addition. Read in the
 * other it is not arithmetic at all, because the same pitch is available in up
 * to six places and nothing in the addition says which one a person would use.
 *
 * That is what this is for. It is a preference with a hand attached: the right
 * place for a note depends on where the hand already is, on what else is
 * sounding at the same moment, and on whether the phrase around it is the kind
 * that uses open strings. Three things want it -- entering notes from a MIDI
 * keyboard, which has no strings on it; inserting a chord by name, which is a
 * shape rather than a list of pitches; and writing out a file whose notes
 * arrived as pitches -- and each of them would otherwise grow a private half
 * version of the same judgement.
 *
 * It has no dependency on the score, deliberately. What it needs is a tuning,
 * a capo and a neck, and a caller with a Track has all three to hand.
 */
namespace Fretboard
{
/** A hand that has not been anywhere yet, and a position that is not one. */
constexpr int Unknown = -1;

/**
 * The instrument the question is being asked about.
 *
 * `frets` is how many the neck has, and it matters: whether a pitch is
 * reachable at all is a question about the instrument in the room rather than
 * about music. The default is the most a common instrument has, so that a
 * caller who does not know is told about more places rather than fewer; one
 * who does know should say, because a classical guitar stops at nineteen.
 */
struct Instrument {
    QList<int> tuning;      //< MIDI pitch per string, lowest string first
    int capo = 0;
    int frets = 24;
};

/** One place on the neck: which string, and which fret counted from the capo. */
struct Position {
    int string = Unknown;
    int fret = Unknown;

    bool isValid() const
    {
        return string >= 0 && fret >= 0;
    }
};

bool operator==(const Position &left, const Position &right);
bool operator!=(const Position &left, const Position &right);

/**
 * Where the hand is, and what the music around it is doing.
 *
 * `fret` is where the index finger sits, and `span` is how far the hand
 * reaches without moving -- so a hand at 5 with a span of 4 covers frets 5 to
 * 8, and an open string, which needs no hand at all, is always within reach.
 * `Unknown` is the honest state before the first note: nothing has happened
 * yet, so nothing is near or far.
 *
 * `openStrings` is a statement about the phrase rather than about the hand. A
 * run at the twelfth fret that drops to an open string is a break in the
 * sound, not a convenience, and the solver should climb some way to avoid one
 * -- but only where the music is not already using them, which is a thing the
 * caller knows and this cannot work out from one note.
 */
struct Hand {
    int fret = Unknown;
    int span = 4;
    bool openStrings = true;

    /**
     * The hand having played that note: it stays where it is if the note was
     * within reach or was an open string, and otherwise moves the least it can
     * to cover it.
     */
    Hand after(const Position &played) const;
};

/** The identity, read forwards: the open string, the capo and the fret. */
int pitchAt(const Instrument &instrument, int string, int fret);

/**
 * The fret that sounds a pitch on a given string, which is the identity read
 * backwards once the string has been chosen.
 *
 * It answers arithmetically and leaves to the caller the judgement about
 * whether that is a fret anybody can play: past `frets` it is above the neck,
 * and negative it is below the capo. A string the instrument has not got
 * answers negative too, since the useful reading of a negative answer is "no
 * fret here" rather than a number -- how negative it is means nothing.
 */
int fretFor(const Instrument &instrument, int string, int midi);

/**
 * Every place the pitch can be played, lowest string first.
 *
 * Empty where the instrument cannot sound it at all -- and for a drum kit,
 * which has no tuning, because the question does not apply to one.
 */
QList<Position> candidates(const Instrument &instrument, int midi);

/**
 * The best place for one pitch, given a hand and the strings already sounding
 * at this moment.
 *
 * Causal: it knows what has been played and nothing about what comes next,
 * which is the only thing it can know when the notes are arriving from a
 * keyboard one key at a time. Where a whole phrase is in hand already, use
 * phrase() instead -- it will often answer differently and better.
 *
 * An invalid Position where the pitch is out of reach or every string that
 * could sound it is taken.
 */
Position choose(const Instrument &instrument, int midi, const Hand &hand = {}, const QList<int> &occupied = {});

/**
 * A shape for several pitches sounding at once: one string each, all of the
 * fretted ones within the span of one hand.
 *
 * The answer is in the order the pitches were given. Refused whole -- an empty
 * list -- where any pitch is out of reach, where there are more notes than
 * strings, or where no assignment fits under one hand, because a chord with a
 * note missing is a different chord and not a partial answer to this one.
 */
QList<Position> chord(const Instrument &instrument, const QList<int> &pitches, const Hand &hand = {});

/**
 * A fingering for a run of single notes, chosen over the whole line at once.
 *
 * The difference from calling choose() repeatedly is lookahead, and it is not
 * a small one: the cheapest place for the first note is often the wrong one,
 * because it leaves the hand somewhere the second note is not. This searches
 * every path through the line and keeps the cheapest, which is what a player
 * does when they read a phrase before playing it.
 *
 * Refused whole where any pitch is out of reach, on the same grounds the
 * editor refuses a transposition that would run off the neck: a phrase with
 * one note left behind is not the phrase that was asked for.
 */
QList<Position> phrase(const Instrument &instrument, const QList<int> &pitches, const Hand &hand = {});
}
