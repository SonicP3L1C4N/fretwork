// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "harmonic.h"

#include <QTest>

#include <cmath>

/**
 * The harmonic arithmetic, checked against physics rather than against itself.
 *
 * The numbers here are not taken from the implementation. A partial n sounds
 * 12*log2(n) semitones above the open string and has nodes at 12*log2(n/(n-k))
 * frets along it, and both of those are properties of a vibrating string that
 * were true long before Guitar Pro existed. So the expectations below are
 * written out as the notes a guitarist would name -- the twelfth fret gives
 * the octave, the seventh gives an octave and a fifth, the fifth gives two
 * octaves -- and the file format only appears where a real transcription's
 * values are checked at the end.
 */
class HarmonicTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    /**
     * The three harmonics every guitarist can find without thinking, at the
     * frets they are actually at.
     */
    void findsThePartialsEverybodyKnows()
    {
        QCOMPARE(Harmonic::partialAt(12.0), 2);   // the octave
        QCOMPARE(Harmonic::partialAt(7.0), 3);    // an octave and a fifth
        QCOMPARE(Harmonic::partialAt(5.0), 4);    // two octaves

        // And the same partials again, from their other nodes further up.
        QCOMPARE(Harmonic::partialAt(19.0), 3);
        QCOMPARE(Harmonic::partialAt(24.0), 4);
    }

    /**
     * The twelfth fret is a node for every even partial. It has to answer 2,
     * because the octave is what a finger there produces -- a string does not
     * pick the fourth partial when the second is available to it.
     */
    void answersWithTheLowestPartialSharingANode()
    {
        QCOMPARE(Harmonic::partialAt(12.0), 2);
        QCOMPARE(Harmonic::partialAt(7.0), 3);    // also a node for 6, 9, 12
        QCOMPARE(Harmonic::partialAt(5.0), 4);    // also a node for 8, 12, 16
    }

    /** Nothing is a node at or before the nut, and nothing is behind it. */
    void refusesPositionsThatAreNotOnTheString()
    {
        QCOMPARE(Harmonic::partialAt(0.0), 0);
        QCOMPARE(Harmonic::partialAt(-3.0), 0);
        QCOMPARE(Harmonic::offsetAbove(0.0), 0.0);
    }

    /**
     * The offsets are not whole semitones and are not rounded to them here.
     *
     * The seventh partial is 33.69 semitones up, which is 31 cents flat of a
     * minor seventh; the third is 19.02, two cents sharp of an octave and a
     * fifth. Rounding belongs where a MIDI note number is finally wanted, and
     * a MIDI note number is a lossy way to write a harmonic down.
     */
    void keepsTheOffsetsUnrounded()
    {
        QVERIFY(std::abs(Harmonic::offsetAbove(12.0) - 12.0) < 0.001);
        QVERIFY(std::abs(Harmonic::offsetAbove(7.0) - 19.0196) < 0.001);
        QVERIFY(std::abs(Harmonic::offsetAbove(5.0) - 24.0) < 0.001);
        QVERIFY(std::abs(Harmonic::offsetAbove(5.8) - 33.6883) < 0.001);

        // Not a whole number, which is the point of the previous line.
        QVERIFY(std::abs(Harmonic::offsetAbove(5.8) - 34.0) > 0.3);
    }

    /**
     * A natural harmonic ignores the fretting hand; every other kind is
     * measured from it. This is the distinction the whole file turns on.
     */
    void measuresNaturalHarmonicsFromTheOpenStringAndTheRestFromTheFret()
    {
        const int lowE = 40;

        // Touched at the twelfth: the octave above the open string, whatever
        // the tab happens to say the fret is.
        QCOMPARE(Harmonic::sounding(Harmonic::Type::Natural, lowE, 12, 12.0), 52);
        QCOMPARE(Harmonic::sounding(Harmonic::Type::Natural, lowE, 0, 12.0), 52);

        // The same node on a note held at the ninth fret, pinched rather than
        // touched open: an octave above the note under the finger, not above
        // the string.
        QCOMPARE(Harmonic::sounding(Harmonic::Type::Semi, lowE, 9, 12.0), 61);
        QCOMPARE(Harmonic::sounding(Harmonic::Type::Pinch, lowE, 9, 12.0), 61);
    }

    /** With no harmonic on it, a note sounds where it is fretted. */
    void leavesAnOrdinaryNoteAlone()
    {
        QCOMPARE(Harmonic::sounding(Harmonic::Type::None, 40, 7, 0.0), 47);
        QCOMPARE(Harmonic::sounding(Harmonic::Type::None, 40, 7, 12.0), 47);
    }

    /**
     * A harmonic whose node names no partial is played where it is fretted
     * rather than dropped. A note in the wrong register is still a note; a
     * silence is a hole in the score.
     */
    void fallsBackToTheFrettedPitchRatherThanToSilence()
    {
        QCOMPARE(Harmonic::sounding(Harmonic::Type::Natural, 40, 7, 0.0), 47);
        QCOMPARE(Harmonic::sounding(Harmonic::Type::Natural, 40, 7, -1.0), 47);
    }

    /**
     * The twelve harmonics that exist in the corpus, worked out by hand.
     *
     * These are the values that settled the question the roadmap had been
     * carrying: seven natural harmonics in Sharp Dressed Man at three
     * different nodes, and five marked `semi` in Twilight Of The Thunder God.
     * They are written here as literals so that a change to the arithmetic has
     * to argue with a real transcription rather than with a fixture.
     *
     * Sharp Dressed Man's are the exotic ones -- the seventh and eighth
     * partials, two and three octaves up -- which is exactly why getting this
     * wrong would not have been subtle.
     */
    void agreesWithTheHarmonicsInTheCorpus()
    {
        // string, fret, node, expected -- Sharp Dressed Man, all natural.
        struct Case { int open; int fret; double node; int sounds; };
        const QList<Case> natural = {
            {55, 10, 9.6, 89},   // G3, seventh partial:  F6, 31 cents flat
            {55,  8, 8.2, 91},   // G3, eighth partial:   G6, three octaves
            {50,  8, 8.2, 86},   // D3, eighth partial:   D6
            {45,  6, 5.8, 79},   // A2, seventh partial:  G5, 31 cents flat
            {40,  8, 8.2, 76},   // E2, eighth partial:   E5
        };
        for (const Case &c : natural) {
            QCOMPARE(Harmonic::sounding(Harmonic::Type::Natural, c.open, c.fret, c.node),
                     c.sounds);
        }

        // Twilight Of The Thunder God, all `semi`, all at the twelfth: an
        // octave above the fretted note, which is what a squeal is.
        const QList<Case> semi = {
            {59,  9, 12.0, 80},  // B3 string, ninth fret
            {54, 10, 12.0, 76},
            {50,  9, 12.0, 71},
            {45, 11, 12.0, 68},
            {59,  7, 12.0, 78},
        };
        for (const Case &c : semi) {
            QCOMPARE(Harmonic::sounding(Harmonic::Type::Semi, c.open, c.fret, c.node),
                     c.sounds);
        }
    }

    /** The names gpif uses, both ways, and an unknown one reading as none. */
    void readsAndWritesTheNames()
    {
        QCOMPARE(Harmonic::typeFrom(QStringLiteral("natural")), Harmonic::Type::Natural);
        QCOMPARE(Harmonic::typeFrom(QStringLiteral("semi")), Harmonic::Type::Semi);
        QCOMPARE(Harmonic::typeFrom(QStringLiteral("Pinch")), Harmonic::Type::Pinch);
        QCOMPARE(Harmonic::typeFrom(QStringLiteral("nonsense")), Harmonic::Type::None);
        QCOMPARE(Harmonic::typeFrom(QString()), Harmonic::Type::None);

        QCOMPARE(Harmonic::nameOf(Harmonic::Type::Natural), QStringLiteral("natural"));
        QCOMPARE(Harmonic::nameOf(Harmonic::Type::Feedback), QStringLiteral("feedback"));
        QVERIFY(Harmonic::nameOf(Harmonic::Type::None).isEmpty());
    }
};

QTEST_GUILESS_MAIN(HarmonicTest)
#include "harmonictest.moc"
