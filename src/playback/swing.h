// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#pragma once

#include "score.h"

#include <QString>

/**
 * Where a written position is actually played, when the bar swings.
 *
 * A shuffle is a warp of time rather than a change to the notes. Every pair of
 * notes keeps the room it was written in; the line between them moves. So this
 * is one monotonic function from written position to played position, applied
 * to the start and the end of everything in the bar, and not a rule about
 * which notes count as a swung pair -- which is the version that goes wrong.
 *
 * Doing it as a warp gets the awkward cases for nothing. A crotchet on the
 * beat is untouched, because the warp is the identity at every pair boundary.
 * A note that begins halfway through the second quaver of a pair lands
 * somewhere sensible instead of falling outside the rule. A triplet written
 * inside a swung bar -- which happens, and is not a contradiction -- is moved
 * with everything else rather than being argued about.
 *
 * **The unit is a pair, not a beat.** Triplet8th swings pairs of quavers, so
 * its unit is a crotchet; Triplet16th swings pairs of semiquavers, so its unit
 * is a quaver. Inside the unit the first note gets `share` of it and the second
 * gets the rest: two thirds for a triplet feel, three quarters for a dotted
 * one, and one quarter for the Scotch snap, which is the same idea with the
 * short note first.
 */
namespace Swing
{
/** Whether anything happens at all. */
bool isSwung(TripletFeel feel);

/** How long a swung pair is, in quarters: a crotchet, or a quaver. */
Rational unitOf(TripletFeel feel);

/** How much of the pair the first of the two notes gets. */
Rational shareOf(TripletFeel feel);

/**
 * A position measured from the start of its bar, moved to where it is played.
 *
 * `barLength` is needed for the bars that do not hold a whole number of pairs
 * -- 7/8 with a quaver feel has three pairs and a quaver over. That last
 * quaver is left where it was written rather than stretched into a pair that
 * does not exist, which is also the only way the bar still ends where the next
 * one begins.
 */
Rational played(const Rational &within, TripletFeel feel, const Rational &barLength);

/** "triplet quavers", "snapped semiquavers" -- already translated. */
QString nameOf(TripletFeel feel);

/**
 * gpif's own spelling, which is also what a `.fw` writes.
 *
 * Kept rather than invented: a value in a saved file that reads the same as
 * the value in the file it was imported from is a value somebody can compare
 * without a table, and one table is one fewer thing to get out of step.
 */
QString tokenOf(TripletFeel feel);
TripletFeel fromToken(const QString &token);
}
