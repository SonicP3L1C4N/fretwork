// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "rational.h"

#include <QTest>

/**
 * Durations, which are the one thing in a score that must not drift.
 */
class RationalTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    /**
     * A file can ask for arithmetic the type cannot do exactly, and the
     * answer has to be a number rather than a fault: two dotted rhythms
     * with thirty-two dots each multiply their denominators to exactly 2^64,
     * which is nought in sixty-four bits, and a nought denominator is a
     * hardware divide-by-zero the next time anybody asks how long a bar is.
     */
    void survivesWhatAHostileFileAsksOf()
    {
        const Rational zero(1, 0);
        QCOMPARE(zero, Rational(0));
        QVERIFY(zero.denominator > 0);

        const Rational huge(1, qint64(1) << 32);
        const Rational sum = huge + huge;
        QVERIFY(sum.denominator > 0);
        QCOMPARE(sum, Rational(1, qint64(1) << 31));

        // Where the exact product would not fit, the answer is the nearest
        // point on a fine grid rather than a wrapped integer.
        const Rational wide(1, (qint64(1) << 62) + 1);
        const Rational other(1, (qint64(1) << 62) - 1);
        const Rational tiny = wide + other;
        QVERIFY(tiny.denominator > 0);
        QVERIFY(!(tiny < Rational(0)));
        QVERIFY(tiny < Rational(1, 1000));

        const Rational big(qint64(1) << 40);
        const Rational product = big * big;
        QVERIFY(product.denominator > 0);
        QVERIFY(Rational(1) < product);

        QCOMPARE(Rational(1) / Rational(0), Rational(0));
        QVERIFY(wide < other);
    }

    void reducesWhatItIsGiven()
    {
        QCOMPARE(Rational(2, 4), Rational(1, 2));
        QCOMPARE(Rational(6, 3), Rational(2));
        QCOMPARE(Rational(-1, -2), Rational(1, 2));
        QCOMPARE(Rational(1, -2), Rational(-1, 2));
    }

    /**
     * Three triplet eighths are a quarter, exactly.
     *
     * Doubles get this particular sum right, which is what makes the problem
     * easy to dismiss: 1/3 three times rounds neatly back to 1.0. Five triplet
     * sixteenths do not, and that is the second assertion. Of the sixty-odd
     * durations a real score is built from -- plain, dotted, and tuplets of
     * 3:2, 5:4, 6:4 and 7:8 -- forty-two drift from their exact value within
     * sixty-four repetitions.
     */
    void tripletsAccumulateExactly()
    {
        const Rational tripletEighth = Rational(1, 2) * Rational(2, 3);
        QCOMPARE(tripletEighth + tripletEighth + tripletEighth, Rational(1));

        // A triplet sixteenth, five of them: 1/6 of a quarter, five sixths.
        const Rational tripletSixteenth = Rational(1, 4) * Rational(2, 3);
        Rational exact;
        double drifting = 0;
        for (int count = 0; count < 5; ++count) {
            exact += tripletSixteenth;
            drifting += tripletSixteenth.toDouble();
        }
        QCOMPARE(exact, Rational(5, 6));
        QVERIFY(drifting != exact.toDouble());
    }

    void dottedNotesAreHalfAgainAndThreeQuartersAgain()
    {
        // The importer's arithmetic: 2 - 1/2^dots.
        QCOMPARE(Rational(1) * Rational(3, 2), Rational(3, 2));
        QCOMPARE(Rational(1) * Rational(7, 4), Rational(7, 4));
    }

    void addsAcrossDifferentDenominators()
    {
        QCOMPARE(Rational(1, 3) + Rational(1, 6), Rational(1, 2));
        QCOMPARE(Rational(4) + Rational(3) + Rational(3), Rational(10));
    }

    void ordersWithoutFloating()
    {
        QVERIFY(Rational(1, 3) < Rational(1, 2));
        QVERIFY(!(Rational(1, 2) < Rational(1, 3)));
        QVERIFY(Rational(-1, 2) < Rational(0));
    }

    void subtractsAndDividesExactly()
    {
        QCOMPARE(Rational(1) - Rational(1, 3), Rational(2, 3));
        QCOMPARE(Rational(1, 2) - Rational(3, 4), Rational(-1, 4));
        QCOMPARE(Rational(2, 3) / Rational(1, 3), Rational(2));
        QCOMPARE(Rational(1) / Rational(3), Rational(1, 3));
        QCOMPARE(Rational(3, 4) / Rational(1, 2), Rational(3, 2));

        // How many whole ones fit, which is what finding the pair a position
        // falls in comes down to.
        QCOMPARE(Rational(7, 2).dividedBy(Rational(1)), 3);
        QCOMPARE(Rational(4).dividedBy(Rational(1)), 4);
        QCOMPARE(Rational(1, 3).dividedBy(Rational(1, 2)), 0);
        QCOMPARE(Rational(5, 4).dividedBy(Rational(1, 2)), 2);
    }

    void convertsToTicksByRounding()
    {
        QCOMPARE(Rational(1).toTicks(960), 960);
        QCOMPARE(Rational(1, 3).toTicks(960), 320);
        // A 960-tick quarter cannot divide by seven, so the nearest tick it is.
        QCOMPARE(Rational(1, 7).toTicks(960), 137);
    }
};

QTEST_GUILESS_MAIN(RationalTest)
#include "rationaltest.moc"
