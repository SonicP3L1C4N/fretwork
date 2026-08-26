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
        return someBars(2);
    }

    /** The same, as many bars and as many tracks as a test needs. */
    static Score someBars(int count, int trackCount = 1)
    {
        Score score;
        for (int track = 0; track < trackCount; ++track) {
            Track guitar;
            guitar.name = QStringLiteral("Guitar %1").arg(track + 1);
            guitar.instrumentType = QStringLiteral("electricGuitar");
            guitar.tuning = {40, 45, 50, 55, 59, 64};
            score.tracks.append(guitar);
        }
        score.rhythms.insert(0, Rational(1));

        int beatId = 0;
        int barId = 0;
        for (int bar = 0; bar < count; ++bar) {
            MasterBar master;
            for (int track = 0; track < trackCount; ++track) {
                QList<int> beats;
                for (int beat = 0; beat < 4; ++beat) {
                    score.beats.insert(beatId, Beat{0, {}, Dynamic::F, false, false});
                    beats.append(beatId);
                    ++beatId;
                }
                // A voice per bar of per track, numbered alongside the bars
                // they belong to, which is close enough to what an importer
                // produces to be worth testing against.
                score.voices.insert(barId, Voice{beats});
                score.bars.insert(barId, Bar{{barId, -1, -1, -1}});
                master.bars.append(barId);
                ++barId;
            }
            score.masterBars.append(master);
        }
        return score;
    }

    /** Everything about a score that an edit could change, as comparable text. */
    static QString fingerprint(const Score &score)
    {
        QStringList out;
        for (int bar = 0; bar < score.masterBars.size(); ++bar) {
            const MasterBar &master = score.masterBars.at(bar);
            // The bar itself and not only what is in it: an edit that added a
            // bar, took one away, or changed what one is worth has to show up
            // here too, or the reversal tests are agreeing with a bug.
            out.append(QStringLiteral("[%1]%2/%3%4")
                           .arg(bar)
                           .arg(master.numerator)
                           .arg(master.denominator)
                           .arg(master.section));
            for (const int barId : master.bars) {
                for (const int voiceId : score.bars.value(barId).voices) {
                    if (voiceId < 0) {
                        continue;
                    }
                    for (const int beatId : score.voices.value(voiceId).beats) {
                        QStringList notes;
                        for (const int noteId : score.beats.value(beatId).notes) {
                            const Note note = score.notes.value(noteId);
                            notes.append(QStringLiteral("s%1f%2m%3%4")
                                             .arg(note.string)
                                             .arg(note.fret)
                                             .arg(note.midi)
                                             .arg(marksOf(note)));
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
        }
        // And the tempo map, because a bar is where a tempo change lives: one
        // left pointing at the index it used to have is a tempo that moved.
        for (const TempoChange &tempo : score.tempos) {
            out.append(QStringLiteral("t%1@%2:%3")
                           .arg(tempo.bar)
                           .arg(tempo.position)
                           .arg(tempo.quarterBpm));
        }
        return out.join(QLatin1Char('|'));
    }

    /**
     * The marks on a note, as letters.
     *
     * In the fingerprint because a mark is a change to the music like any
     * other: an edit that palm-muted four bars and an undo that only appeared
     * to take it off would agree with each other perfectly if nothing here
     * could see it.
     */
    static QString marksOf(const Note &note)
    {
        QString out;
        const QList<QPair<bool, char>> flags = {
            {note.muted, 'x'},          {note.ghost, 'g'},
            {note.palmMuted, 'p'},      {note.letRing, 'l'},
            {note.accent, 'a'},         {note.vibrato, 'v'},
            {note.hammerOrigin, 'h'},   {note.hammerDestination, 'H'},
            {note.tieOrigin, 't'},      {note.tieDestination, 'T'},
            {note.tapped, 'k'},         {note.harmonic, 'r'},
            {note.bended, 'b'},         {note.slide != SlideType::None, 's'},
        };
        for (const auto &flag : flags) {
            if (flag.first) {
                out.append(QLatin1Char(flag.second));
            }
        }
        return out;
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
        editor.transpose(2);
        editor.setDuration(8);
        editor.toggleDot();
        editor.setCursor(at(1, 3, 0));
        editor.scaleDuration(-1);
        editor.setCursor(at(0, 2, 2));
        editor.insertBeat();
        editor.typeDigit(8);
        editor.setCursor(at(1, 0, 0));
        editor.deleteBeat();
        editor.setCursor(at(0, 1, 0));
        editor.setCursor(at(0, 3, 0), true);
        editor.cut();
        editor.setCursor(at(1, 1, 0));
        editor.paste();
        editor.setCursor(at(0, 0, 5));
        editor.clearNote();
        editor.setCursor(at(0, 1, 4));
        editor.moveNoteAcross(-1);
        editor.setCursor(at(0, 1, 3));
        editor.toggleMark(Editor::Mark::PalmMute);
        editor.setCursor(at(1, 2, 0));
        editor.toggleMark(Editor::Mark::Dead);
        editor.setCursor(at(1, 0, 0));
        editor.insertBar();
        editor.typeDigit(6);
        editor.appendBar();
        editor.typeDigit(4);
        editor.setCursor(at(0, 0, 0));
        editor.deleteBar();

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

        QCOMPARE(editor.transpose(2), Editor::Edit::Done);
        const Note note = editor.score().notes.value(Editing::noteIdAt(editor.score(), editor.cursor()));
        QCOMPARE(note.fret, 7);
        QCOMPARE(note.midi, 40 + 7);

        // Off the end of the neck, or behind the nut, is refused rather than
        // wrapped around.
        QCOMPARE(editor.transpose(-99), Editor::Edit::Refused);
        QCOMPARE(editor.score().notes.value(Editing::noteIdAt(editor.score(), editor.cursor())).fret, 7);

        // An empty string is not a refusal: there is simply nothing there.
        editor.setCursor(at(0, 1, 3));
        QCOMPARE(editor.transpose(1), Editor::Edit::Nothing);
        QVERIFY(!editor.score().notes.contains(Editing::noteIdAt(editor.score(), at(0, 1, 3))));
    }

    /** A phrase is transposed as a phrase, on every string it uses. */
    void transposingASelectionMovesEveryNoteInIt()
    {
        Editor editor;
        editor.setScore(twoBars());
        editor.setCursor(at(0, 0, 0));
        editor.typeDigit(5);
        editor.setCursor(at(0, 0, 2));
        editor.typeDigit(7);
        editor.setCursor(at(1, 1, 0));
        editor.typeDigit(3);
        const QString before = fingerprint(editor.score());

        editor.setCursor(at(0, 0, 0));
        editor.setCursor(at(1, 1, 0), true);
        QCOMPARE(editor.transpose(2), Editor::Edit::Done);

        QCOMPARE(editor.score().notes.value(Editing::noteIdAt(editor.score(), at(0, 0, 0))).fret, 7);
        QCOMPARE(editor.score().notes.value(Editing::noteIdAt(editor.score(), at(0, 0, 2))).fret, 9);
        QCOMPARE(editor.score().notes.value(Editing::noteIdAt(editor.score(), at(1, 1, 0))).fret, 5);
        QCOMPARE(editor.score().notes.value(Editing::noteIdAt(editor.score(), at(1, 1, 0))).midi,
                 40 + 5);

        // The selection is the thing being worked on, so it stays: a phrase
        // can be walked up a fret at a time.
        QVERIFY(editor.hasSelection());

        // And one press of undo puts the whole phrase back.
        editor.undo();
        QCOMPARE(fingerprint(editor.score()), before);
    }

    /**
     * A phrase with one note left behind is not the phrase that was asked
     * for, and it looks like it worked.
     */
    void refusesATranspositionThatWouldStrandANote()
    {
        Editor editor;
        editor.setScore(twoBars());
        editor.setCursor(at(0, 0, 0));
        editor.typeDigit(5);
        editor.setCursor(at(0, 1, 0));
        editor.typeDigit(1);
        const QString before = fingerprint(editor.score());
        const int history = editor.undoStack()->index();

        editor.setCursor(at(0, 0, 0));
        editor.setCursor(at(0, 1, 0), true);
        QCOMPARE(editor.transpose(-3), Editor::Edit::Refused);
        QCOMPARE(fingerprint(editor.score()), before);
        // Nothing was pushed, so there is nothing to undo back out of.
        QCOMPARE(editor.undoStack()->index(), history);
    }

    /** Which string a note is played on is a fingering, not a change of music. */
    void movingANoteAcrossKeepsThePitchAndChangesTheFret()
    {
        Editor editor;
        editor.setScore(twoBars());
        editor.setCursor(at(0, 0, 0));
        editor.typeDigit(5);
        const QString before = fingerprint(editor.score());

        // The low E at fret 5 is an A, which is the open A string above it.
        QCOMPARE(editor.moveNoteAcross(1), Editor::Edit::Done);
        QCOMPARE(editor.cursor().string, 1);
        const Note moved = editor.score().notes.value(Editing::noteIdAt(editor.score(), at(0, 0, 1)));
        QCOMPARE(moved.fret, 0);
        QCOMPARE(moved.midi, 45);

        editor.undo();
        QCOMPARE(fingerprint(editor.score()), before);
    }

    void refusesAStringMoveWithNowhereToLand()
    {
        Editor editor;
        editor.setScore(twoBars());

        // Fret 1 on the low E cannot be played on the A string: it is behind
        // that string's nut.
        editor.setCursor(at(0, 0, 0));
        editor.typeDigit(1);
        QCOMPARE(editor.moveNoteAcross(1), Editor::Edit::Refused);

        // Nor off the top of the neck.
        editor.setCursor(at(0, 1, 5));
        editor.typeDigit(1);
        QCOMPARE(editor.moveNoteAcross(1), Editor::Edit::Refused);

        // Nor on to a string that is already sounding: two notes on one string
        // at one moment is not a chord, it is a mistake.
        editor.setCursor(at(0, 2, 0));
        editor.typeDigit(7);
        editor.setCursor(at(0, 2, 1));
        editor.typeDigit(0);
        editor.setCursor(at(0, 2, 0));
        QCOMPARE(editor.moveNoteAcross(1), Editor::Edit::Refused);
        QVERIFY(editor.canUndo());
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

    // ---- selection and the clipboard ----

    /** The beat the caret was on is part of what shift selects, not the next one. */
    void shiftAndAnArrowSelectsFromWhereTheCaretWas()
    {
        Editor editor;
        editor.setScore(twoBars());
        editor.setCursor(at(0, 1, 0));
        QVERIFY(!editor.hasSelection());

        editor.move(Editing::Move::Right, true);
        QVERIFY(editor.hasSelection());
        QCOMPARE(editor.selection().from.beat, 1);
        QCOMPARE(editor.selection().to.beat, 2);

        // Backwards past the start: the range comes out in score order
        // whichever way round it was made.
        editor.move(Editing::Move::Left, true);
        editor.move(Editing::Move::Left, true);
        QCOMPARE(editor.selection().from.beat, 0);
        QCOMPARE(editor.selection().to.beat, 1);

        // Moving without shift is the end of it.
        editor.move(Editing::Move::Right);
        QVERIFY(!editor.hasSelection());
    }

    /** A selection is one voice of one track. Leaving either ends it. */
    void leavingTheVoiceEndsTheSelection()
    {
        Editor editor;
        editor.setScore(twoBars());
        editor.setCursor(at(0, 0, 0));
        editor.move(Editing::Move::Right, true);
        QVERIFY(editor.hasSelection());

        Cursor elsewhere = editor.cursor();
        elsewhere.voice = 1;
        editor.setCursor(elsewhere, true);
        QVERIFY(!editor.hasSelection());
    }

    void copiesAndPastesABeatWithItsNotes()
    {
        Editor editor;
        editor.setScore(twoBars());
        editor.setCursor(at(0, 0, 2));
        editor.typeDigit(7);
        editor.setDuration(8);

        editor.copy();
        editor.setCursor(at(1, 0, 0));
        QVERIFY(editor.paste());

        // A new beat with a new note on it, sounding the same and lasting the
        // same, and the bar it went into is a beat longer.
        QCOMPARE(Editing::beatCount(editor.score(), editor.cursor()), 5);
        QCOMPARE(Editing::durationAt(editor.score(), at(1, 0, 0)), Rational(1, 2));
        Cursor pasted = at(1, 0, 2);
        QCOMPARE(editor.score().notes.value(
                     Editing::noteIdAt(editor.score(), pasted)).fret, 7);

        // Not the same note: the original is untouched by anything done to it.
        QCOMPARE(int(editor.score().notes.size()), 2);
    }

    /** Four bars copied are four bars pasted, not one bar of sixteen beats. */
    void pasteKeepsTheBarsItWasCopiedFrom()
    {
        Editor editor;
        editor.setScore(someBars(6));
        editor.setCursor(at(0, 0, 0));
        editor.typeDigit(1);
        editor.setCursor(at(1, 0, 0));
        editor.typeDigit(2);

        editor.setCursor(at(0, 0, 0));
        editor.setCursor(at(1, 3, 0), true);
        QCOMPARE(editor.selection().barCount(), 2);
        editor.copy();

        editor.setCursor(at(3, 0, 0));
        QVERIFY(editor.paste());

        QCOMPARE(Editing::beatCount(editor.score(), at(3, 0, 0)), 8);
        QCOMPARE(Editing::beatCount(editor.score(), at(4, 0, 0)), 8);
        QCOMPARE(editor.score().notes.value(
                     Editing::noteIdAt(editor.score(), at(3, 0, 0))).fret, 1);
        QCOMPARE(editor.score().notes.value(
                     Editing::noteIdAt(editor.score(), at(4, 0, 0))).fret, 2);
    }

    /** Half a paste is worse than none: the half that landed has to be found. */
    void refusesAPasteThatWouldRunOffTheEnd()
    {
        Editor editor;
        editor.setScore(someBars(3));
        editor.setCursor(at(0, 0, 0));
        editor.setCursor(at(1, 3, 0), true);
        editor.copy();

        editor.setCursor(at(2, 0, 0));
        const QString before = fingerprint(editor.score());
        QVERIFY(!editor.paste());
        QCOMPARE(fingerprint(editor.score()), before);
        QVERIFY(!editor.canUndo());
    }

    void deletingASelectionTakesEveryBeatInIt()
    {
        Editor editor;
        editor.setScore(someBars(3));
        editor.setCursor(at(0, 2, 0));
        editor.typeDigit(5);
        const QString before = fingerprint(editor.score());

        editor.setCursor(at(0, 2, 0));
        editor.setCursor(at(1, 1, 0), true);
        editor.deleteSelection();

        // Two beats gone from the first bar and two from the second, and the
        // note that was on one of them with them.
        QCOMPARE(Editing::beatCount(editor.score(), at(0, 0, 0)), 2);
        QCOMPARE(Editing::beatCount(editor.score(), at(1, 0, 0)), 2);
        QCOMPARE(int(editor.score().notes.size()), 0);
        QVERIFY(!editor.hasSelection());

        // Back exactly as it was, ids and all.
        editor.undo();
        QCOMPARE(fingerprint(editor.score()), before);
    }

    void cutTakesWhatItCopied()
    {
        Editor editor;
        editor.setScore(someBars(3));
        editor.setCursor(at(0, 1, 1));
        editor.typeDigit(9);

        editor.setCursor(at(0, 1, 1));
        editor.setCursor(at(0, 2, 1), true);
        editor.cut();
        QCOMPARE(Editing::beatCount(editor.score(), at(0, 0, 0)), 2);

        editor.setCursor(at(2, 0, 0));
        QVERIFY(editor.paste());
        QCOMPARE(Editing::beatCount(editor.score(), at(2, 0, 0)), 6);
        QCOMPARE(editor.score().notes.value(
                     Editing::noteIdAt(editor.score(), at(2, 0, 1))).fret, 9);
    }

    /** A missed selection should not lose what was on the clipboard. */
    void copyingFromAnEmptyPlaceKeepsTheLastCopy()
    {
        Editor editor;
        editor.setScore(twoBars());
        editor.setCursor(at(0, 0, 0));
        editor.typeDigit(4);
        editor.copy();

        Score hollow = twoBars();
        hollow.bars[1] = Bar{{-1, -1, -1, -1}};
        editor.setScore(hollow);
        editor.setCursor(at(1, 0, 0));
        editor.copy();

        QVERIFY(editor.canPaste());
        QCOMPARE(editor.clip().beatCount(), 1);
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

    // ---- bars ----

    /** A bar made room for is a bar in every track, or the tracks drift apart. */
    void insertingABarMakesOneInEveryTrack()
    {
        Editor editor;
        editor.setScore(someBars(3, 2));
        editor.setCursor(at(1, 0, 0));
        const QString before = fingerprint(editor.score());

        editor.insertBar();
        QCOMPARE(int(editor.score().masterBars.size()), 4);
        QCOMPARE(int(editor.score().masterBars.at(1).bars.size()), 2);

        // Empty in both tracks, and the music that was in bar 1 is in bar 2.
        for (int track = 0; track < 2; ++track) {
            Cursor made = at(1, 0, 0);
            made.track = track;
            QCOMPARE(Editing::beatCount(editor.score(), made), 0);
            Cursor moved = at(2, 0, 0);
            moved.track = track;
            QCOMPARE(Editing::beatCount(editor.score(), moved), 4);
        }

        editor.undo();
        QCOMPARE(fingerprint(editor.score()), before);
    }

    /** A bar added to a piece in 6/8 is in 6/8. */
    void aNewBarIsWorthWhatTheOneItDisplacedWas()
    {
        Score score = someBars(2);
        score.masterBars[1].numerator = 6;
        score.masterBars[1].denominator = 8;
        score.masterBars[1].section = QStringLiteral("Chorus");
        score.masterBars[1].repeatStart = true;

        Editor editor;
        editor.setScore(score);
        editor.setCursor(at(1, 0, 0));
        editor.insertBar();

        QCOMPARE(editor.score().masterBars.at(1).numerator, 6);
        QCOMPARE(editor.score().masterBars.at(1).denominator, 8);
        // The name and the repeat sign were written on that bar, not on the
        // moment before it: a chorus that starts a bar early is worse than one
        // that has to be typed again.
        QVERIFY(editor.score().masterBars.at(1).section.isEmpty());
        QVERIFY(!editor.score().masterBars.at(1).repeatStart);
        QCOMPARE(editor.score().masterBars.at(2).section, QStringLiteral("Chorus"));
        QVERIFY(editor.score().masterBars.at(2).repeatStart);
    }

    /** A tempo change is written in a bar, so it goes where that bar goes. */
    void insertingABarMovesTheTempoChangesAfterIt()
    {
        Score score = someBars(3);
        score.tempos = {TempoChange{0, 0, 120}, TempoChange{2, 0, 90}};

        Editor editor;
        editor.setScore(score);
        editor.setCursor(at(1, 0, 0));
        const QString before = fingerprint(editor.score());

        editor.insertBar();
        QCOMPARE(editor.score().tempos.at(0).bar, 0);
        QCOMPARE(editor.score().tempos.at(1).bar, 3);

        editor.undo();
        QCOMPARE(fingerprint(editor.score()), before);
    }

    /** Which is how music reaches the end of a piece and then goes past it. */
    void aBarAddedAtTheEndCanBeWrittenInto()
    {
        Editor editor;
        editor.setScore(someBars(2));
        editor.setCursor(at(0, 0, 0));
        const QString before = fingerprint(editor.score());

        editor.appendBar();
        QCOMPARE(int(editor.score().masterBars.size()), 3);
        // The caret went with it: a bar added at the end is one somebody is
        // about to write in.
        QCOMPARE(editor.cursor().bar, 2);
        QCOMPARE(editor.cursor().beat, 0);

        editor.typeDigit(7);
        QCOMPARE(editor.score().notes.value(
                     Editing::noteIdAt(editor.score(), at(2, 0, 0))).fret, 7);

        editor.undo();
        editor.undo();
        QCOMPARE(fingerprint(editor.score()), before);
    }

    void deletingABarTakesItsMusicWithIt()
    {
        Editor editor;
        editor.setScore(someBars(3, 2));
        editor.setCursor(at(1, 2, 3));
        editor.typeDigit(5);
        const QString before = fingerprint(editor.score());
        const int notesBefore = int(editor.score().notes.size());

        editor.setCursor(at(1, 0, 0));
        editor.deleteBar();

        QCOMPARE(int(editor.score().masterBars.size()), 2);
        QCOMPARE(int(editor.score().notes.size()), notesBefore - 1);
        // Both tracks lost their share of it, and nothing was left in the
        // tables pointing at a bar that is no longer in the score.
        QCOMPARE(int(editor.score().bars.size()), 4);
        QCOMPARE(int(editor.score().voices.size()), 4);
        QCOMPARE(int(editor.score().beats.size()), 16);

        // Back exactly: the same bars, the same ids, the same note on it.
        editor.undo();
        QCOMPARE(fingerprint(editor.score()), before);
        QCOMPARE(editor.score().notes.value(
                     Editing::noteIdAt(editor.score(), at(1, 2, 3))).fret, 5);
    }

    /** A score with no bars is not a shorter score. */
    void theLastBarOfAScoreIsKept()
    {
        Editor editor;
        editor.setScore(someBars(1));
        editor.setCursor(at(0, 0, 0));
        const QString before = fingerprint(editor.score());

        QVERIFY(!editor.canDeleteBar());
        editor.deleteBar();
        QCOMPARE(fingerprint(editor.score()), before);
        QVERIFY(!editor.canUndo());
    }

    /**
     * Losing the tempo a piece starts at would not shorten it: it would
     * silently re-time all of it.
     */
    void deletingABarKeepsTheTempoTheScoreStartsAt()
    {
        Score score = someBars(3);
        score.tempos = {TempoChange{0, 0, 90}, TempoChange{2, 0, 140}};

        Editor editor;
        editor.setScore(score);
        editor.setCursor(at(0, 0, 0));
        const QString before = fingerprint(editor.score());

        editor.deleteBar();
        QCOMPARE(int(editor.score().tempos.size()), 2);
        QCOMPARE(editor.score().tempos.at(0).bar, 0);
        QCOMPARE(editor.score().tempos.at(0).quarterBpm, 90.0);
        QCOMPARE(editor.score().tempos.at(1).bar, 1);

        // A change written inside a deleted bar goes with it: it was made at a
        // moment that is no longer in the piece.
        editor.setCursor(at(1, 0, 0));
        editor.deleteBar();
        QCOMPARE(int(editor.score().tempos.size()), 1);
        QCOMPARE(editor.score().tempos.at(0).quarterBpm, 90.0);

        editor.undo();
        editor.undo();
        QCOMPARE(fingerprint(editor.score()), before);
    }

    /** The answer to a paste that would run off the end is more score. */
    void barsAddedAtTheEndMakeRoomForAPasteThatWasRefused()
    {
        Editor editor;
        editor.setScore(someBars(3));
        editor.setCursor(at(0, 0, 0));
        editor.typeDigit(4);
        editor.setCursor(at(0, 0, 0));
        editor.setCursor(at(1, 3, 0), true);
        editor.copy();

        editor.setCursor(at(2, 0, 0));
        QVERIFY(!editor.paste());

        editor.appendBar();
        editor.setCursor(at(2, 0, 0));
        QVERIFY(editor.paste());
        QCOMPARE(Editing::beatCount(editor.score(), at(2, 0, 0)), 8);
        QCOMPARE(Editing::beatCount(editor.score(), at(3, 0, 0)), 4);
    }

    void doesNothingWithABarWhereThereIsNoScore()
    {
        Editor editor;
        editor.setScore(Score());

        editor.insertBar();
        editor.appendBar();
        editor.deleteBar();
        QVERIFY(!editor.canUndo());
    }

    // ---- marks ----

    void marksTheNoteUnderTheCaretAndTakesItOffAgain()
    {
        Editor editor;
        editor.setScore(twoBars());
        editor.setCursor(at(0, 0, 0));
        editor.typeDigit(5);
        const QString before = fingerprint(editor.score());

        QCOMPARE(editor.toggleMark(Editor::Mark::PalmMute), Editor::Edit::Done);
        QVERIFY(editor.score().notes.value(Editing::noteIdAt(editor.score(), at(0, 0, 0))).palmMuted);

        // The second press means stop, not "flip it again and see".
        editor.toggleMark(Editor::Mark::PalmMute);
        QVERIFY(!editor.score().notes.value(Editing::noteIdAt(editor.score(), at(0, 0, 0))).palmMuted);

        editor.undo();
        editor.undo();
        QCOMPARE(fingerprint(editor.score()), before);

        // An empty string has nothing to mark, and that is not a refusal.
        editor.setCursor(at(0, 2, 3));
        QCOMPARE(editor.toggleMark(Editor::Mark::Dead), Editor::Edit::Nothing);
    }

    /**
     * A half-marked phrase gets finished rather than turned inside out, and
     * undoing it leaves alone the notes that were already marked.
     */
    void marksAWholeSelectionAndUndoesOnlyWhatItChanged()
    {
        Editor editor;
        editor.setScore(twoBars());
        editor.setCursor(at(0, 0, 0));
        editor.typeDigit(5);
        editor.setCursor(at(0, 1, 0));
        editor.typeDigit(7);
        editor.setCursor(at(0, 2, 0));
        editor.typeDigit(3);

        // The first of them is let ring already.
        editor.setCursor(at(0, 0, 0));
        editor.toggleMark(Editor::Mark::LetRing);

        editor.setCursor(at(0, 0, 0));
        editor.setCursor(at(0, 2, 0), true);
        editor.toggleMark(Editor::Mark::LetRing);
        for (int beat = 0; beat < 3; ++beat) {
            QVERIFY(editor.score().notes.value(
                        Editing::noteIdAt(editor.score(), at(0, beat, 0))).letRing);
        }
        // The selection stays: marking a phrase and marking it differently is
        // two edits to the same phrase.
        QVERIFY(editor.hasSelection());

        editor.undo();
        QVERIFY(editor.score().notes.value(Editing::noteIdAt(editor.score(), at(0, 0, 0))).letRing);
        QVERIFY(!editor.score().notes.value(Editing::noteIdAt(editor.score(), at(0, 1, 0))).letRing);
        QVERIFY(!editor.score().notes.value(Editing::noteIdAt(editor.score(), at(0, 2, 0))).letRing);

        // And now every one of them is marked, so the next press takes it off
        // all three.
        editor.redo();
        editor.toggleMark(Editor::Mark::LetRing);
        for (int beat = 0; beat < 3; ++beat) {
            QVERIFY(!editor.score().notes.value(
                        Editing::noteIdAt(editor.score(), at(0, beat, 0))).letRing);
        }
    }

    /** A number typed over a dead note means a note again. */
    void typingOverADeadNoteBringsItBack()
    {
        Editor editor;
        editor.setScore(twoBars());
        editor.setCursor(at(0, 0, 0));
        editor.typeDigit(5);
        editor.toggleMark(Editor::Mark::Dead);
        QVERIFY(editor.score().notes.value(Editing::noteIdAt(editor.score(), at(0, 0, 0))).muted);

        editor.typeDigit(7);
        const Note note = editor.score().notes.value(Editing::noteIdAt(editor.score(), at(0, 0, 0)));
        QVERIFY(!note.muted);
        QCOMPARE(note.fret, 7);
    }
};

QTEST_GUILESS_MAIN(EditorTest)
#include "editortest.moc"
