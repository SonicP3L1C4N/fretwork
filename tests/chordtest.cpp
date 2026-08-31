// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "chord.h"

#include <QTest>

/**
 * The chords of a key, and where they are on the neck.
 *
 * The shape tests are the ones that matter and the ones a reader can check
 * without trusting a word of this: a C major comes out x32010, a G 320003, an
 * A minor x02210, and an F under a hand at the first fret comes out 133211,
 * which is the barre. Those are the shapes in every chord book ever printed.
 * If the rule produced anything else, no argument about how principled it is
 * would be worth reading.
 */
class ChordTest : public QObject
{
    Q_OBJECT

private:
    static Fretboard::Instrument guitar(int capo = 0)
    {
        return Fretboard::Instrument{{40, 45, 50, 55, 59, 64}, capo, 24};
    }

    /**
     * A shape written the way a chord book writes one: a fret per string from
     * the lowest, and `x` for a string that is not played.
     *
     * Every chord tested here is in the first four frets, so the digits are
     * unambiguous without separators -- which is the only reason this reads
     * like the thing it is being compared against.
     */
    static QString written(const QList<Fretboard::Position> &shape, int strings = 6)
    {
        QString out;
        for (int string = 0; string < strings; ++string) {
            int fret = -1;
            for (const Fretboard::Position &at : shape) {
                if (at.string == string) {
                    fret = at.fret;
                }
            }
            out += fret < 0 ? QStringLiteral("x") : QString::number(fret);
        }
        return out;
    }

    static QStringList named(const QList<Chord::Named> &chords, const Key::Signature &key)
    {
        QStringList out;
        for (const Chord::Named &chord : chords) {
            out.append(Chord::nameOf(chord, key));
        }
        return out;
    }

private Q_SLOTS:
    void aKeyIsSevenChords()
    {
        const Key::Signature major{0, false};
        QCOMPARE(named(Chord::diatonic(major), major),
                 QStringList({QStringLiteral("C"), QStringLiteral("Dm"), QStringLiteral("Em"),
                              QStringLiteral("F"), QStringLiteral("G"), QStringLiteral("Am"),
                              QStringLiteral("B°")}));

        // The relative minor is the same seven chords starting in a different
        // place, which is the whole of what "relative" means.
        const Key::Signature minor{0, true};
        QCOMPARE(named(Chord::diatonic(minor), minor),
                 QStringList({QStringLiteral("Am"), QStringLiteral("B°"), QStringLiteral("C"),
                              QStringLiteral("Dm"), QStringLiteral("Em"), QStringLiteral("F"),
                              QStringLiteral("G")}));
    }

    void aChordIsSpelledTheWayItsKeySpellsIt()
    {
        // The same sound, and two names for it, decided by the key it is being
        // read in and by nothing else.
        const Chord::Named chord{10, Chord::Quality::Major};
        QCOMPARE(Chord::nameOf(chord, Key::Signature{-1, false}), QStringLiteral("B♭"));
        QCOMPARE(Chord::nameOf(chord, Key::Signature{5, false}), QStringLiteral("A♯"));
    }

    void degreesAreWrittenAsHarmonyBooksWriteThem()
    {
        const Key::Signature key{0, false};
        QStringList degrees;
        for (const Chord::Named &chord : Chord::diatonic(key)) {
            degrees.append(Chord::degreeOf(chord, key));
        }
        QCOMPARE(degrees,
                 QStringList({QStringLiteral("I"), QStringLiteral("ii"), QStringLiteral("iii"),
                              QStringLiteral("IV"), QStringLiteral("V"), QStringLiteral("vi"),
                              QStringLiteral("vii°")}));

        // A chord from outside the key has no degree in it, which is a fact
        // about the chord and not a complaint about it.
        QVERIFY(Chord::degreeOf(Chord::Named{1, Chord::Quality::Major}, key).isEmpty());
    }

    void whatIsBorrowedComesFromTheParallelKey()
    {
        // In C major, the chords of C minor that are not already there -- and
        // spelled in C minor's own three flats, so the flat sixth is the A
        // flat it is rather than a G sharp.
        const Key::Signature key{0, false};
        const QStringList borrowed = named(Chord::borrowed(key), Key::Signature{-3, true});
        QVERIFY2(borrowed.contains(QStringLiteral("A♭")), qPrintable(borrowed.join(u' ')));
        QVERIFY2(borrowed.contains(QStringLiteral("Fm")), qPrintable(borrowed.join(u' ')));
        QVERIFY2(borrowed.contains(QStringLiteral("Cm")), qPrintable(borrowed.join(u' ')));
        // And nothing already in C major is offered as a borrowing of it.
        QVERIFY(!borrowed.contains(QStringLiteral("F")));
    }

    void theShapesAreTheOnesInTheBooks()
    {
        const Fretboard::Hand nut{0, 4, true};
        QCOMPARE(written(Chord::shapeOn(guitar(), {0, Chord::Quality::Major}, nut)),
                 QStringLiteral("x32010"));
        QCOMPARE(written(Chord::shapeOn(guitar(), {7, Chord::Quality::Major}, nut)),
                 QStringLiteral("320003"));
        QCOMPARE(written(Chord::shapeOn(guitar(), {9, Chord::Quality::Minor}, nut)),
                 QStringLiteral("x02210"));
        QCOMPARE(written(Chord::shapeOn(guitar(), {4, Chord::Quality::Major}, nut)),
                 QStringLiteral("022100"));

        // And the barre, which is the case that proves an open string is not
        // free to a hand that is lying across the neck.
        const Fretboard::Hand first{1, 4, true};
        QCOMPARE(written(Chord::shapeOn(guitar(), {5, Chord::Quality::Major}, first)),
                 QStringLiteral("133211"));
    }

    void askedForNowhereInParticularItStartsAtTheNut()
    {
        // The shape nearest the nut is the one a player already knows.
        QCOMPARE(written(Chord::shapeOf(guitar(), {0, Chord::Quality::Major})),
                 QStringLiteral("x32010"));
        QCOMPARE(written(Chord::shapeOf(guitar(), {5, Chord::Quality::Major})),
                 QStringLiteral("133211"));
    }

    void aChordArrivesWhereTheHandAlreadyIs()
    {
        // A C major asked for with the hand at the eighth fret comes as the
        // shape under that hand rather than as the open one down at the nut.
        const QList<Fretboard::Position> near =
            Chord::shapeNear(guitar(), {0, Chord::Quality::Major}, 8);
        QVERIFY(!near.isEmpty());
        for (const Fretboard::Position &at : near) {
            QVERIFY2(at.fret >= 8 && at.fret <= 11,
                     qPrintable(QStringLiteral("fret %1 is not under a hand at 8").arg(at.fret)));
        }

        // With the hand nowhere -- nothing under the caret, or an open string,
        // which says nothing about where a hand is -- it is the shape nearest
        // the nut.
        QCOMPARE(written(Chord::shapeNear(guitar(), {0, Chord::Quality::Major}, -1)),
                 QStringLiteral("x32010"));
        QCOMPARE(written(Chord::shapeNear(guitar(), {0, Chord::Quality::Major}, 0)),
                 QStringLiteral("x32010"));

        // And where it cannot be held under that hand at all, a chord
        // somewhere beats no chord in the right place.
        QVERIFY(!Chord::shapeNear(guitar(), {0, Chord::Quality::Major}, 30).isEmpty());
    }

    void aChordWithANoteMissingIsNotTheChord()
    {
        // A hand past the end of the neck reaches nothing, so there is no
        // shape rather than a shape with notes missing from it.
        const Fretboard::Hand off{30, 4, true};
        QVERIFY(Chord::shapeOn(guitar(), {0, Chord::Quality::Major}, off).isEmpty());

        // A drum kit has no neck and so has no shapes.
        const Fretboard::Instrument kit{{}, 0, 24};
        QVERIFY(Chord::shapeOf(kit, {0, Chord::Quality::Major}).isEmpty());
    }

    void aCapoMovesTheShapesWithIt()
    {
        // The fret numbers under a capo are counted from the capo, so the
        // shape a player makes is the same one and the chord it sounds is not.
        const Fretboard::Hand nut{0, 4, true};
        QCOMPARE(written(Chord::shapeOn(guitar(2), {2, Chord::Quality::Major}, nut)),
                 QStringLiteral("x32010"));
    }
};

QTEST_GUILESS_MAIN(ChordTest)
#include "chordtest.moc"
