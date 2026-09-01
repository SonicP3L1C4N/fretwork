// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#pragma once

#include "score.h"

#include <QByteArray>

/**
 * The score as MusicXML: the answer to "can I have this file in something
 * else".
 *
 * It is the only interchange format for music with no vendor behind it, which
 * is the whole argument for it. This program refuses to write `.gp` -- reading
 * a format is one risk and handing somebody a file to open in another
 * company's program is a different one -- and that refusal is only honest if
 * there is some other way out. This is it.
 *
 * Unlike every other format this project touches, MusicXML has a published
 * specification, and that changes the work rather than merely making it
 * easier. Nothing here was measured off a sample file, because nothing had to
 * be: the element names, their order and their meanings are written down.
 * Where this file makes a choice the specification allows several of, the
 * comment says so, and where it cannot say something the specification has a
 * place for, it leaves the place empty rather than filling it.
 *
 * **Out, not in.** Reading MusicXML is a different program: an importer has to
 * survive everything anybody has ever emitted, and this has to satisfy one
 * schema.
 *
 * What makes it cheap is that the hard half was already built. A MusicXML note
 * is a `<step>`, an `<alter>` and an `<octave>` -- a letter, an accidental and
 * a register -- and deciding that a pitch is an F sharp rather than a G flat
 * is not something a MIDI number can answer. `Key::spell` answers it, and was
 * written for the harmony work with no thought of this.
 */
namespace Musicxml
{
/**
 * The whole score, every part, as a `score-partwise` document.
 *
 * Notated rather than played: repeats are written as repeat barlines and left
 * for the reader to take, because this is a document and not a performance.
 * That is the opposite of the choice a practice pack makes, and for the
 * opposite reason.
 */
QByteArray documentFor(const Score &score);

/**
 * How many divisions to a crotchet, for a score.
 *
 * MusicXML counts durations in whole numbers of a unit the file chooses, so
 * the unit has to be small enough that every duration in the score is a whole
 * number of them. That is the lowest common multiple of what the rhythms
 * actually use -- computed rather than guessed at, because a fixed number
 * either wastes precision on simple music or quietly rounds a quintuplet.
 */
int divisionsFor(const Score &score);
}
