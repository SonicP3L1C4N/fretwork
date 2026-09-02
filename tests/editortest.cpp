// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "cursor.h"
#include "editor.h"
#include "notename.h"
#include "timeline.h"

#include <QSet>
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
            {note.tapped, 'k'},         {note.isHarmonic(), 'r'},
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

    // ---- recording ----

    void recordingABarReplacesItAndOneUndoPutsItBack()
    {
        Editor editor;
        Score score = twoBars();
        const QString before = fingerprint(score);
        editor.setScore(score);

        QList<Recorder::Beat> beats;
        beats.append(Recorder::Beat{Rational(1), {Fretboard::Position{5, 3}}, false});
        beats.append(Recorder::Beat{Rational(3), {}, false});
        QCOMPARE(editor.recordBar(0, 1, beats, false), Editor::Edit::Done);

        const Score &after = editor.score();
        const int voiceId = after.bars.value(after.masterBars.at(1).bars.at(0)).voices.at(0);
        QCOMPARE(after.voices.value(voiceId).beats.size(), 2);
        const int first = after.voices.value(voiceId).beats.at(0);
        QCOMPARE(after.beats.value(first).notes.size(), 1);
        const Note note = after.notes.value(after.beats.value(first).notes.first());
        QCOMPARE(note.string, 5);
        QCOMPARE(note.fret, 3);
        QCOMPARE(note.midi, 67);
        QCOMPARE(after.rhythms.value(after.beats.value(first).rhythm), Rational(1));
        const int second = after.voices.value(voiceId).beats.at(1);
        QVERIFY(after.beats.value(second).notes.isEmpty());
        QCOMPARE(after.rhythms.value(after.beats.value(second).rhythm), Rational(3));

        editor.undo();
        QCOMPARE(fingerprint(editor.score()), before);
    }

    void aBarStillBeingRecordedIsOneUndo()
    {
        Editor editor;
        Score score = twoBars();
        const QString before = fingerprint(score);
        editor.setScore(score);

        QList<Recorder::Beat> first;
        first.append(Recorder::Beat{Rational(4), {Fretboard::Position{5, 0}}, false});
        editor.recordBar(0, 0, first, false);
        QList<Recorder::Beat> second;
        second.append(Recorder::Beat{Rational(2), {Fretboard::Position{5, 0}}, false});
        second.append(Recorder::Beat{Rational(2), {Fretboard::Position{5, 3}}, false});
        editor.recordBar(0, 0, second, true);
        const QString built = fingerprint(editor.score());

        QCOMPARE(editor.undoStack()->count(), 1);
        editor.undo();
        QCOMPARE(fingerprint(editor.score()), before);
        editor.redo();
        QCOMPARE(fingerprint(editor.score()), built);
    }

    void aSecondBarIsASecondUndo()
    {
        Editor editor;
        editor.setScore(twoBars());

        QList<Recorder::Beat> beats;
        beats.append(Recorder::Beat{Rational(4), {Fretboard::Position{5, 0}}, false});
        editor.recordBar(0, 0, beats, false);
        const QString firstBar = fingerprint(editor.score());
        // Still "building" as far as the keys are concerned, but a different
        // bar: the merge is per bar, not per take.
        editor.recordBar(0, 1, beats, true);
        QCOMPARE(editor.undoStack()->count(), 2);

        editor.undo();
        QCOMPARE(fingerprint(editor.score()), firstBar);
    }

    void fragmentsOfOneNoteAreTied()
    {
        Editor editor;
        editor.setScore(twoBars());

        QList<Recorder::Beat> beats;
        beats.append(Recorder::Beat{Rational(1), {Fretboard::Position{5, 3}}, true});
        beats.append(Recorder::Beat{Rational(1, 4), {Fretboard::Position{5, 3}}, false});
        beats.append(Recorder::Beat{Rational(3, 4), {}, false});
        beats.append(Recorder::Beat{Rational(2), {}, false});
        QCOMPARE(editor.recordBar(0, 0, beats, false), Editor::Edit::Done);

        const Score &after = editor.score();
        const int voiceId = after.bars.value(after.masterBars.at(0).bars.at(0)).voices.at(0);
        const QList<int> ids = after.voices.value(voiceId).beats;
        QCOMPARE(ids.size(), 4);
        const Note head = after.notes.value(after.beats.value(ids.at(0)).notes.first());
        const Note tail = after.notes.value(after.beats.value(ids.at(1)).notes.first());
        QVERIFY(head.tieOrigin);
        QVERIFY(!head.tieDestination);
        QVERIFY(tail.tieDestination);
        QVERIFY(!tail.tieOrigin);
    }

    void recordingRefusesAStringThePartHasNot()
    {
        Editor editor;
        Score score = twoBars();
        const QString before = fingerprint(score);
        editor.setScore(score);

        QList<Recorder::Beat> beats;
        beats.append(Recorder::Beat{Rational(4), {Fretboard::Position{7, 0}}, false});
        QCOMPARE(editor.recordBar(0, 0, beats, false), Editor::Edit::Refused);
        QCOMPARE(fingerprint(editor.score()), before);
        QVERIFY(!editor.canUndo());
        QCOMPARE(editor.recordBar(0, 5, beats, false), Editor::Edit::Refused);
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

    // ---- tempo ----

    void setsTheTempoAtTheBarTheCaretIsIn()
    {
        Editor editor;
        editor.setScore(someBars(4));
        editor.setCursor(Cursor{0, 2, 0, 1});

        QCOMPARE(editor.setTempo(96), Editor::Edit::Done);
        QCOMPARE(editor.score().tempos.size(), 1);
        QCOMPARE(editor.score().tempos.first().bar, 2);
        // At the bar line, whatever beat the caret happened to be sitting on:
        // typing notes on the third beat is not a statement about where the
        // music changes speed.
        QCOMPARE(editor.score().tempos.first().position, 0.0);
        QCOMPARE(Timeline::tempoAtBar(editor.score(), 2), 96.0);
        QCOMPARE(Timeline::tempoAtBar(editor.score(), 3), 96.0);
        // And the bars before it are untouched, which is the whole point of a
        // change rather than a setting.
        QCOMPARE(Timeline::tempoAtBar(editor.score(), 1), 120.0);
    }

    void keepsTheChangesInTheOrderTheyArePlayed()
    {
        Editor editor;
        editor.setScore(someBars(6));
        for (const QPair<int, double> &wanted :
             QList<QPair<int, double>>({{4, 80}, {1, 140}, {2, 100}})) {
            editor.setCursor(Cursor{0, wanted.first, 0, 0});
            QCOMPARE(editor.setTempo(wanted.second), Editor::Edit::Done);
        }

        QList<int> bars;
        for (const TempoChange &tempo : editor.score().tempos) {
            bars.append(tempo.bar);
        }
        // Written out of order and kept in order: everything downstream walks
        // this list expecting the music's own order.
        QCOMPARE(bars, QList<int>({1, 2, 4}));
        QCOMPARE(Timeline::tempoAtBar(editor.score(), 3), 100.0);
    }

    void oneBarHasOneTempo()
    {
        Editor editor;
        editor.setScore(someBars(3));
        editor.setCursor(Cursor{1, 0, 0, 1});
        QCOMPARE(editor.setTempo(90), Editor::Edit::Done);
        QCOMPARE(editor.setTempo(150), Editor::Edit::Done);
        QCOMPARE(editor.score().tempos.size(), 1);
        QCOMPARE(Timeline::tempoAtBar(editor.score(), 1), 150.0);

        // Setting what is already set is nothing to do rather than an edit: a
        // step on the undo stack that undoes nothing visible is a step that
        // makes undo untrustworthy.
        QCOMPARE(editor.setTempo(150), Editor::Edit::Nothing);
    }

    void sweepsUpAChangeWrittenPartWayThroughTheBar()
    {
        Score score = someBars(3);
        // What an import can hand over and this editor will not create.
        score.tempos = {{1, 2.0, 60}};
        Editor editor;
        editor.setScore(score);
        editor.setCursor(Cursor{0, 1, 0, 0});
        QCOMPARE(editor.setTempo(100), Editor::Edit::Done);

        QCOMPARE(editor.score().tempos.size(), 1);
        QCOMPARE(editor.score().tempos.first().position, 0.0);
        QCOMPARE(editor.score().tempos.first().quarterBpm, 100.0);
    }

    void refusesATempoThatIsATypingMistake()
    {
        Editor editor;
        editor.setScore(someBars(2));
        editor.setCursor(Cursor{0, 1, 0, 0});
        QCOMPARE(editor.setTempo(1100), Editor::Edit::Refused);
        QCOMPARE(editor.setTempo(0), Editor::Edit::Refused);
        QCOMPARE(editor.setTempo(-40), Editor::Edit::Refused);
        // Refused and not clamped: quietly turning 1100 into 400 leaves
        // somebody looking for the tempo they typed.
        QVERIFY(editor.score().tempos.isEmpty());
    }

    void takesATempoOffAgainButNotTheFirstOne()
    {
        Score score = someBars(4);
        score.tempos = {{0, 0, 120}, {2, 0, 90}};
        Editor editor;
        editor.setScore(score);

        editor.setCursor(Cursor{0, 2, 0, 0});
        QVERIFY(editor.hasTempoHere());
        QCOMPARE(editor.clearTempo(), Editor::Edit::Done);
        QCOMPARE(Timeline::tempoAtBar(editor.score(), 3), 120.0);

        // Nothing there to take away is not a refusal.
        QCOMPARE(editor.clearTempo(), Editor::Edit::Nothing);

        // The first bar keeps one: there is nothing before it to inherit from,
        // so a score whose opening bar has no tempo is played at whatever the
        // default happens to be, which is not what taking a marking off means.
        editor.setCursor(Cursor{0, 0, 0, 0});
        QCOMPARE(editor.clearTempo(), Editor::Edit::Refused);
        QVERIFY(editor.hasTempoHere());
    }

    void undoingATempoPutsBackExactlyWhatWasThere()
    {
        Score score = someBars(5);
        score.tempos = {{0, 0, 120}, {3, 1.5, 200}};
        Editor editor;
        editor.setScore(score);

        editor.setCursor(Cursor{0, 3, 0, 0});
        QCOMPARE(editor.setTempo(75), Editor::Edit::Done);
        QCOMPARE(editor.score().tempos.size(), 2);
        editor.undo();

        // Including the mid-bar position the edit swept up, which a record of
        // one entry rather than the whole list would have lost.
        QCOMPARE(editor.score().tempos.size(), 2);
        QCOMPARE(editor.score().tempos.at(1).bar, 3);
        QCOMPARE(editor.score().tempos.at(1).position, 1.5);
        QCOMPARE(editor.score().tempos.at(1).quarterBpm, 200.0);
    }

    void readsTheTempoAtABarWhateverOrderTheyAreWrittenIn()
    {
        Score score = someBars(8);
        // An importer is not obliged to hand these over in order.
        score.tempos = {{5, 0, 60}, {0, 0, 120}, {2, 0, 90}};
        QCOMPARE(Timeline::tempoAtBar(score, 0), 120.0);
        QCOMPARE(Timeline::tempoAtBar(score, 1), 120.0);
        QCOMPARE(Timeline::tempoAtBar(score, 4), 90.0);
        QCOMPARE(Timeline::tempoAtBar(score, 7), 60.0);

        // Two in one bar: the later one is the one in force after it.
        score.tempos.append({2, 3.0, 200});
        QCOMPARE(Timeline::tempoAtBar(score, 2), 200.0);
    }

    // ---- time signature ----

    void aSignatureRunsUntilTheNextChange()
    {
        Editor editor;
        editor.setScore(someBars(8));
        editor.setCursor(Cursor{0, 3, 0, 0});
        QCOMPARE(editor.setTimeSignature(3, 4), Editor::Edit::Done);

        // From the caret's bar to the end, because nothing after it said
        // anything different. A musician who writes 3/4 at bar four means bars
        // four onwards, not bar four alone.
        for (int bar = 0; bar < 3; ++bar) {
            QCOMPARE(editor.score().masterBars.at(bar).numerator, 4);
        }
        for (int bar = 3; bar < 8; ++bar) {
            QCOMPARE(editor.score().masterBars.at(bar).numerator, 3);
            QCOMPARE(editor.score().masterBars.at(bar).denominator, 4);
        }
    }

    void itStopsAtTheChangeThatIsAlreadyThere()
    {
        Score score = someBars(8);
        score.masterBars[5].numerator = 6;
        score.masterBars[5].denominator = 8;
        score.masterBars[6].numerator = 6;
        score.masterBars[6].denominator = 8;

        Editor editor;
        editor.setScore(score);
        editor.setCursor(Cursor{0, 1, 0, 0});
        QCOMPARE(editor.setTimeSignature(7, 8), Editor::Edit::Done);

        // Bars one to four take the new signature; bar five was already a
        // change and is not what anybody asked to alter.
        QCOMPARE(editor.score().masterBars.at(0).numerator, 4);
        for (int bar = 1; bar <= 4; ++bar) {
            QCOMPARE(editor.score().masterBars.at(bar).numerator, 7);
        }
        QCOMPARE(editor.score().masterBars.at(5).numerator, 6);
        // And bar eight is left alone even though it still holds the signature
        // that was being replaced: the run stopped at the change in bar six and
        // does not jump over it to find more of the same further on.
        QCOMPARE(editor.score().masterBars.at(7).numerator, 4);
    }

    void refusesASignatureNobodyCanWriteDown()
    {
        Editor editor;
        editor.setScore(someBars(3));
        editor.setCursor(Cursor{0, 1, 0, 0});
        // A denominator names a note value, so it is a power of two or it is
        // nothing.
        QCOMPARE(editor.setTimeSignature(4, 5), Editor::Edit::Refused);
        QCOMPARE(editor.setTimeSignature(4, 0), Editor::Edit::Refused);
        QCOMPARE(editor.setTimeSignature(0, 4), Editor::Edit::Refused);
        QCOMPARE(editor.setTimeSignature(33, 4), Editor::Edit::Refused);
        QCOMPARE(editor.setTimeSignature(4, 128), Editor::Edit::Refused);
        QCOMPARE(editor.score().masterBars.at(1).numerator, 4);

        // And the ones that are signatures go in.
        QCOMPARE(editor.setTimeSignature(12, 8), Editor::Edit::Done);
        QCOMPARE(editor.setTimeSignature(2, 2), Editor::Edit::Done);
        QCOMPARE(editor.setTimeSignature(5, 16), Editor::Edit::Done);
    }

    void leavesTheMusicWhereItWasAndLetsTheBarNotAddUp()
    {
        // The rule the whole editor is built on: a bar that no longer adds up
        // is marked, not corrected. Four crotchets asked to be 3/4 stay four
        // crotchets, because taking the difference out of the last note would
        // be rewriting music nobody asked it to touch.
        Editor editor;
        editor.setScore(someBars(2));
        editor.setCursor(Cursor{0, 0, 0, 0});
        const int beatsBefore = editor.score().voices.value(0).beats.size();

        QCOMPARE(editor.setTimeSignature(3, 4), Editor::Edit::Done);
        QCOMPARE(editor.score().voices.value(0).beats.size(), beatsBefore);
        QCOMPARE(editor.score().masterBars.at(0).length(), Rational(3));
    }

    void undoingASignaturePutsEveryBarBack()
    {
        Score score = someBars(6);
        score.masterBars[4].numerator = 3;      // a change already in the score
        Editor editor;
        editor.setScore(score);

        editor.setCursor(Cursor{0, 0, 0, 0});
        QCOMPARE(editor.setTimeSignature(6, 8), Editor::Edit::Done);
        editor.undo();

        for (int bar = 0; bar < 4; ++bar) {
            QCOMPARE(editor.score().masterBars.at(bar).numerator, 4);
            QCOMPARE(editor.score().masterBars.at(bar).denominator, 4);
        }
        QCOMPARE(editor.score().masterBars.at(4).numerator, 3);
    }

    void knowsWhereTheSignatureIsWrittenRatherThanContinued()
    {
        Score score = someBars(5);
        score.masterBars[2].numerator = 3;
        Editor editor;
        editor.setScore(score);

        // The first bar always states one; every other bar only where it
        // differs from the bar before, which is where a page prints it.
        editor.setCursor(Cursor{0, 0, 0, 0});
        QVERIFY(editor.timeSignatureWrittenHere());
        editor.setCursor(Cursor{0, 1, 0, 0});
        QVERIFY(!editor.timeSignatureWrittenHere());
        editor.setCursor(Cursor{0, 2, 0, 0});
        QVERIFY(editor.timeSignatureWrittenHere());
        editor.setCursor(Cursor{0, 3, 0, 0});
        QVERIFY(editor.timeSignatureWrittenHere());     // back to 4/4 is a change too
        editor.setCursor(Cursor{0, 4, 0, 0});
        QVERIFY(!editor.timeSignatureWrittenHere());
    }

    void settingWhatIsAlreadySetIsNothingToDo()
    {
        Editor editor;
        editor.setScore(someBars(3));
        editor.setCursor(Cursor{0, 1, 0, 0});
        QCOMPARE(editor.setTimeSignature(4, 4), Editor::Edit::Nothing);
        QVERIFY(!editor.canUndo());
    }

    // ---- the instrument ----

    /** A bar of notes, one per string, all at the same fret. */
    static Score fretted(int fret)
    {
        Score score = someBars(2);
        for (int bar = 0; bar < 2; ++bar) {
            const QList<int> beats = score.voices.value(bar).beats;
            for (int at = 0; at < beats.size() && at < 4; ++at) {
                Note note;
                note.string = at;
                note.fret = fret;
                note.midi = score.tracks.first().tuning.at(at) + fret;
                const int id = 900 + bar * 10 + at;
                score.notes.insert(id, note);
                score.beats[beats.at(at)].notes = {id};
            }
        }
        return score;
    }

    void retuningMovesThePitchesAndLeavesTheFrets()
    {
        Editor editor;
        editor.setScore(fretted(3));

        // Standard tuning with the low string dropped a tone.
        QCOMPARE(editor.retune({38, 45, 50, 55, 59, 64}), Editor::Edit::Done);
        QCOMPARE(editor.score().tracks.first().tuning.first(), 38);

        // Fret three is still fret three -- nobody rewrote the tab -- and it
        // sounds a tone lower than it did.
        const Note &low = editor.score().notes.value(900);
        QCOMPARE(low.fret, 3);
        QCOMPARE(low.midi, 41);         // was 40 + 3 = 43, now 38 + 3 = 41

        // And every other string is untouched, because only one moved.
        QCOMPARE(editor.score().notes.value(901).midi, 45 + 3);
    }

    void aCapoTakesEveryNoteWithIt()
    {
        Editor editor;
        editor.setScore(fretted(0));
        const int before = editor.score().notes.value(902).midi;

        QCOMPARE(editor.setCapo(2), Editor::Edit::Done);
        QCOMPARE(editor.score().tracks.first().capo, 2);
        // Every string at once, and the fret numbers under it unchanged: they
        // are counted from the capo rather than from the nut.
        QCOMPARE(editor.score().notes.value(902).midi, before + 2);
        QCOMPARE(editor.score().notes.value(902).fret, 0);
        QCOMPARE(editor.score().notes.value(900).fret, 0);

        // Moving it again moves by the difference, not from the nut again.
        QCOMPARE(editor.setCapo(5), Editor::Edit::Done);
        QCOMPARE(editor.score().notes.value(902).midi, before + 5);
    }

    /**
     * The capo belongs in the identity, and for a while it was only in half of
     * it: setCapo() moved every note in the track and typing a fret did not,
     * so the same fret on the same string was two different pitches depending
     * on which of them had put it there.
     */
    /**
     * Chord insertion, which is the only edit in the program that writes notes
     * nobody typed. What it must not do is any of it by halves.
     */
    void aChordIsWrittenAsOneBeatAndTakenBackInOneUndo()
    {
        Editor editor;
        editor.setScore(fretted(0));
        editor.setCursor(at(0, 0, 0));

        // The beat the caret is on already has a note on it. A chord dropped
        // onto a beat is what that beat is now.
        const int beatId = Editing::beatIdAt(editor.score(), editor.cursor());
        QCOMPARE(editor.score().beats.value(beatId).notes.size(), 1);

        const QList<Fretboard::Position> shape = {{1, 3}, {2, 2}, {3, 0}, {4, 1}, {5, 0}};
        QCOMPARE(editor.insertChord(shape, QStringLiteral("C")), Editor::Edit::Done);

        const QList<int> written = editor.score().beats.value(beatId).notes;
        QCOMPARE(written.size(), 5);
        // Real notes on real strings, each sounding what that fret sounds.
        for (const int noteId : written) {
            const Note &note = editor.score().notes.value(noteId);
            QCOMPARE(note.midi, editor.score().tracks.first().tuning.at(note.string) + note.fret);
        }

        // One act: the five notes and the one they displaced come and go
        // together.
        editor.undo();
        QCOMPARE(editor.score().beats.value(beatId).notes.size(), 1);
        QCOMPARE(editor.score().notes.value(900).fret, 0);
        editor.redo();
        QCOMPARE(editor.score().beats.value(beatId).notes.size(), 5);
    }

    /** Redoing it twice must not leave two chords' worth of notes behind. */
    void aChordRedoneIsStillOneChord()
    {
        Editor editor;
        editor.setScore(fretted(0));
        editor.setCursor(at(0, 0, 0));
        const int beatId = Editing::beatIdAt(editor.score(), editor.cursor());
        const int before = int(editor.score().notes.size());

        const QList<Fretboard::Position> shape = {{1, 3}, {2, 2}, {3, 0}};
        QCOMPARE(editor.insertChord(shape, QStringLiteral("C")), Editor::Edit::Done);
        editor.undo();
        editor.redo();
        editor.undo();
        editor.redo();

        QCOMPARE(editor.score().beats.value(beatId).notes.size(), 3);
        // One note went out of the beat and three came in.
        QCOMPARE(int(editor.score().notes.size()), before - 1 + 3);
    }

    /**
     * A hand holding three keys is one act.
     *
     * Step entry rewrites the beat as each key lands, so a triad arrives as
     * three chords written onto the same beat. Three presses of undo to take
     * back one chord would be three presses too many -- and the trap is the
     * other way round: a merge that forgot the notes the newer one wrote would
     * leave a chord behind when it was undone.
     */
    void aChordStillBeingBuiltIsOneUndo()
    {
        Editor editor;
        editor.setScore(fretted(0));
        editor.setCursor(at(0, 0, 0));
        const int beatId = Editing::beatIdAt(editor.score(), editor.cursor());
        const int before = int(editor.score().notes.size());
        const QList<int> was = editor.score().beats.value(beatId).notes;

        // A key, then a second, then a third, all held.
        QCOMPARE(editor.insertChord({{1, 3}}, QStringLiteral("C"), true), Editor::Edit::Done);
        QCOMPARE(editor.insertChord({{1, 3}, {2, 2}}, QStringLiteral("C"), true),
                 Editor::Edit::Done);
        QCOMPARE(editor.insertChord({{1, 3}, {2, 2}, {3, 0}}, QStringLiteral("C"), true),
                 Editor::Edit::Done);
        QCOMPARE(editor.score().beats.value(beatId).notes.size(), 3);

        // One undo, and the beat is exactly as it was before any key was
        // pressed -- no leftovers, and the same note count as at the start.
        editor.undo();
        QCOMPARE(editor.score().beats.value(beatId).notes, was);
        QCOMPARE(int(editor.score().notes.size()), before);

        // And redoing puts back the whole chord rather than the first key.
        editor.redo();
        QCOMPARE(editor.score().beats.value(beatId).notes.size(), 3);
        editor.undo();
        QCOMPARE(int(editor.score().notes.size()), before);
    }

    /** A chord that is finished does not swallow the next one. */
    void twoChordsAskedForSeparatelyAreTwoUndos()
    {
        Editor editor;
        editor.setScore(fretted(0));
        editor.setCursor(at(0, 0, 0));
        const int beatId = Editing::beatIdAt(editor.score(), editor.cursor());

        QCOMPARE(editor.insertChord({{1, 3}, {2, 2}}, QStringLiteral("C"), false),
                 Editor::Edit::Done);
        QCOMPARE(editor.insertChord({{4, 1}}, QStringLiteral("D"), false), Editor::Edit::Done);
        QCOMPARE(editor.score().beats.value(beatId).notes.size(), 1);

        editor.undo();
        QCOMPARE(editor.score().beats.value(beatId).notes.size(), 2);
    }

    void aChordOnAStringThePartHasNotGotIsRefused()
    {
        Editor editor;
        editor.setScore(fretted(0));
        editor.setCursor(at(0, 0, 0));
        QCOMPARE(editor.insertChord({{6, 0}}, QStringLiteral("C")), Editor::Edit::Refused);
        QCOMPARE(editor.insertChord({{0, 99}}, QStringLiteral("C")), Editor::Edit::Refused);
        // Nothing to write is nothing to do rather than a refusal.
        QCOMPARE(editor.insertChord({}, QStringLiteral("C")), Editor::Edit::Nothing);
    }

    void aTypedNoteSoundsWithTheCapoInIt()
    {
        Editor editor;
        editor.setScore(fretted(0));
        QCOMPARE(editor.setCapo(2), Editor::Edit::Done);

        // The note that was already on the third string: open, and sounding
        // two semitones above the open string because of the capo.
        QCOMPARE(editor.score().notes.value(902).fret, 0);
        QCOMPARE(editor.score().notes.value(902).midi, 52);

        // Fret three on that same string, typed now, is three semitones above
        // it. Anything else means the page is drawing two notes the same and
        // playing them differently.
        editor.setCursor(at(0, 0, 2));
        editor.typeDigit(3);
        const int typed = Editing::noteIdAt(editor.score(), editor.cursor());
        QVERIFY(typed >= 0);
        QCOMPARE(editor.score().notes.value(typed).fret, 3);
        QCOMPARE(editor.score().notes.value(typed).midi, 55);
    }

    /**
     * Moving a note across strings is the one edit that changes a fret without
     * changing the music, so it is the one edit a missing capo shows up in
     * most plainly: the arithmetic was off by exactly the capo, and the note
     * landed on a fret that did not sound what it had been sounding.
     */
    void movingANoteAcrossStringsKeepsItsPitchUnderACapo()
    {
        Editor editor;
        editor.setScore(fretted(5));
        QCOMPARE(editor.setCapo(2), Editor::Edit::Done);

        // Fret five on the low string, with a capo at the second: A2.
        editor.setCursor(at(0, 0, 0));
        const int was = editor.score().notes.value(900).midi;
        QCOMPARE(was, 47);

        // The next string up is tuned to that pitch, so with the capo on it
        // the note is the open string -- fret nought, counted from the capo.
        QCOMPARE(editor.moveNoteAcross(1), Editor::Edit::Done);
        QCOMPARE(editor.score().notes.value(900).string, 1);
        QCOMPARE(editor.score().notes.value(900).fret, 0);
        QCOMPARE(editor.score().notes.value(900).midi, was);
    }

    void undoingAnInstrumentChangePutsThePitchesBack()
    {
        Editor editor;
        editor.setScore(fretted(7));
        const int wasMidi = editor.score().notes.value(900).midi;
        const QList<int> wasTuning = editor.score().tracks.first().tuning;

        QCOMPARE(editor.retune({36, 43, 48, 53, 57, 62}), Editor::Edit::Done);
        QCOMPARE(editor.setCapo(3), Editor::Edit::Done);
        editor.undo();
        editor.undo();

        QCOMPARE(editor.score().tracks.first().tuning, wasTuning);
        QCOMPARE(editor.score().tracks.first().capo, 0);
        QCOMPARE(editor.score().notes.value(900).midi, wasMidi);
        QCOMPARE(editor.score().notes.value(900).fret, 7);
    }

    void refusesAnInstrumentNobodyIsHolding()
    {
        Editor editor;
        editor.setScore(fretted(0));
        // The wrong number of strings is a question about which one went.
        QCOMPARE(editor.retune({40, 45, 50, 55, 59}), Editor::Edit::Refused);
        // And a pitch outside anything fretted is a slipped digit.
        QCOMPARE(editor.retune({4, 45, 50, 55, 59, 64}), Editor::Edit::Refused);
        QCOMPARE(editor.retune({40, 45, 50, 55, 59, 120}), Editor::Edit::Refused);
        QCOMPARE(editor.setCapo(13), Editor::Edit::Refused);
        QCOMPARE(editor.setCapo(-1), Editor::Edit::Refused);

        QCOMPARE(editor.score().tracks.first().tuning.first(), 40);
        QCOMPARE(editor.score().tracks.first().capo, 0);

        // Setting what is already set is nothing to do rather than a refusal.
        QCOMPARE(editor.retune({40, 45, 50, 55, 59, 64}), Editor::Edit::Nothing);
        QCOMPARE(editor.setCapo(0), Editor::Edit::Nothing);
    }

    void aNoteWithNoPitchStaysWithoutOne()
    {
        Score score = fretted(2);
        score.notes[901].midi = -1;         // what an importer leaves behind
        Editor editor;
        editor.setScore(score);

        QCOMPARE(editor.setCapo(4), Editor::Edit::Done);
        QCOMPARE(editor.score().notes.value(901).midi, -1);
    }

    void readsATuningBackFromItsNames()
    {
        QCOMPARE(NoteName::parse(QStringLiteral("E2")), 40);
        QCOMPARE(NoteName::parse(QStringLiteral("C2")), 36);
        QCOMPARE(NoteName::parse(QStringLiteral("A#3")), 58);
        QCOMPARE(NoteName::parse(QStringLiteral("Bb3")), 58);       // the same note
        QCOMPARE(NoteName::parse(QStringLiteral("c4")), 60);
        QCOMPARE(NoteName::parse(QStringLiteral("40")), 40);
        QCOMPARE(NoteName::parse(QStringLiteral(" E2 ")), 40);
        QCOMPARE(NoteName::parse(QStringLiteral("H2")), -1);
        QCOMPARE(NoteName::parse(QStringLiteral("E")), -1);
        QCOMPARE(NoteName::parse(QString()), -1);

        // And whatever it prints, it reads.
        for (int midi = 12; midi <= 96; ++midi) {
            QCOMPARE(NoteName::parse(NoteName::of(midi)), midi);
        }
    }

    void namesABarAndTakesTheNameOffAgain()
    {
        Editor editor;
        editor.setScore(someBars(4));
        editor.setCursor(Cursor{0, 2, 0, 0});

        QCOMPARE(editor.setSection(QStringLiteral("Chorus")), Editor::Edit::Done);
        QCOMPARE(editor.score().masterBars.at(2).section, QStringLiteral("Chorus"));
        QCOMPARE(editor.score().masterBars.at(1).section, QString());

        // A name that is a space is a section on the bar strip and nothing on
        // the page, and nobody meant to make one.
        QCOMPARE(editor.setSection(QStringLiteral("  Chorus  ")), Editor::Edit::Nothing);
        QCOMPARE(editor.setSection(QStringLiteral("   ")), Editor::Edit::Done);
        QCOMPARE(editor.score().masterBars.at(2).section, QString());

        editor.undo();
        QCOMPARE(editor.score().masterBars.at(2).section, QStringLiteral("Chorus"));
    }

    // ---- tracks ----

    /** Every bar id any track can still reach. */
    static QSet<int> reachableBars(const Score &score)
    {
        QSet<int> bars;
        for (const MasterBar &master : score.masterBars) {
            for (const int id : master.bars) {
                bars.insert(id);
            }
        }
        return bars;
    }

    void addingAPartPutsABarOfItInEveryBar()
    {
        Editor editor;
        editor.setScore(someBars(5));
        QCOMPARE(editor.score().tracks.size(), 1);

        QCOMPARE(editor.addTrack(QStringLiteral("electricBass")), Editor::Edit::Done);
        QCOMPARE(editor.score().tracks.size(), 2);
        QCOMPARE(editor.score().tracks.at(1).instrumentType, QStringLiteral("electricBass"));
        QCOMPARE(editor.score().tracks.at(1).tuning.size(), 4);

        // A track is a column through every bar: a score where one part has
        // fewer bars than another is not one anything downstream can read.
        for (const MasterBar &master : editor.score().masterBars) {
            QCOMPARE(master.bars.size(), 2);
            QVERIFY(editor.score().bars.contains(master.bars.at(1)));
        }
        // And the caret is in the new part, which is where somebody who just
        // added one is about to type.
        QCOMPARE(editor.cursor().track, 1);

        editor.undo();
        QCOMPARE(editor.score().tracks.size(), 1);
        for (const MasterBar &master : editor.score().masterBars) {
            QCOMPARE(master.bars.size(), 1);
        }
    }

    void removingAPartTakesOnlyWhatNothingElseIsUsing()
    {
        // Two parts sharing a beat, which is what a deduplicated file looks
        // like: walking the departing track would erase a beat the other one
        // is still playing.
        Score score = someBars(2, 2);
        const int shared = score.voices.value(0).beats.first();
        score.voices[1].beats = {shared};

        Editor editor;
        editor.setScore(score);
        QCOMPARE(editor.score().tracks.size(), 2);

        QCOMPARE(editor.removeTrack(1), Editor::Edit::Done);
        QCOMPARE(editor.score().tracks.size(), 1);
        for (const MasterBar &master : editor.score().masterBars) {
            QCOMPARE(master.bars.size(), 1);
        }
        // The shared beat is still there, because the remaining part reaches it.
        QVERIFY(editor.score().beats.contains(shared));

        // And nothing unreachable is left lying about.
        const QSet<int> bars = reachableBars(editor.score());
        for (const int id : editor.score().bars.keys()) {
            QVERIFY2(bars.contains(id), "an unreachable bar was left behind");
        }
    }

    void undoingARemovalPutsBackEverythingItSweptUp()
    {
        Score score = someBars(3, 2);
        Editor editor;
        editor.setScore(score);
        const int barsBefore = editor.score().bars.size();
        const int voicesBefore = editor.score().voices.size();
        const int beatsBefore = editor.score().beats.size();

        QCOMPARE(editor.removeTrack(0), Editor::Edit::Done);
        QVERIFY(editor.score().bars.size() < barsBefore);
        editor.undo();

        QCOMPARE(editor.score().tracks.size(), 2);
        QCOMPARE(editor.score().bars.size(), barsBefore);
        QCOMPARE(editor.score().voices.size(), voicesBefore);
        QCOMPARE(editor.score().beats.size(), beatsBefore);
        for (const MasterBar &master : editor.score().masterBars) {
            QCOMPARE(master.bars.size(), 2);
        }
    }

    void keepsTheLastPart()
    {
        Editor editor;
        editor.setScore(someBars(2));
        // The same rule the last bar has: a score with no parts is not a
        // shorter score, it is one the rest of the program treats as empty.
        QCOMPARE(editor.removeTrack(0), Editor::Edit::Refused);
        QCOMPARE(editor.score().tracks.size(), 1);
    }

    void renamesAPartButNotToNothing()
    {
        Editor editor;
        editor.setScore(someBars(1));
        QCOMPARE(editor.renameTrack(0, QStringLiteral("  Rhythm  ")), Editor::Edit::Done);
        QCOMPARE(editor.score().tracks.first().name, QStringLiteral("Rhythm"));
        QCOMPARE(editor.renameTrack(0, QStringLiteral("Rhythm")), Editor::Edit::Nothing);
        // A row of nothing in the list and the mixer is not a name.
        QCOMPARE(editor.renameTrack(0, QStringLiteral("   ")), Editor::Edit::Refused);
        QCOMPARE(editor.score().tracks.first().name, QStringLiteral("Rhythm"));
    }

    void changingTheInstrumentLeavesTheTuningAlone()
    {
        Editor editor;
        editor.setScore(someBars(1));
        const QList<int> tuning = editor.score().tracks.first().tuning;

        QCOMPARE(editor.setTrackInstrument(0, QStringLiteral("drumKit")),
                 Editor::Edit::Done);
        QCOMPARE(editor.score().tracks.first().instrumentType, QStringLiteral("drumKit"));
        QVERIFY(editor.score().tracks.first().isPercussion());
        // Which strings a part is written for is a separate decision with an
        // editor of its own: answering it here would move every pitch in the
        // part in answer to a question about its sound.
        QCOMPARE(editor.score().tracks.first().tuning, tuning);

        editor.undo();
        QCOMPARE(editor.score().tracks.first().instrumentType,
                 QStringLiteral("electricGuitar"));
    }

    void movingAPartTakesItsBarsWithIt()
    {
        Score score = someBars(2, 3);
        Editor editor;
        editor.setScore(score);
        QCOMPARE(editor.renameTrack(0, QStringLiteral("First")), Editor::Edit::Done);
        QCOMPARE(editor.renameTrack(2, QStringLiteral("Third")), Editor::Edit::Done);
        const QList<int> wasFirstColumn = {score.masterBars.at(0).bars.at(0),
                                           score.masterBars.at(1).bars.at(0)};

        editor.setCursor(Cursor{0, 0, 0, 0});
        QCOMPARE(editor.moveTrack(0, 2), Editor::Edit::Done);
        QCOMPARE(editor.score().tracks.at(2).name, QStringLiteral("First"));
        QCOMPARE(editor.score().tracks.at(1).name, QStringLiteral("Third"));
        // The bars moved with the part, or every other part would be playing
        // somebody else's music.
        QCOMPARE(editor.score().masterBars.at(0).bars.at(2), wasFirstColumn.at(0));
        QCOMPARE(editor.score().masterBars.at(1).bars.at(2), wasFirstColumn.at(1));
        // And the caret went with it rather than staying at a number that now
        // means a different part.
        QCOMPARE(editor.cursor().track, 2);

        // And back up one.
        QCOMPARE(editor.moveTrack(2, -1), Editor::Edit::Done);
        QCOMPARE(editor.score().tracks.at(1).name, QStringLiteral("First"));

        // Off either end is refused rather than clamped: a part that did not
        // move when it was told to would look like a part that could not be.
        QCOMPARE(editor.moveTrack(0, -1), Editor::Edit::Refused);
        QCOMPARE(editor.moveTrack(2, 1), Editor::Edit::Refused);
    }

    void aBlankScoreIsSomethingToWriteIn()
    {
        Editor editor;
        editor.setScore(Editor::blankScore());
        QVERIFY(!editor.score().isEmpty());
        QCOMPARE(editor.score().tracks.size(), 1);
        QCOMPARE(editor.score().masterBars.size(), 1);
        QCOMPARE(editor.score().tracks.first().tuning.size(), 6);
        // A tempo said rather than implied: the page prints the first thing a
        // player looks for, and a score with none prints nothing.
        QCOMPARE(editor.score().tempos.size(), 1);

        // And it can be written in straight away.
        editor.setCursor(Cursor{0, 0, 0, 0});
        editor.setFret(5);
        QCOMPARE(Editing::beatIdAt(editor.score(), editor.cursor()) >= 0, true);
    }
};

QTEST_GUILESS_MAIN(EditorTest)
#include "editortest.moc"
