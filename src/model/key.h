// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#pragma once

#include <QList>
#include <QString>

/**
 * How a pitch is written down.
 *
 * The score knows that a note is MIDI 63. It does not know whether that is a
 * D sharp or an E flat, which is fine for playing it and is the whole problem
 * for writing it down: the two are the same sound and different notes, and
 * nothing about the sound decides between them. What decides is the key.
 *
 * So this is the layer under standard notation -- an accidental cannot be
 * drawn until something has said which one -- and under any question of the
 * form "is this note in the key", which is what analysis and a scale overlay
 * are both made of. It is deliberately the smallest thing that answers those:
 * a signature, a spelling, and the rule between them.
 *
 * It is not [NoteName], and the two are worth keeping apart. NoteName answers
 * "what is this pitch called with nothing at all to go on", which is the
 * question a tuner asks about a string, and it answers in sharps because a
 * string has no key for it to consult. This answers "how is this note written
 * in this key", which is a different question with a different input, and the
 * two disagreeing about MIDI 63 is them being right about different things.
 *
 * The theory here describes and never corrects. A note outside the key is a
 * decision somebody made rather than a mistake, and nothing in this file may
 * ever refuse an edit or change a note.
 */
namespace Key
{
/**
 * A key signature as it is written at the head of a staff: how many sharps or
 * flats, and whether the key is major or minor.
 *
 * `accidentals` is signed -- positive sharps, negative flats -- which is the
 * shape gpif stores and the shape the circle of fifths has. The default is no
 * accidentals and major, which is C major, and is also what "no key signature
 * has been said" means: the two are the same thing on paper and there is no
 * point in the model pretending otherwise.
 *
 * `minor` changes what the key is *called* and nothing about how notes in it
 * are spelled, because the signature alone settles that -- B major and G sharp
 * minor are five sharps either way, and a D sharp is a D sharp in both.
 */
struct Signature {
    int accidentals = 0;
    bool minor = false;
};

bool operator==(const Signature &left, const Signature &right);
bool operator!=(const Signature &left, const Signature &right);

/**
 * One note as it is written: a letter, an accidental, and an octave.
 *
 * `step` is 0 to 6 for C D E F G A B -- the line or space it sits on, which is
 * the part an accidental cannot change. `alteration` is in semitones, so 1 is
 * a sharp and -2 a double flat.
 *
 * The octave belongs to the letter and not to the pitch, which is why B sharp
 * and C are not in the same one: MIDI 60 spelled as a C is C4, and spelled as
 * a B sharp is B♯3, because that is the B its accidental was applied to.
 */
struct Spelling {
    int step = 0;
    int alteration = 0;
    int octave = 4;
};

bool operator==(const Spelling &left, const Spelling &right);
bool operator!=(const Spelling &left, const Spelling &right);

/** Whether a signature is one anybody writes: seven sharps to seven flats. */
bool isValid(const Signature &signature);

/** The pitch a spelling sounds, which is the direction that never needs a key. */
int midiOf(const Spelling &spelling);

/**
 * How a pitch is written down in a key.
 *
 * Diatonic notes are spelled as the signature spells them, which is the whole
 * of the rule where the music stays in the key: MIDI 63 is the D sharp of B
 * major and the E flat of B flat minor, and neither needed anything but the
 * five accidentals at the head of the staff.
 *
 * A note outside the key has no such answer, so the rule is the plainest one
 * that does not produce nonsense: the smallest accidental that reaches it, and
 * where a sharp and a flat are equally small, the one the key is already
 * written in. That gives F sharp in C major and G flat in F major, and it
 * gives a plain D in C sharp major rather than the C double sharp that
 * following the key's direction alone would have written.
 *
 * It is a default and not an analysis. Which of two spellings a chromatic note
 * actually wants depends on what it is doing -- a raised fourth and a flat
 * fifth are the same key and the same pitch -- and that is a question for a
 * layer that can see the music around it.
 */
Spelling spell(int midi, const Signature &signature = {});

/** Whether a pitch is one of the seven the key is made of. */
bool isDiatonic(int midi, const Signature &signature);

/**
 * The seven notes of the key, from its tonic upwards, spelled as it spells
 * them -- which is a scale, and is not the same as seven pitches.
 *
 * The octave on each is not meaningful; what matters is the letter, its
 * accidental, and the order. Asking `spell()` for each letter in turn does not
 * answer this: the third degree of C minor is an E flat, and the pitch of a
 * natural E spells as a natural E in any key that does not contain it.
 */
QList<Spelling> scaleOf(const Signature &signature);

/**
 * "D♯", "B♭", "F" -- the letter and its accidental.
 *
 * Sharps and flats are the Unicode signs rather than `#` and `b`, because this
 * is the layer that exists to be written down. Doubles are the sign twice: the
 * proper double sharp is a character most fonts on a Linux desktop have never
 * heard of, and a name nobody can read is worse than one that is merely plain.
 *
 * A natural note is its letter alone. Whether a natural *sign* needs drawing
 * is a question about the bar it is in and the signature it is cancelling, and
 * so belongs to whatever is drawing the staff rather than here.
 */
QString nameOf(const Spelling &spelling);

/** "D♯4" -- the name with the octave it is in. */
QString withOctave(const Spelling &spelling);

/** "B major", "B♭ minor", "C major" -- what the signature is called. */
QString nameOf(const Signature &signature);

/** The note the key is named after, spelled as the key spells it. */
Spelling tonicOf(const Signature &signature);

/**
 * The signature a key is written with, given its tonic and its mode.
 *
 * The inverse of [tonicOf], and not quite a function: F sharp major and G flat
 * major are the same twelve notes spelled two ways, as are D sharp minor and E
 * flat minor. Where there is a choice this makes the one a musician would --
 * the conventional spelling rather than the arithmetically neater one -- from
 * a table that can be checked against a music book.
 */
Signature signatureFor(int tonicPitchClass, bool minor);
}
