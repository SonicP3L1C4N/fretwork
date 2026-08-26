// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "swing.h"
#include "timeline.h"

#include <QTest>

/**
 * A shuffle, which is the difference between a blues and a nursery rhyme.
 *
 * Two halves to this. The warp itself is arithmetic and is checked against
 * numbers anybody can work out on paper: a pair of quavers played two thirds
 * and one third. The other half is that the warp reaches the notes -- a rule
 * that is right and never applied is the same as no rule.
 *
 * The case worth reading is `aBarThatDoesNotHoldWholePairsIsLeftAtItsLength`.
 * A warp that stretched the odd quaver at the end of a 7/8 bar would move the
 * end of the bar, and every bar after it would start in the wrong place -- one
 * bar's feel would have quietly rewritten the rest of the piece.
 */
class SwingTest : public QObject
{
    Q_OBJECT

private:
    static Rational at(qint64 numerator, qint64 denominator)
    {
        return Rational(numerator, denominator);
    }

    /** One 4/4 bar of `count` beats of `rhythm`, all on the bottom string. */
    static Score bar(const Rational &rhythm, int count, TripletFeel feel,
                     int numerator = 4, int denominator = 4)
    {
        Score score;
        Track guitar;
        guitar.name = QStringLiteral("Guitar");
        guitar.instrumentType = QStringLiteral("electricGuitar");
        guitar.tuning = {40, 45, 50, 55, 59, 64};
        score.tracks.append(guitar);

        MasterBar master;
        master.bars = {0};
        master.tripletFeel = feel;
        master.numerator = numerator;
        master.denominator = denominator;
        score.masterBars.append(master);

        score.rhythms.insert(0, rhythm);
        QList<int> beats;
        for (int index = 0; index < count; ++index) {
            Note note;
            note.midi = 40 + index;
            note.string = 0;
            score.notes.insert(index, note);
            score.beats.insert(index, Beat{0, {index}, Dynamic::F, false, false});
            beats.append(index);
        }
        score.voices.insert(0, Voice{beats});
        score.bars.insert(0, Bar{{0, -1, -1, -1}});
        return score;
    }

    static QList<Rational> startsOf(const Score &score)
    {
        QList<Rational> starts;
        for (const Timeline::NoteEvent &event :
             Timeline::notesFor(score, 0, Timeline::playedOrder(score))) {
            starts.append(event.start);
        }
        return starts;
    }

private Q_SLOTS:
    void aStraightBarIsNotTouched()
    {
        for (const Rational &position : {at(0, 1), at(1, 4), at(1, 2), at(3, 1)}) {
            QCOMPARE(Swing::played(position, TripletFeel::None, Rational(4)), position);
        }
    }

    void aPairOfQuaversBecomesTwoThirdsAndOneThird()
    {
        const auto swung = [](qint64 n, qint64 d) {
            return Swing::played(Rational(n, d), TripletFeel::Triplet8th, Rational(4));
        };
        QCOMPARE(swung(0, 1), at(0, 1));
        QCOMPARE(swung(1, 2), at(2, 3));        // the second quaver, moved late
        QCOMPARE(swung(1, 1), at(1, 1));        // the next beat, exactly where it was
        QCOMPARE(swung(3, 2), at(5, 3));
        QCOMPARE(swung(2, 1), at(2, 1));

        // Inside each half the stretch is even, which is what makes the warp a
        // warp rather than a rule about pairs.
        QCOMPARE(swung(1, 4), at(1, 3));
        QCOMPARE(swung(3, 4), at(5, 6));
    }

    void everyFeelKeepsTheBeatsWhereTheyWere()
    {
        for (const TripletFeel feel :
             {TripletFeel::Triplet8th, TripletFeel::Triplet16th, TripletFeel::Dotted8th,
              TripletFeel::Dotted16th, TripletFeel::Scottish8th, TripletFeel::Scottish16th}) {
            const Rational unit = Swing::unitOf(feel);
            for (int pair = 0; pair < 8; ++pair) {
                const Rational boundary = unit * Rational(pair);
                QVERIFY2(Swing::played(boundary, feel, Rational(4)) == boundary,
                         qPrintable(QStringLiteral("pair boundary %1/%2 moved")
                                        .arg(boundary.numerator)
                                        .arg(boundary.denominator)));
            }
        }
    }

    void theOtherFeelsDivideThePairDifferently()
    {
        const Rational half = at(1, 2);
        // Dotted: three quarters and a quarter, which is harder than a shuffle.
        QCOMPARE(Swing::played(half, TripletFeel::Dotted8th, Rational(4)), at(3, 4));
        // Snapped: the short note first, the whole point of the thing.
        QCOMPARE(Swing::played(half, TripletFeel::Scottish8th, Rational(4)), at(1, 4));
        // A semiquaver feel swings a pair of semiquavers, so its unit is half
        // the size and a quaver is where the line falls.
        QCOMPARE(Swing::played(at(1, 4), TripletFeel::Triplet16th, Rational(4)), at(1, 3));
        QCOMPARE(Swing::played(half, TripletFeel::Triplet16th, Rational(4)), half);
    }

    void itNeverGoesBackwards()
    {
        // A warp that was not monotonic would put two notes out of order, and
        // the second would be played before the first.
        for (const TripletFeel feel :
             {TripletFeel::Triplet8th, TripletFeel::Dotted16th, TripletFeel::Scottish8th}) {
            Rational previous(-1);
            for (int step = 0; step <= 64; ++step) {
                const Rational moved = Swing::played(Rational(step, 16), feel, Rational(4));
                QVERIFY(previous < moved);
                previous = moved;
            }
        }
    }

    void aBarThatDoesNotHoldWholePairsIsLeftAtItsLength()
    {
        // 7/8 with a quaver feel: three pairs and a quaver over. The odd
        // quaver is not half of a pair that is not there.
        const Rational sevenEight = Rational(7 * 4, 8);
        const auto swung = [&](qint64 n, qint64 d) {
            return Swing::played(Rational(n, d), TripletFeel::Triplet8th, sevenEight);
        };
        QCOMPARE(swung(5, 2), at(8, 3));            // inside the third pair, swung
        QCOMPARE(swung(13, 4), at(13, 4));          // inside the odd quaver, left alone
        QCOMPARE(swung(7, 2), sevenEight);          // and the bar ends where it ends
    }

    // ---- and now the notes themselves ----

    void theNotesOfASwungBarArePlayedLate()
    {
        const Score score = bar(Rational(1, 2), 4, TripletFeel::Triplet8th);
        QCOMPARE(startsOf(score), QList<Rational>({at(0, 1), at(2, 3), at(1, 1), at(5, 3)}));

        const Score straight = bar(Rational(1, 2), 4, TripletFeel::None);
        QCOMPARE(startsOf(straight), QList<Rational>({at(0, 1), at(1, 2), at(1, 1), at(3, 2)}));
    }

    void aSwungNoteIsAsLongAsTheRoomItHasNow()
    {
        const QList<Timeline::NoteEvent> events =
            Timeline::notesFor(bar(Rational(1, 2), 4, TripletFeel::Triplet8th), 0, {0});
        QCOMPARE(events.size(), 4);
        // Long, short, long, short -- and no gaps between them.
        QCOMPARE(events.at(0).end - events.at(0).start, at(2, 3));
        QCOMPARE(events.at(1).end - events.at(1).start, at(1, 3));
        for (int index = 1; index < events.size(); ++index) {
            QCOMPARE(events.at(index - 1).end, events.at(index).start);
        }
        QCOMPARE(events.last().end, at(2, 1));
    }

    void crotchetsDoNotCareThatTheBarSwings()
    {
        // The whole reason this is a warp: a beat is a beat either way, and
        // only what happens between beats moves.
        QCOMPARE(startsOf(bar(Rational(1), 4, TripletFeel::Triplet8th)),
                 QList<Rational>({at(0, 1), at(1, 1), at(2, 1), at(3, 1)}));
    }

    void aSwungBarIsStillFourFour()
    {
        // The bar is the same length, so the piece is the same length: a feel
        // that changed how long a score took would be a feel that had got out
        // of the bar it was written in.
        const Score score = bar(Rational(1, 2), 4, TripletFeel::Triplet8th);
        QCOMPARE(Timeline::length(score, {0}), Rational(4));
        QCOMPARE(Timeline::length(score, {0, 0}), Rational(8));
    }
};

QTEST_GUILESS_MAIN(SwingTest)
#include "swingtest.moc"
