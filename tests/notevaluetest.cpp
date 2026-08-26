// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "notevalue.h"

#include <QTest>

/**
 * Reading a written symbol back out of a duration.
 *
 * The document keeps durations with their dots and tuplets already multiplied
 * in, because that is what playback needs; the page and the editor both need
 * the symbol back. The test that matters is that the two directions are
 * inverses -- if they are not, a dotted crotchet drawn once and dotted again
 * becomes something nobody asked for.
 */
class NoteValueTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void readsBackEveryValueAndEveryDot()
    {
        for (Rational value = NoteValue::Shortest; !(NoteValue::Longest < value);
             value = value * Rational(2)) {
            for (int dots = 0; dots < 3; ++dots) {
                const NoteValue::Written written{value, dots};
                const Rational duration = NoteValue::durationOf(written);
                QVERIFY2(NoteValue::of(duration) == written,
                         qPrintable(QStringLiteral("%1/%2 with %3 dots came back as %4/%5 "
                                                   "with %6")
                                        .arg(value.numerator)
                                        .arg(value.denominator)
                                        .arg(dots)
                                        .arg(NoteValue::of(duration).value.numerator)
                                        .arg(NoteValue::of(duration).value.denominator)
                                        .arg(NoteValue::of(duration).dots)));
            }
        }
    }

    void namesValuesTheWayMusiciansDo()
    {
        QCOMPARE(NoteValue::valueOf(1), Rational(4));        // a semibreve
        QCOMPARE(NoteValue::valueOf(2), Rational(2));        // a minim
        QCOMPARE(NoteValue::valueOf(4), Rational(1));        // a crotchet
        QCOMPARE(NoteValue::valueOf(8), Rational(1, 2));     // a quaver
        QCOMPARE(NoteValue::valueOf(64), Rational(1, 16));

        // Nothing shorter, nothing that is not a note value at all.
        QVERIFY(NoteValue::valueOf(128).isZero());
        QVERIFY(NoteValue::valueOf(6).isZero());
        QVERIFY(NoteValue::valueOf(0).isZero());
    }

    void countsBeamsFromTheValue()
    {
        QCOMPARE(NoteValue::beamsOf(Rational(4)), 0);
        QCOMPARE(NoteValue::beamsOf(Rational(1)), 0);
        QCOMPARE(NoteValue::beamsOf(Rational(1, 2)), 1);
        QCOMPARE(NoteValue::beamsOf(Rational(1, 4)), 2);
        QCOMPARE(NoteValue::beamsOf(Rational(1, 16)), 4);
    }

    /**
     * A tuplet is written as the value it is written as.
     *
     * Three quavers in the time of two are still three quavers on the page,
     * and a third of a crotchet is no dotted anything, so there is nothing to
     * recover and the next value up is the honest answer.
     */
    void givesATupletTheValueItIsWrittenAs()
    {
        QCOMPARE(NoteValue::of(Rational(1, 3)).value, Rational(1, 2));   // triplet quaver
        QCOMPARE(NoteValue::of(Rational(1, 3)).dots, 0);
        QCOMPARE(NoteValue::of(Rational(2, 3)).value, Rational(1));      // triplet crotchet
        QCOMPARE(NoteValue::of(Rational(1, 6)).value, Rational(1, 4));   // triplet semiquaver
        QCOMPARE(NoteValue::of(Rational(1, 5)).value, Rational(1, 4));   // one of five
    }

    void refusesWhatIsNotANoteValue()
    {
        QVERIFY(NoteValue::isValue(Rational(1)));
        QVERIFY(NoteValue::isValue(Rational(1, 8)));
        QVERIFY(!NoteValue::isValue(Rational(3, 2)));
        QVERIFY(!NoteValue::isValue(Rational(0)));
        QVERIFY(!NoteValue::isValue(Rational(-1)));
    }
};

QTEST_GUILESS_MAIN(NoteValueTest)
#include "notevaluetest.moc"
