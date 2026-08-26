// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "cursor.h"
#include "editor.h"

#include <QTest>
#include <QUndoStack>

/**
 * The caret and the undo history.
 *
 * The tests worth having here are the ones about *reversal*. Whether typing a
 * number puts a number on the page is easy; whether undoing it leaves the
 * score exactly as it was -- including a string that had nothing on it at all,
 * rather than a fret 0, which is a different note and a different-looking bar
 * -- is the thing that decides whether an editor can be trusted.
 */
class EditorTest : public QObject
{
    Q_OBJECT

private:
    /** Two bars of four quarter-note beats, on a guitar in standard tuning. */
    static Score twoBars()
    {
        Score score;
        Track guitar;
        guitar.name = QStringLiteral("Guitar");
        guitar.instrumentType = QStringLiteral("electricGuitar");
        guitar.tuning = {40, 45, 50, 55, 59, 64};
        score.tracks.append(guitar);
        score.rhythms.insert(0, Rational(1));

        int id = 0;
        for (int bar = 0; bar < 2; ++bar) {
            MasterBar master;
            master.bars = {bar};
            score.masterBars.append(master);

            QList<int> beats;
            for (int beat = 0; beat < 4; ++beat) {
                score.beats.insert(id, Beat{0, {}, Dynamic::F, false, false});
                beats.append(id);
                ++id;
            }
            score.voices.insert(bar, Voice{beats});
            score.bars.insert(bar, Bar{{bar, -1, -1, -1}});
        }
        return score;
    }

    /** Everything about a score that an edit could change, as comparable text. */
    static QString fingerprint(const Score &score)
    {
        QStringList out;
        for (int bar = 0; bar < score.masterBars.size(); ++bar) {
            for (const int voiceId : score.bars.value(score.masterBars.at(bar).bars.value(0)).voices) {
                if (voiceId < 0) {
                    continue;
                }
                for (const int beatId : score.voices.value(voiceId).beats) {
                    QStringList notes;
                    for (const int noteId : score.beats.value(beatId).notes) {
                        const Note note = score.notes.value(noteId);
                        notes.append(QStringLiteral("s%1f%2m%3")
                                         .arg(note.string)
                                         .arg(note.fret)
                                         .arg(note.midi));
                    }
                    // The duration too: an edit that changed how long a beat
                    // lasts and nothing else has to show up here, or the
                    // reversal tests are agreeing with a bug.
                    const Rational duration =
                        score.rhythms.value(score.beats.value(beatId).rhythm, Rational(1));
                    out.append(QStringLiteral("b%1:%2:%3/%4")
                                   .arg(beatId)
                                   .arg(notes.join(QLatin1Char(',')))
                                   .arg(duration.numerator)
                                   .arg(duration.denominator));
                }
            }
        }
        return out.join(QLatin1Char('|'));
    }

    static Cursor at(int bar, int beat, int string)
    {
        Cursor cursor;
        cursor.bar = bar;
        cursor.beat = beat;
        cursor.string = string;
        return cursor;
    }

private Q_SLOTS:
    // ---- the caret ----

    void movingRightOffABarStepsIntoTheNext()
    {
        Editor editor;
        editor.setScore(twoBars());
        editor.setCursor(at(0, 3, 0));

        editor.move(Editing::Move::Right);
        QCOMPARE(editor.cursor().bar, 1);
        QCOMPARE(editor.cursor().beat, 0);
    }

    void movingLeftOffABarStepsBackIntoTheLastBeat()
    {
        Editor editor;
        editor.setScore(twoBars());
        editor.setCursor(at(1, 0, 0));

        editor.move(Editing::Move::Left);
        QCOMPARE(editor.cursor().bar, 0);
        QCOMPARE(editor.cursor().beat, 3);
    }

    /** Up the page is up in pitch, because string 0 is drawn at the bottom. */
    void upIsTowardsTheThinStrings()
    {
        Editor editor;
        editor.setScore(twoBars());
        editor.setCursor(at(0, 0, 0));

        editor.move(Editing::Move::Up);
        QCOMPARE(editor.cursor().string, 1);

        editor.setCursor(at(0, 0, 5));
        editor.move(Editing::Move::Up);
        QCOMPARE(editor.cursor().string, 5);    // no seventh string to reach

        editor.move(Editing::Move::Down);
        QCOMPARE(editor.cursor().string, 4);
    }

    void aCursorAlwaysPointsSomewhereReal()
    {
        Editor editor;
        editor.setScore(twoBars());
        editor.setCursor(at(99, 99, 99));

        const Cursor cursor = editor.cursor();
        QCOMPARE(cursor.bar, 1);
        QCOMPARE(cursor.string, 5);
        QVERIFY(cursor.beat <= 4);
    }

    // ---- typing ----

    void typingADigitPutsAFretOnTheString()
    {
        Editor editor;
        editor.setScore(twoBars());
        editor.setCursor(at(0, 0, 5));
        editor.typeDigit(7);

        const int noteId = Editing::noteIdAt(editor.score(), editor.cursor());
        QVERIFY(noteId >= 0);
        QCOMPARE(editor.score().notes.value(noteId).fret, 7);
        // The open string plus the fret is the note that sounds.
        QCOMPARE(editor.score().notes.value(noteId).midi, 64 + 7);
    }

    /** One then two is fret twelve, and one press of undo takes it all away. */
    void twoDigitsAreOneNumberAndOneUndo()
    {
        Editor editor;
        editor.setScore(twoBars());
        editor.setCursor(at(0, 0, 5));

        editor.typeDigit(1);
        editor.typeDigit(2);

        const int noteId = Editing::noteIdAt(editor.score(), editor.cursor());
        QCOMPARE(editor.score().notes.value(noteId).fret, 12);
        QCOMPARE(editor.undoStack()->count(), 1);

        editor.undo();
        QCOMPARE(Editing::noteIdAt(editor.score(), editor.cursor()), -1);
    }

    void endingTheRunStartsANewNumber()
    {
        Editor editor;
        editor.setScore(twoBars());
        editor.setCursor(at(0, 0, 5));

        editor.typeDigit(1);
        editor.endDigitEntry();
        editor.typeDigit(2);

        const int noteId = Editing::noteIdAt(editor.score(), editor.cursor());
        QCOMPARE(editor.score().notes.value(noteId).fret, 2);
        QCOMPARE(editor.undoStack()->count(), 2);
    }

    void movingTheCaretEndsTheNumber()
    {
        Editor editor;
        editor.setScore(twoBars());
        editor.setCursor(at(0, 0, 5));

        editor.typeDigit(1);
        editor.move(Editing::Move::Right);
        editor.move(Editing::Move::Left);
        editor.typeDigit(2);

        QCOMPARE(editor.score().notes.value(Editing::noteIdAt(editor.score(), editor.cursor())).fret, 2);
    }

    /** 37 is a slip, not a note: it starts a new number rather than being clamped. */
    void aFretTooHighIsTreatedAsANewNumber()
    {
        Editor editor;
        editor.setScore(twoBars());
        editor.setCursor(at(0, 0, 5));

        editor.typeDigit(3);
        editor.typeDigit(7);        // 37 does not exist on any guitar

        QCOMPARE(editor.score().notes.value(Editing::noteIdAt(editor.score(), editor.cursor())).fret, 7);
        QVERIFY(editor.score().notes.value(Editing::noteIdAt(editor.score(), editor.cursor())).fret <= 36);
    }

    // ---- reversal, which is the point ----

    /**
     * Undoing a note that was typed onto an empty string must leave the string
     * empty, not leave a fret 0 behind. They look different and sound
     * different, and this is the single easiest thing for an editor to get
     * wrong.
     */
    void undoingANewNoteLeavesNothingRatherThanAZero()
    {
        Editor editor;
        Score score = twoBars();
        const QString before = fingerprint(score);
        editor.setScore(score);

        editor.setCursor(at(0, 1, 3));
        editor.typeDigit(5);
        QVERIFY(fingerprint(editor.score()) != before);

        editor.undo();
        QCOMPARE(fingerprint(editor.score()), before);
        QCOMPARE(Editing::noteIdAt(editor.score(), editor.cursor()), -1);
    }

    void undoingAChangedFretPutsTheOldOneBack()
    {
        Editor editor;
        editor.setScore(twoBars());
        editor.setCursor(at(0, 0, 2));
        editor.typeDigit(3);
        editor.endDigitEntry();

        const QString withThree = fingerprint(editor.score());
        editor.typeDigit(9);
        QCOMPARE(editor.score().notes.value(Editing::noteIdAt(editor.score(), editor.cursor())).fret, 9);

        editor.undo();
        QCOMPARE(fingerprint(editor.score()), withThree);
    }

    void deletingANoteAndUndoingItRestoresItWhereItWas()
    {
        Editor editor;
        editor.setScore(twoBars());

        // A chord, so that position within the beat matters.
        editor.setCursor(at(0, 0, 0));
        editor.typeDigit(3);
        editor.setCursor(at(0, 0, 1));
        editor.typeDigit(5);
        editor.setCursor(at(0, 0, 2));
        editor.typeDigit(5);

        const QString chord = fingerprint(editor.score());
        editor.setCursor(at(0, 0, 1));
        editor.clearNote();
        QVERIFY(fingerprint(editor.score()) != chord);

        editor.undo();
        QCOMPARE(fingerprint(editor.score()), chord);
    }

    /** Everything, undone, is where it started; redone, where it got to. */
    void aWholeSessionUndoesAndRedoesExactly()
    {
        Editor editor;
        editor.setScore(twoBars());
        const QString empty = fingerprint(editor.score());

        editor.setCursor(at(0, 0, 5));
        editor.typeDigit(1);
        editor.typeDigit(2);
        editor.setCursor(at(0, 1, 4));
        editor.typeDigit(9);
        editor.setCursor(at(1, 2, 0));
        editor.typeDigit(3);
        editor.transposeNote(2);
        editor.setDuration(8);
        editor.toggleDot();
        editor.setCursor(at(1, 3, 0));
        editor.scaleDuration(-1);
        editor.setCursor(at(0, 2, 2));
        editor.insertBeat();
        editor.typeDigit(8);
        editor.setCursor(at(1, 0, 0));
        editor.deleteBeat();
        editor.setCursor(at(0, 0, 5));
        editor.clearNote();

        const QString edited = fingerprint(editor.score());
        QVERIFY(edited != empty);

        int steps = 0;
        while (editor.canUndo()) {
            editor.undo();
            QVERIFY2(++steps < 100, "undo is not making progress");
        }
        QCOMPARE(fingerprint(editor.score()), empty);

        while (editor.canRedo()) {
            editor.redo();
        }
        QCOMPARE(fingerprint(editor.score()), edited);
    }

    // ---- how long a beat lasts ----

    void setsHowLongABeatLastsAndPutsItBack()
    {
        Editor editor;
        editor.setScore(twoBars());
        editor.setCursor(at(0, 1, 0));
        QCOMPARE(Editing::durationAt(editor.score(), editor.cursor()), Rational(1));

        editor.setDuration(8);
        QCOMPARE(Editing::durationAt(editor.score(), editor.cursor()), Rational(1, 2));

        // The beat keeps everything else: a quaver where a crotchet was is the
        // same fingering held for less time.
        editor.undo();
        QCOMPARE(Editing::durationAt(editor.score(), editor.cursor()), Rational(1));
    }

    void addsADotAndTakesItAway()
    {
        Editor editor;
        editor.setScore(twoBars());
        editor.setCursor(at(0, 0, 0));

        editor.toggleDot();
        QCOMPARE(Editing::durationAt(editor.score(), editor.cursor()), Rational(3, 2));
        editor.toggleDot();
        QCOMPARE(Editing::durationAt(editor.score(), editor.cursor()), Rational(1));

        // Two commands, two undos: dotting and undotting are separate acts.
        editor.undo();
        QCOMPARE(Editing::durationAt(editor.score(), editor.cursor()), Rational(3, 2));
        editor.undo();
        QCOMPARE(Editing::durationAt(editor.score(), editor.cursor()), Rational(1));
    }

    /** Halving keeps the dots: a dotted crotchet halves to a dotted quaver. */
    void halvesAndDoublesAndStopsAtTheEnds()
    {
        Editor editor;
        editor.setScore(twoBars());
        editor.setCursor(at(0, 0, 0));

        editor.toggleDot();
        editor.scaleDuration(-1);
        QCOMPARE(Editing::durationAt(editor.score(), editor.cursor()), Rational(3, 4));
        editor.scaleDuration(1);
        QCOMPARE(Editing::durationAt(editor.score(), editor.cursor()), Rational(3, 2));

        // Off the end in either direction stops rather than wrapping or
        // pushing on into durations nobody writes.
        editor.setCursor(at(0, 1, 0));
        editor.scaleDuration(-20);
        QCOMPARE(Editing::durationAt(editor.score(), editor.cursor()), NoteValue::Shortest);
        editor.scaleDuration(20);
        QCOMPARE(Editing::durationAt(editor.score(), editor.cursor()), NoteValue::Longest);
    }

    /** Durations are deduplicated, the way gpif stores them in the first place. */
    void reusesADurationRatherThanAddingItAgain()
    {
        Editor editor;
        editor.setScore(twoBars());
        const int before = int(editor.score().rhythms.size());

        editor.setCursor(at(0, 0, 0));
        editor.setDuration(8);
        editor.setCursor(at(0, 1, 0));
        editor.setDuration(8);

        QCOMPARE(int(editor.score().rhythms.size()), before + 1);
        QCOMPARE(editor.score().beats.value(0).rhythm, editor.score().beats.value(1).rhythm);
    }

    void asksForNothingThatIsNotANoteValue()
    {
        Editor editor;
        editor.setScore(twoBars());
        editor.setCursor(at(0, 0, 0));

        editor.setDuration(6);
        editor.setDuration(128);
        QVERIFY(!editor.canUndo());
        QCOMPARE(Editing::durationAt(editor.score(), editor.cursor()), Rational(1));

        // Setting the duration it already has is not a change, and does not
        // put a step in the history that appears to do nothing.
        editor.setDuration(4);
        QVERIFY(!editor.canUndo());
    }

    void transposingMovesTheFretAndThePitchTogether()
    {
        Editor editor;
        editor.setScore(twoBars());
        editor.setCursor(at(0, 0, 0));
        editor.typeDigit(5);

        editor.transposeNote(2);
        const Note note = editor.score().notes.value(Editing::noteIdAt(editor.score(), editor.cursor()));
        QCOMPARE(note.fret, 7);
        QCOMPARE(note.midi, 40 + 7);

        // Off the end of the neck, or behind the nut, is refused rather than
        // wrapped around.
        editor.transposeNote(-99);
        QCOMPARE(editor.score().notes.value(Editing::noteIdAt(editor.score(), editor.cursor())).fret, 7);
    }

    void knowsWhetherItHasBeenChanged()
    {
        Editor editor;
        editor.setScore(twoBars());
        QVERIFY(!editor.isModified());

        editor.setCursor(at(0, 0, 0));
        editor.typeDigit(4);
        QVERIFY(editor.isModified());

        editor.undo();
        QVERIFY(!editor.isModified());

        editor.redo();
        QVERIFY(editor.isModified());
        editor.setUnmodified();
        QVERIFY(!editor.isModified());
    }

    void aNewScoreForgetsTheHistoryOfTheOldOne()
    {
        Editor editor;
        editor.setScore(twoBars());
        editor.setCursor(at(0, 0, 0));
        editor.typeDigit(4);
        QVERIFY(editor.canUndo());

        editor.setScore(twoBars());
        QVERIFY(!editor.canUndo());
        QVERIFY(!editor.isModified());
    }

    // ---- beats ----

    /**
     * Typing one past the end of a voice writes a beat as well as a note.
     *
     * This is how music gets added to the end of a piece, and the caret is
     * allowed to sit there precisely so that it can be. One undo takes both
     * away, because typing a number was one act however many things it had to
     * make happen.
     */
    void typingPastTheLastBeatWritesOneAndUndoesInOneStep()
    {
        Editor editor;
        editor.setScore(twoBars());
        const QString before = fingerprint(editor.score());

        editor.setCursor(at(1, 4, 0));
        QCOMPARE(Editing::beatCount(editor.score(), editor.cursor()), 4);

        editor.typeDigit(5);
        QCOMPARE(Editing::beatCount(editor.score(), editor.cursor()), 5);
        QCOMPARE(editor.score().notes.value(Editing::noteIdAt(editor.score(),
                                                              editor.cursor())).fret, 5);

        editor.undo();
        QCOMPARE(Editing::beatCount(editor.score(), editor.cursor()), 4);
        QCOMPARE(fingerprint(editor.score()), before);
        QVERIFY(!editor.canUndo());
    }

    /** Two digits are one number there too, and still one undo. */
    void twoDigitsPastTheEndAreStillOneBeatAndOneUndo()
    {
        Editor editor;
        editor.setScore(twoBars());
        editor.setCursor(at(1, 4, 0));

        editor.typeDigit(1);
        editor.typeDigit(2);
        QCOMPARE(Editing::beatCount(editor.score(), editor.cursor()), 5);
        QCOMPARE(editor.score().notes.value(Editing::noteIdAt(editor.score(),
                                                              editor.cursor())).fret, 12);

        editor.undo();
        QCOMPARE(Editing::beatCount(editor.score(), editor.cursor()), 4);
        QVERIFY(!editor.canUndo());
    }

    /** A bar with no voice at all gets one, and undo leaves it empty again. */
    void writesIntoAnEmptyBar()
    {
        Score score = twoBars();
        score.bars[1] = Bar{{-1, -1, -1, -1}};

        Editor editor;
        editor.setScore(score);
        editor.setCursor(at(1, 0, 2));
        QCOMPARE(Editing::voiceIdAt(editor.score(), editor.cursor()), -1);

        editor.typeDigit(7);
        QVERIFY(Editing::voiceIdAt(editor.score(), editor.cursor()) >= 0);
        QCOMPARE(Editing::beatCount(editor.score(), editor.cursor()), 1);

        editor.undo();
        QCOMPARE(Editing::voiceIdAt(editor.score(), editor.cursor()), -1);
        QCOMPARE(int(editor.score().voices.size()), 2);
    }

    void insertsAnEmptyBeatAndPushesTheRestAlong()
    {
        Editor editor;
        editor.setScore(twoBars());
        editor.setCursor(at(0, 0, 0));
        editor.typeDigit(3);
        editor.setCursor(at(0, 1, 0));
        editor.typeDigit(5);

        editor.setCursor(at(0, 1, 0));
        editor.insertBeat();
        QCOMPARE(Editing::beatCount(editor.score(), editor.cursor()), 5);

        // The caret is on the new beat, which has nothing on it; the 5 has
        // moved along and is where the caret was going to be.
        QCOMPARE(Editing::noteIdAt(editor.score(), editor.cursor()), -1);
        QCOMPARE(editor.score().notes.value(
                     Editing::noteIdAt(editor.score(), at(0, 2, 0))).fret, 5);
        QCOMPARE(editor.score().notes.value(
                     Editing::noteIdAt(editor.score(), at(0, 0, 0))).fret, 3);

        editor.undo();
        QCOMPARE(Editing::beatCount(editor.score(), editor.cursor()), 4);
        QCOMPARE(editor.score().notes.value(
                     Editing::noteIdAt(editor.score(), at(0, 1, 0))).fret, 5);
    }

    /** A bar of quavers wants another quaver, not a crotchet and a warning. */
    void aNewBeatLastsAsLongAsTheOneItPushesAlong()
    {
        Editor editor;
        editor.setScore(twoBars());
        editor.setCursor(at(0, 2, 0));
        editor.setDuration(8);

        editor.insertBeat();
        QCOMPARE(Editing::durationAt(editor.score(), editor.cursor()), Rational(1, 2));

        // At the end of a voice there is nothing to displace, so it takes the
        // length of the beat before it.
        editor.setCursor(at(0, 5, 0));
        editor.insertBeat();
        QCOMPARE(Editing::durationAt(editor.score(), at(0, 5, 0)), Rational(1));
    }

    void deletesABeatAndPutsItBackWithItsNotes()
    {
        Editor editor;
        editor.setScore(twoBars());
        editor.setCursor(at(0, 1, 0));
        editor.typeDigit(7);
        editor.setCursor(at(0, 1, 3));
        editor.typeDigit(9);
        editor.setCursor(at(0, 2, 0));
        editor.typeDigit(4);
        const QString before = fingerprint(editor.score());

        editor.setCursor(at(0, 1, 0));
        editor.deleteBeat();
        QCOMPARE(Editing::beatCount(editor.score(), editor.cursor()), 3);
        // The chord is gone rather than orphaned in the note table.
        QCOMPARE(int(editor.score().notes.size()), 1);
        QCOMPARE(editor.score().notes.value(
                     Editing::noteIdAt(editor.score(), at(0, 1, 0))).fret, 4);

        editor.undo();
        QCOMPARE(fingerprint(editor.score()), before);
    }

    void deletingTheLastBeatLeavesTheCaretSomewhereReal()
    {
        Editor editor;
        editor.setScore(twoBars());
        editor.setCursor(at(0, 3, 0));
        editor.deleteBeat();

        QCOMPARE(Editing::beatCount(editor.score(), editor.cursor()), 3);
        QVERIFY(editor.cursor().beat <= 3);
        QVERIFY(Editing::clamped(editor.score(), editor.cursor()) == editor.cursor());
    }

    void doesNothingWhereThereIsNowhereToPutABeat()
    {
        Editor editor;
        editor.setScore(Score());

        editor.typeDigit(5);
        editor.insertBeat();
        editor.deleteBeat();
        QVERIFY(!editor.canUndo());

        // A master bar that names no bar for this track. The caret cannot be
        // put anywhere invalid -- it clamps -- so this is the only way to be
        // pointing at a place a beat cannot go, and it comes from a file
        // rather than from anything a user did.
        Score hollow = twoBars();
        hollow.masterBars[0].bars = {};

        Editor second;
        second.setScore(hollow);
        second.setCursor(at(0, 0, 0));
        second.typeDigit(5);
        second.insertBeat();
        QVERIFY(!second.canUndo());
    }
};

QTEST_GUILESS_MAIN(EditorTest)
#include "editortest.moc"
