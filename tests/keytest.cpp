// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "key.h"

#include <QTest>

/**
 * Writing a pitch down.
 *
 * MIDI 63 is a sound. D sharp and E flat are two different notes that make it,
 * and the only thing that decides between them is the key -- so the test that
 * matters most here is not any single spelling but the invariant underneath
 * them all: whatever is written, it must sound what it was spelled from. A
 * spelling layer that is merely plausible and does not round-trip is a layer
 * that will put a wrong note on a page.
 */
class KeyTest : public QObject
{
    Q_OBJECT

private:
    static Key::Signature sharps(int count, bool minor = false)
    {
        return Key::Signature{count, minor};
    }

    static Key::Signature flats(int count, bool minor = false)
    {
        return Key::Signature{-count, minor};
    }

private Q_SLOTS:
    void theSameSoundIsTwoDifferentNotes()
    {
        // The example the roadmap set this layer to answer.
        QCOMPARE(Key::withOctave(Key::spell(63, sharps(5))), QStringLiteral("D♯4"));
        QCOMPARE(Key::withOctave(Key::spell(63, flats(5, true))), QStringLiteral("E♭4"));

        // Neither needed anything but the accidentals at the head of the
        // staff: the mode names the key and does not spell it.
        QCOMPARE(Key::spell(63, sharps(5)), Key::spell(63, sharps(5, true)));
    }

    /**
     * The invariant the whole layer stands on, over every pitch on a piano and
     * every signature anybody writes.
     */
    void whateverIsWrittenSoundsWhatItWasSpelledFrom()
    {
        for (int accidentals = -7; accidentals <= 7; ++accidentals) {
            for (const bool minor : {false, true}) {
                const Key::Signature signature{accidentals, minor};
                for (int midi = 21; midi <= 108; ++midi) {
                    const Key::Spelling spelling = Key::spell(midi, signature);
                    // Built only where it fails: QVERIFY2 evaluates its
                    // message whether or not it needs it, and naming the key
                    // is a translated string being asked for two and a half
                    // thousand times to say nothing.
                    if (Key::midiOf(spelling) != midi) {
                        QFAIL(qPrintable(QStringLiteral("%1 in %2 spelled as %3, which sounds %4")
                                             .arg(midi)
                                             .arg(Key::nameOf(signature),
                                                  Key::withOctave(spelling))
                                             .arg(Key::midiOf(spelling))));
                    }
                    // And nothing needs a triple anything to write it.
                    QVERIFY(qAbs(spelling.alteration) <= 2);
                }
            }
        }
    }

    void theOctaveBelongsToTheLetterAndNotToThePitch()
    {
        // MIDI 60 is middle C, and in C sharp major it is written as the B
        // sharp below it -- which is in the octave of its B.
        QCOMPARE(Key::withOctave(Key::spell(60, sharps(7))), QStringLiteral("B♯3"));

        // And the other way about: MIDI 59 is B3, written in C flat major as
        // the C flat above it.
        QCOMPARE(Key::withOctave(Key::spell(59, flats(7))), QStringLiteral("C♭4"));
    }

    void aNoteOutsideTheKeyTakesTheSmallestAccidental()
    {
        // Every letter in C sharp major is already sharp, so following the
        // key's direction alone would write a D natural as a C double sharp.
        // A plain letter is easier to read than a double anything.
        QCOMPARE(Key::nameOf(Key::spell(62, sharps(7))), QStringLiteral("D"));
        QVERIFY(!Key::isDiatonic(62, sharps(7)));
    }

    void whereTwoAreEquallySmallTheKeyDecides()
    {
        // The same pitch, written the way the key around it is written.
        QCOMPARE(Key::nameOf(Key::spell(66, sharps(0))), QStringLiteral("F♯"));
        QCOMPARE(Key::nameOf(Key::spell(66, flats(1))), QStringLiteral("G♭"));
    }

    void theSevenNotesOfTheKeyAreTheOnesInIt()
    {
        for (const int midi : {60, 62, 64, 65, 67, 69, 71}) {
            QVERIFY(Key::isDiatonic(midi, sharps(0)));
        }
        QVERIFY(!Key::isDiatonic(66, sharps(0)));

        // B major has D sharp in it, which is why 63 was spelled that way.
        QVERIFY(Key::isDiatonic(63, sharps(5)));
        QVERIFY(!Key::isDiatonic(63, sharps(0)));
    }

    /**
     * The table of tonics is written out rather than derived, so it is worth
     * checking against the rules rather than against itself: spelling a key's
     * own tonic, by the ordinary spelling rule, must produce the note the
     * table says the key is named after.
     */
    void everyKeyIsNamedAfterANoteItContains()
    {
        for (int accidentals = -7; accidentals <= 7; ++accidentals) {
            for (const bool minor : {false, true}) {
                const Key::Signature signature{accidentals, minor};
                const Key::Spelling tonic = Key::tonicOf(signature);
                const Key::Spelling spelled = Key::spell(Key::midiOf(tonic), signature);
                if (spelled.step != tonic.step || spelled.alteration != tonic.alteration) {
                    QFAIL(qPrintable(QStringLiteral("%1 is named after %2 but spells it %3")
                                         .arg(Key::nameOf(signature), Key::nameOf(tonic),
                                              Key::nameOf(spelled))));
                }
                QVERIFY(Key::isDiatonic(Key::midiOf(tonic), signature));
            }
        }
    }

    void theCircleIsTheWayRoundEverybodyElseHasIt()
    {
        QCOMPARE(Key::nameOf(sharps(0)), QStringLiteral("C major"));
        QCOMPARE(Key::nameOf(sharps(0, true)), QStringLiteral("A minor"));
        QCOMPARE(Key::nameOf(sharps(5)), QStringLiteral("B major"));
        QCOMPARE(Key::nameOf(sharps(5, true)), QStringLiteral("G♯ minor"));
        QCOMPARE(Key::nameOf(flats(5)), QStringLiteral("D♭ major"));
        QCOMPARE(Key::nameOf(flats(5, true)), QStringLiteral("B♭ minor"));
        QCOMPARE(Key::nameOf(sharps(7)), QStringLiteral("C♯ major"));
        QCOMPARE(Key::nameOf(flats(7)), QStringLiteral("C♭ major"));
    }

    void aSignatureNobodyWritesIsNoSignatureAtAll()
    {
        QVERIFY(Key::isValid(sharps(7)));
        QVERIFY(Key::isValid(flats(7)));
        QVERIFY(!Key::isValid(sharps(8)));
        QVERIFY(!Key::isValid(flats(8)));

        // And it is read as none rather than as something: a file that is
        // wrong about its key is not a file nobody can open.
        QCOMPARE(Key::spell(63, sharps(9)), Key::spell(63, sharps(0)));
        QCOMPARE(Key::nameOf(Key::tonicOf(sharps(9))), QStringLiteral("C"));
    }

    void anAccidentalIsWrittenAsOne()
    {
        QCOMPARE(Key::nameOf(Key::Spelling{1, 1, 4}), QStringLiteral("D♯"));
        QCOMPARE(Key::nameOf(Key::Spelling{6, -1, 3}), QStringLiteral("B♭"));
        QCOMPARE(Key::nameOf(Key::Spelling{3, 0, 4}), QStringLiteral("F"));
        // Doubles are the sign twice, because the proper characters for them
        // are ones most fonts have never heard of.
        QCOMPARE(Key::nameOf(Key::Spelling{0, 2, 4}), QStringLiteral("C♯♯"));
        QCOMPARE(Key::nameOf(Key::Spelling{4, -2, 4}), QStringLiteral("G♭♭"));
    }
};

QTEST_GUILESS_MAIN(KeyTest)
#include "keytest.moc"
