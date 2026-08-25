// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "timeline.h"

#include <QTest>

/**
 * The pass between the document and the sound, built here from scores made in
 * code rather than read from files -- the two questions are separate and their
 * tests should be too.
 */
class TimelineTest : public QObject
{
    Q_OBJECT

private:
    /** A guitar, six strings, and as many empty 4/4 bars as asked for. */
    static Score blank(int bars, int strings = 6)
    {
        Score score;
        Track guitar;
        guitar.name = QStringLiteral("Guitar");
        guitar.instrumentType = QStringLiteral("electricGuitar");
        for (int string = 0; string < strings; ++string) {
            guitar.tuning.append(40 + string * 5);
        }
        score.tracks.append(guitar);

        for (int index = 0; index < bars; ++index) {
            MasterBar bar;
            bar.bars = {index};
            score.masterBars.append(bar);
            score.bars.insert(index, Bar{{-1, -1, -1, -1}});
        }
        score.rhythms.insert(0, Rational(1));   // a quarter
        return score;
    }

    /** Puts one voice of `beats` into a bar, each beat holding one note. */
    static void fill(Score &score, int barIndex, const QList<Note> &notes)
    {
        const int voiceId = barIndex;
        QList<int> beatIds;
        for (int index = 0; index < notes.size(); ++index) {
            const int id = barIndex * 100 + index;
            score.notes.insert(id, notes.at(index));
            score.beats.insert(id, Beat{0, {id}, Dynamic::F, false, false});
            beatIds.append(id);
        }
        score.voices.insert(voiceId, Voice{beatIds});
        score.bars[barIndex] = Bar{{voiceId, -1, -1, -1}};
    }

    static Note at(int midi, int string)
    {
        Note note;
        note.midi = midi;
        note.string = string;
        return note;
    }

private Q_SLOTS:
    // ---- repeats ----

    void aScoreWithoutRepeatsIsPlayedAsWritten()
    {
        const Score score = blank(4);
        QCOMPARE(Timeline::playedOrder(score), QList<int>({0, 1, 2, 3}));
    }

    void aRepeatedSectionIsPlayedAsManyTimesAsItSays()
    {
        Score score = blank(4);
        score.masterBars[1].repeatStart = true;
        score.masterBars[2].repeatEnd = true;
        score.masterBars[2].repeatCount = 2;
        QCOMPARE(Timeline::playedOrder(score), QList<int>({0, 1, 2, 1, 2, 3}));
    }

    void aRepeatWithoutAStartGoesBackToTheBeginning()
    {
        Score score = blank(3);
        score.masterBars[1].repeatEnd = true;
        score.masterBars[1].repeatCount = 2;
        QCOMPARE(Timeline::playedOrder(score), QList<int>({0, 1, 0, 1, 2}));
    }

    void theScoreCanStillBeReadAsNotated()
    {
        Score score = blank(3);
        score.masterBars[0].repeatStart = true;
        score.masterBars[1].repeatEnd = true;
        score.masterBars[1].repeatCount = 4;
        QCOMPARE(Timeline::playedOrder(score, false), QList<int>({0, 1, 2}));
    }

    /** Approximate is fine; approximate and silent is not. */
    void alternateEndingsAreReportedRatherThanQuietlyPlayedWrongly()
    {
        Score score = blank(2);
        QVERIFY(!Timeline::hasAlternateEndings(score));
        score.masterBars[1].alternateEndings = true;
        QVERIFY(Timeline::hasAlternateEndings(score));
    }

    // ---- notes ----

    void everyStringGetsAChannelOfItsOwn()
    {
        Score score = blank(1);
        fill(score, 0, {at(40, 0), at(64, 5), at(50, 2)});

        const QList<Timeline::NoteEvent> notes =
            Timeline::notesFor(score, 0, Timeline::playedOrder(score));
        QCOMPARE(notes.size(), 3);
        for (const Timeline::NoteEvent &note : notes) {
            QCOMPARE(note.channel, note.string);
        }
    }

    /**
     * Pitch bend is per channel. Six strings sharing one would bend the chord
     * under the note being bent, which is the failure this whole arrangement
     * exists to avoid -- so it is asserted rather than assumed.
     */
    void twoNotesOnDifferentStringsNeverShareAChannel()
    {
        Score score = blank(1);
        fill(score, 0, {at(40, 0), at(45, 1)});

        const QList<Timeline::NoteEvent> notes =
            Timeline::notesFor(score, 0, Timeline::playedOrder(score));
        QCOMPARE(notes.size(), 2);
        QVERIFY(notes.at(0).channel != notes.at(1).channel);
    }

    void percussionHasNoStringsAndOneChannel()
    {
        Score score = blank(1, 0);
        score.tracks[0].instrumentType = QStringLiteral("drumKit");
        fill(score, 0, {at(36, -1), at(38, -1)});

        const QList<Timeline::NoteEvent> notes =
            Timeline::notesFor(score, 0, Timeline::playedOrder(score));
        QCOMPARE(notes.size(), 2);
        QCOMPARE(notes.at(0).channel, 0);
        QCOMPARE(notes.at(1).channel, 0);
    }

    /** The most audible mistake available: a tie that restrikes. */
    void aTieLengthensTheNoteRatherThanPlayingItAgain()
    {
        Score score = blank(2);
        Note held = at(64, 5);
        held.tieOrigin = true;
        Note continues = at(64, 5);
        continues.tieDestination = true;

        fill(score, 0, {held});
        fill(score, 1, {continues});
        // The first bar's single quarter is followed by three of silence, so
        // the tie in bar two starts four quarters in -- not one.
        score.rhythms.insert(1, Rational(4));
        score.beats[0] = Beat{1, {0}, Dynamic::F, false, false};
        score.beats[100] = Beat{1, {100}, Dynamic::F, false, false};

        const QList<Timeline::NoteEvent> notes =
            Timeline::notesFor(score, 0, Timeline::playedOrder(score));
        QCOMPARE(notes.size(), 1);
        QCOMPARE(notes.at(0).start, Rational(0));
        QCOMPARE(notes.at(0).end, Rational(8));
    }

    void aDeadNoteIsShortenedAndADeadenedOneHalved()
    {
        Score score = blank(1);
        Note dead = at(40, 0);
        dead.muted = true;
        Note palm = at(45, 1);
        palm.palmMuted = true;
        fill(score, 0, {dead, palm});

        const QList<Timeline::NoteEvent> notes =
            Timeline::notesFor(score, 0, Timeline::playedOrder(score));
        QCOMPARE(notes.at(0).end + Rational(-1, 1) < Rational(0), true);
        QCOMPARE(notes.at(1).end, Rational(1) + Rational(1, 2));
    }

    void accentsAndGhostNotesMoveTheVelocity()
    {
        Score score = blank(1);
        Note loud = at(40, 0);
        loud.accent = true;
        Note quiet = at(45, 1);
        quiet.ghost = true;
        fill(score, 0, {at(50, 2), loud, quiet});

        const QList<Timeline::NoteEvent> notes =
            Timeline::notesFor(score, 0, Timeline::playedOrder(score));
        const int plain = notes.at(0).velocity;
        QVERIFY(notes.at(1).velocity > plain);
        QVERIFY(notes.at(2).velocity < plain);
        for (const Timeline::NoteEvent &note : notes) {
            QVERIFY(note.velocity >= 1 && note.velocity <= 127);
        }
    }

    // ---- time ----

    void barsOfDifferentLengthsAddUp()
    {
        Score score = blank(3);
        score.masterBars[1].numerator = 3;
        score.masterBars[2].denominator = 8;
        score.masterBars[2].numerator = 6;
        // 4/4 + 3/4 + 6/8 == 4 + 3 + 3 quarters.
        QCOMPARE(Timeline::length(score, Timeline::playedOrder(score)), Rational(10));
    }

    void aTempoInsideARepeatTakesEffectTheFirstTimeThrough()
    {
        Score score = blank(3);
        score.masterBars[1].repeatStart = true;
        score.masterBars[1].repeatEnd = true;
        score.masterBars[1].repeatCount = 2;
        score.tempos.append({0, 0, 120});
        score.tempos.append({1, 0, 60});

        const QList<int> order = Timeline::playedOrder(score);
        QCOMPARE(order, QList<int>({0, 1, 1, 2}));

        const QList<Timeline::TempoEvent> tempos = Timeline::tempoMap(score, order);
        QCOMPARE(tempos.size(), 2);
        QCOMPARE(tempos.at(1).at, Rational(4));      // where bar 2 first falls
        QCOMPARE(tempos.at(1).quarterBpm, 60.0);

        // Four quarters at 120, then twelve at 60.
        QCOMPARE(Timeline::seconds(score, order), 2.0 + 12.0);
    }

    void aScoreWithNoTempoIsPlayedAtOneHundredAndTwenty()
    {
        const Score score = blank(2);
        const QList<int> order = Timeline::playedOrder(score);
        QCOMPARE(Timeline::tempoMap(score, order).size(), 1);
        QCOMPARE(Timeline::seconds(score, order), 4.0);
    }
};

QTEST_GUILESS_MAIN(TimelineTest)
#include "timelinetest.moc"
