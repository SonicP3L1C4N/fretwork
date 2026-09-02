// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "recorder.h"
#include "timeline.h"

#include <QTest>

/**
 * The quantisation policy, case by case.
 *
 * Every test here is one sentence of the policy in recorder.h made concrete,
 * because a policy that lives only in a comment is a policy that drifts. The
 * moments are in quarters: at 120 to the crotchet, forty milliseconds is
 * eight hundredths of a quarter, which is the number the early-note cases
 * use.
 */
class RecorderTest : public QObject
{
    Q_OBJECT

private:
    /** As many empty 4/4 bars as asked for, on one guitar. */
    static Score bars(int count)
    {
        Score score;
        Track guitar;
        guitar.instrumentType = QStringLiteral("electricGuitar");
        guitar.tuning = {40, 45, 50, 55, 59, 64};
        score.tracks.append(guitar);
        for (int index = 0; index < count; ++index) {
            MasterBar bar;
            bar.bars = {index};
            score.masterBars.append(bar);
            score.bars.insert(index, Bar{{-1, -1, -1, -1}});
        }
        return score;
    }

    static Recorder::Options semiquavers()
    {
        Recorder::Options options;
        options.grid = Rational(1, 4);
        options.instrument = Fretboard::Instrument{{40, 45, 50, 55, 59, 64}, 0, 24};
        return options;
    }

    static Rational total(const Recorder::Take &take)
    {
        Rational sum;
        for (const Recorder::Beat &beat : take.beats) {
            sum += beat.duration;
        }
        return sum;
    }

    static QList<Rational> durations(const Recorder::Take &take)
    {
        QList<Rational> all;
        for (const Recorder::Beat &beat : take.beats) {
            all.append(beat.duration);
        }
        return all;
    }

    static QList<int> noteCounts(const Recorder::Take &take)
    {
        QList<int> all;
        for (const Recorder::Beat &beat : take.beats) {
            all.append(int(beat.shape.size()));
        }
        return all;
    }

private Q_SLOTS:
    void aNoteALittleEarlyForTheDownbeatIsOnIt()
    {
        const Score score = bars(4);
        const QList<int> order = {0, 1, 2, 3};
        Recorder recorder(score, order, semiquavers());

        const Recorder::Take take = recorder.noteOn(64, 3.92);
        QVERIFY(take.isValid());
        QCOMPARE(take.pass, 1);
        QCOMPARE(take.bar, 1);
        // On the downbeat, so no rest in front of it, and held, so it lasts
        // to the bar line: one semibreve.
        QCOMPARE(durations(take), QList<Rational>{Rational(4)});
        QCOMPARE(noteCounts(take), QList<int>{1});
    }

    void keysThatLandOnTheSameLineAreOneBeat()
    {
        const Score score = bars(2);
        const QList<int> order = {0, 1};
        Recorder recorder(score, order, semiquavers());

        recorder.noteOn(60, 0.0);
        recorder.noteOn(64, 0.05);
        const Recorder::Take take = recorder.noteOn(67, 0.1);
        QCOMPARE(noteCounts(take), QList<int>{3});
        QCOMPARE(durations(take), QList<Rational>{Rational(4)});
    }

    void aReleaseWellBeforeTheNextKeyLeavesARest()
    {
        const Score score = bars(2);
        const QList<int> order = {0, 1};
        Recorder recorder(score, order, semiquavers());

        recorder.noteOn(64, 0.0);
        recorder.noteOff(64, 0.5);
        const Recorder::Take take = recorder.noteOn(66, 2.0);
        // A quaver, a dotted crotchet's rest, and a minim held to the line.
        QCOMPARE(durations(take), (QList<Rational>{Rational(1, 2), Rational(3, 2), Rational(2)}));
        QCOMPARE(noteCounts(take), (QList<int>{1, 0, 1}));
    }

    void aReleaseJustBeforeTheNextKeyIsNotARest()
    {
        const Score score = bars(2);
        const QList<int> order = {0, 1};
        Recorder recorder(score, order, semiquavers());

        recorder.noteOn(64, 0.0);
        recorder.noteOff(64, 1.9);
        const Recorder::Take take = recorder.noteOn(66, 2.0);
        QCOMPARE(durations(take), (QList<Rational>{Rational(2), Rational(2)}));
        QCOMPARE(noteCounts(take), (QList<int>{1, 1}));
    }

    void aNoteShorterThanTheGridIsACell()
    {
        const Score score = bars(2);
        const QList<int> order = {0, 1};
        Recorder recorder(score, order, semiquavers());

        recorder.noteOn(64, 0.0);
        const Recorder::Take take = recorder.noteOff(64, 0.02);
        QCOMPARE(take.beats.first().duration, Rational(1, 4));
        QCOMPARE(total(take), Rational(4));
    }

    void theBarAlwaysAddsUp()
    {
        const Score score = bars(2);
        const QList<int> order = {0, 1};
        Recorder recorder(score, order, semiquavers());

        recorder.noteOn(64, 1.0);
        const Recorder::Take take = recorder.noteOff(64, 1.5);
        // A crotchet's rest, a quaver, and two and a half quarters of rest
        // written as a minim and a quaver.
        QCOMPARE(durations(take),
                 (QList<Rational>{Rational(1), Rational(1, 2), Rational(2), Rational(1, 2)}));
        QCOMPARE(noteCounts(take), (QList<int>{0, 1, 0, 0}));
        QCOMPARE(total(take), Rational(4));
    }

    void aLengthNoValueCanWriteIsTiedAcrossTwo()
    {
        const Score score = bars(2);
        const QList<int> order = {0, 1};
        Recorder recorder(score, order, semiquavers());

        recorder.noteOn(64, 0.0);
        recorder.noteOff(64, 1.25);
        const Recorder::Take take = recorder.noteOn(66, 2.0);
        // Five semiquavers: a crotchet tied to a semiquaver, then the rest
        // of the half as a dotted quaver's rest, then the next note.
        QCOMPARE(durations(take),
                 (QList<Rational>{Rational(1), Rational(1, 4), Rational(3, 4), Rational(2)}));
        QVERIFY(take.beats.at(0).tiedToNext);
        QVERIFY(!take.beats.at(1).tiedToNext);
        QCOMPARE(take.beats.at(0).shape, take.beats.at(1).shape);
        QVERIFY(!take.beats.at(2).tiedToNext);
    }

    void writtenValuesLargestFirst()
    {
        QCOMPARE(Recorder::written(Rational(4)), QList<Rational>{Rational(4)});
        QCOMPARE(Recorder::written(Rational(3, 4)), QList<Rational>{Rational(3, 4)});
        QCOMPARE(Recorder::written(Rational(5, 4)), (QList<Rational>{Rational(1), Rational(1, 4)}));
        QCOMPARE(Recorder::written(Rational(7, 4)),
                 (QList<Rational>{Rational(3, 2), Rational(1, 4)}));
        QCOMPARE(Recorder::written(Rational(6)), QList<Rational>{Rational(6)});
    }

    void aPitchOffTheNeckIsLeftOutAndNamed()
    {
        const Score score = bars(2);
        const QList<int> order = {0, 1};
        Recorder recorder(score, order, semiquavers());

        const Recorder::Take take = recorder.noteOn(30, 0.0);
        QVERIFY(take.isValid());
        QCOMPARE(noteCounts(take), QList<int>{0});
        QCOMPARE(total(take), Rational(4));
        QCOMPARE(recorder.unplayable(), QList<int>{30});

        // Once per bar, not once per key: the same low note struck again is
        // the same complaint.
        recorder.noteOn(30, 1.0);
        QVERIFY(recorder.unplayable().isEmpty());
    }

    void aNoteHeldAcrossTheBarLineStopsAtIt()
    {
        const Score score = bars(2);
        const QList<int> order = {0, 1};
        Recorder recorder(score, order, semiquavers());

        const Recorder::Take first = recorder.noteOn(64, 2.0);
        QCOMPARE(durations(first), (QList<Rational>{Rational(2), Rational(2)}));

        const Recorder::Take second = recorder.noteOn(66, 5.0);
        QCOMPARE(second.bar, 1);
        QCOMPARE(durations(second), (QList<Rational>{Rational(1), Rational(3)}));
        QCOMPARE(noteCounts(second), (QList<int>{0, 1}));

        // The first key coming up in the second bar changes nothing: its
        // note ended at the bar line.
        QVERIFY(!recorder.noteOff(64, 5.5).isValid());
    }

    void aRepeatedBarIsTheSameBarEveryTimeThrough()
    {
        const Score score = bars(2);
        const QList<int> order = {0, 1, 0, 1};
        Recorder recorder(score, order, semiquavers());

        const Recorder::Take take = recorder.noteOn(64, 8.0);
        QCOMPARE(take.pass, 2);
        QCOMPARE(take.bar, 0);
    }

    void pastTheEndNothingIsWritten()
    {
        const Score score = bars(1);
        const QList<int> order = {0};
        Recorder recorder(score, order, semiquavers());
        QVERIFY(!recorder.noteOn(64, 4.0).isValid());
        QCOMPARE(recorder.pass(), -1);
    }

    void theHandStaysWhereTheLastBeatPutIt()
    {
        const Score score = bars(2);
        const QList<int> order = {0, 1};
        Recorder recorder(score, order, semiquavers());

        // E, G and B up the top string, the hand following.
        recorder.noteOn(64, 0.0);
        recorder.noteOff(64, 0.9);
        recorder.noteOn(67, 1.0);
        recorder.noteOff(67, 1.9);
        recorder.noteOn(71, 2.0);
        recorder.noteOff(71, 2.9);
        recorder.noteOn(60, 3.0);
        recorder.noteOn(64, 3.0);
        const Recorder::Take take = recorder.noteOn(67, 3.0);

        QCOMPARE(noteCounts(take), (QList<int>{1, 1, 1, 3}));
        QCOMPARE(take.beats.at(0).shape.first().fret, 0);
        QCOMPARE(take.beats.at(1).shape.first().fret, 3);
        QCOMPARE(take.beats.at(2).shape.first().fret, 7);
        // The triad is a shape near where the hand was, not three notes at
        // the nut: every fret within a hand's span, and none of them open.
        int lowest = 99;
        int highest = 0;
        for (const Fretboard::Position &at : take.beats.at(3).shape) {
            lowest = std::min(lowest, at.fret);
            highest = std::max(highest, at.fret);
        }
        QVERIFY(lowest > 0);
        QVERIFY(highest - lowest <= 3);
    }

    void aNewBarStartsWithTheHandWhereTheOldOneLeftIt()
    {
        const Score score = bars(2);
        const QList<int> order = {0, 1};
        Recorder recorder(score, order, semiquavers());

        recorder.noteOn(71, 0.0);       // B4 at the seventh fret of the top string
        recorder.noteOff(71, 3.9);
        // A4 with the hand at the seventh fret is fret 10 on the B string,
        // under the fingers; a hand starting afresh would put it at fret 5.
        const Recorder::Take next = recorder.noteOn(69, 4.0);
        QCOMPARE(next.bar, 1);
        QCOMPARE(next.beats.first().shape.first().string, 4);
        QCOMPARE(next.beats.first().shape.first().fret, 10);
    }

    void snappingRoundsToTheNearestLine()
    {
        const Score score = bars(1);
        const QList<int> order = {0};
        Recorder recorder(score, order, semiquavers());
        QCOMPARE(recorder.snapped(0.12), Rational(0));
        QCOMPARE(recorder.snapped(0.13), Rational(1, 4));
        QCOMPARE(recorder.snapped(1.0), Rational(1));
        QCOMPARE(recorder.snapped(-0.5), Rational(0));
    }
};

QTEST_GUILESS_MAIN(RecorderTest)
#include "recordertest.moc"
