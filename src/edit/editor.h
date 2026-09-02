// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#pragma once

#include "clip.h"
#include "cursor.h"
#include "fretboard.h"
#include "notevalue.h"
#include "recorder.h"
#include "score.h"

#include <QElapsedTimer>
#include <QObject>
#include <QString>

class QUndoStack;

/**
 * The score, a caret, and a way to change one with the other.
 *
 * Every change goes through an undoable command, without exception. Not for
 * the menu item -- for the discipline: a change that cannot describe how to
 * reverse itself is a change that was not thought through, and an editor whose
 * undo works for most things is worse than one with no undo at all, because
 * people stop checking.
 *
 * Typing a fret number is the one place this is not literally true. Typing
 * `1` then `2` means fret 12, not fret 1 followed by fret 2, so the second
 * command merges into the first and one press of undo takes the whole number
 * away. A run of digits ends when the caret moves, when `endDigitEntry` is
 * called, or after a moment's pause.
 */
class Editor : public QObject
{
    Q_OBJECT

public:
    /**
     * A mark a note either carries or does not.
     *
     * Only the four the program can both draw and play. A mark the page cannot
     * show is a key that appears to do nothing, and one the synthesiser
     * ignores is a lie about what will come out of the speakers -- accents,
     * ghosts of the other kind, vibrato and the rest wait until both ends can
     * honour them.
     */
    enum class Mark {
        Dead,       //< a click at no particular pitch, drawn as a cross
        Ghost,      //< played but barely heard, drawn in brackets
        PalmMute,   //< damped by the picking hand, drawn as a run over the staff
        LetRing,    //< held past its written length, drawn the same way
    };

    /** What an edit that is allowed to say no did. */
    enum class Edit {
        Done,       //< the score changed
        Nothing,    //< there was nothing there to change, which is not a refusal
        Refused,    //< there was, and it would not fit
    };

    explicit Editor(QObject *parent = nullptr);
    ~Editor() override;

    /** Replaces the score, and empties the undo history with it. */
    void setScore(const Score &score);
    const Score &score() const;

    Cursor cursor() const;
    void setCursor(const Cursor &cursor, bool extend = false);
    void move(Editing::Move move, bool extend = false);

    // ---- selection ----

    /**
     * Whether more than the beat under the caret is selected.
     *
     * A selection runs from where it was started to where the caret is now,
     * both ends included, within one track and one voice. Moving to another
     * track or voice drops it rather than reinterpreting it: what a selection
     * that spans two parts is supposed to mean is not obvious enough to guess.
     */
    bool hasSelection() const;

    /** The selected beats, or the caret's own beat where nothing is selected. */
    Editing::Range selection() const;

    void clearSelection();

    // ---- the clipboard ----

    /** Takes a copy of the selection. Changes nothing, so there is nothing to undo. */
    void copy();

    /** Copies the selection and then removes it, which is one undoable act. */
    void cut();

    /**
     * Puts the clipboard in at the caret, bar for bar.
     *
     * The first bar's worth goes in at the caret and pushes what follows along;
     * each further bar's worth goes in at the start of the next bar of the
     * score. Returns false and does nothing at all where the music would run
     * off the end -- half a paste is worse than none, because the half that
     * landed has to be found and undone by hand.
     */
    bool paste();

    /** Removes every selected beat, which is what Delete means with a selection. */
    void deleteSelection();

    bool canPaste() const;
    const Clip &clip() const;

    // ---- editing ----

    /**
     * Types one digit at the caret.
     *
     * A digit typed shortly after another on the same string extends it, so
     * `1` then `2` is fret 12 rather than fret 2. Frets above 36 are refused
     * rather than clamped: 37 is a typing mistake, not a note.
     */
    void typeDigit(int digit);

    /** Ends a run of digits, so the next one starts a new number. */
    void endDigitEntry();

    /**
     * Sets the fret under the caret outright, making whatever has to exist.
     *
     * The note, if the string is empty; the beat too, where the caret is one
     * past the end of a voice or in a bar that has none at all -- which is how
     * music gets written into an empty bar, and how it gets added to the end of
     * a piece. All of it comes back off in one undo, because it was one act.
     */
    void setFret(int fret);

    /** Removes the note under the caret, if there is one. */
    void clearNote();

    // ---- beats ----

    /**
     * Puts an empty beat at the caret, pushing what was there along.
     *
     * It lasts as long as the beat it displaced, or the one before it: a bar
     * of quavers wants another quaver, not a crotchet and a warning. The caret
     * does not move, so it is now sitting on the new beat with nothing on it,
     * which is where a number wants typing next.
     */
    void insertBeat();

    /** Removes the beat under the caret, notes and all. */
    void deleteBeat();

    /**
     * Moves the note under the caret along its string, or the whole selection.
     *
     * All of it or none of it: a phrase with one note left behind because it
     * would not fit on the neck is not the phrase that was asked for, and it
     * is worse than a refusal because it looks like it worked. The selection
     * stays, so a phrase can be walked up a fret at a time.
     */
    Edit transpose(int frets);

    /**
     * Puts a mark on the note under the caret, or on the whole selection.
     *
     * On unless every note in it is marked already: "palm mute this" is what a
     * person means the first time and "stop" is what they mean the second.
     * Flipping each note separately would turn a half-marked phrase inside out
     * instead of finishing the job.
     */
    Edit toggleMark(Mark mark);

    /**
     * Moves the note under the caret to the next string, keeping its pitch.
     *
     * The one edit that changes a fret without changing the music: which
     * string a note is played on is a fingering decision, and the fret it
     * lands on is whatever makes it sound the same. Refused where the note
     * would fall behind the nut, past the end of the neck, or on top of a
     * note that is already there.
     */
    Edit moveNoteAcross(int strings);

    // ---- bars ----

    /**
     * Puts an empty bar in at the caret, pushing the rest of the score along.
     *
     * Across every track at once, because a master bar is the score's own unit
     * of time and a bar added to one track alone would put the others out of
     * step for the rest of the piece. It takes the time signature of the bar it
     * displaces, and neither its section name nor its repeat signs.
     */
    void insertBar();

    /**
     * Writes a chord into the beat at the caret, as one act.
     *
     * The beat's own notes go, because a chord dropped onto a beat is what
     * that beat is now: leaving what was there and adding to it produces a
     * sound nobody asked for and no way to say what it was meant to be. One
     * undo takes the whole thing back, including the beat if there was not one
     * there before.
     *
     * Refused where the shape is empty or names a string the part has not got.
     * Nothing here decides *which* chord: that is harmony, it is somebody's
     * choice, and by the time it reaches this it is a list of frets.
     */
    Edit insertChord(const QList<Fretboard::Position> &shape, const QString &name,
                     bool building = false);

    /**
     * Writes a bar as it was just played, in place of what the bar had.
     *
     * The first voice of `bar` in `track` becomes `beats`, whole: what was
     * there goes, because a bar somebody played into is what they played, and
     * a bar they stayed silent through is never handed to this at all. Ties
     * are set between the fragments of one note that would not fit a single
     * written value.
     *
     * One undo takes the whole bar back, however many keys built it. While a
     * bar is still `building`, each key merges into the command before it,
     * the way a chord being held does; the first write into a bar is not
     * building, so that a bar played twice round a repeat is two acts.
     *
     * Refused where the shape names a string the part has not got, or the
     * bar is not there. Beats shared with another part are let go of rather
     * than removed, because the other part is still reading them.
     */
    Edit recordBar(int track, int bar, const QList<Recorder::Beat> &beats, bool building);

    /** Puts an empty bar on the end of the score, and the caret in it. */
    void appendBar();

    /**
     * Takes the bar under the caret out, across every track.
     *
     * Refused on the last bar of a score: a score with no bars is not a shorter
     * score, it is one the rest of the program treats as empty.
     */
    void deleteBar();

    /** Whether there is a bar under the caret that is not the only one. */
    bool canDeleteBar() const;

    // ---- tempo ----

    /**
     * The tempo from the caret's bar on, in crotchets a minute.
     *
     * At the bar line and not at the caret. gpif can put a change part way
     * through a bar and this deliberately will not: a caret on the third beat
     * is where somebody is typing notes, not a statement about where the music
     * changes speed, and a change with no visible start is one nobody can find
     * to remove. Any other change already inside that bar goes with it, so a
     * bar has one tempo and one place to look for it.
     *
     * Refused outside twenty to four hundred, which is wider than music and
     * narrow enough to catch a slipped digit.
     */
    Edit setTempo(double quarterBpm);

    /** Takes the change off the caret's bar, where the bar has one of its own. */
    Edit clearTempo();

    /** Whether the caret's bar carries a change rather than inheriting one. */
    bool hasTempoHere() const;

    // ---- time ----

    /**
     * Sets the time signature from the caret's bar until the next change.
     *
     * Not the one bar. gpif keeps a signature on every master bar because
     * that is how the file is shaped, but a musician who writes 3/4 at bar
     * five means bars five onwards and not bar five alone -- so it runs
     * forward over every bar that shares the old signature and stops at the
     * first that does not, which is where the next change already is.
     *
     * What was already written in those bars is left exactly where it is. A
     * bar of four crotchets asked to be 3/4 is now a bar that does not add up,
     * and the page marks it: taking the difference out of the last note would
     * be rewriting music nobody asked it to touch, and is the one thing an
     * editor must never do quietly.
     *
     * Refused on a numerator outside one to thirty-two, or a denominator that
     * is not a power of two up to sixty-four -- 4/5 is a typing mistake and
     * not a time signature.
     */
    Edit setTimeSignature(int numerator, int denominator);

    /** Whether the caret's bar is where the signature changes rather than continues. */
    bool timeSignatureWrittenHere() const;

    /**
     * Names the section that starts at the caret's bar, or takes the name off.
     *
     * An empty name removes it, which is the only way to say "this is not
     * where the chorus starts any more" -- there is no separate command for
     * it, because a section is a name and no name is no section.
     */
    Edit setSection(const QString &name);

    // ---- tracks ----

    /**
     * Adds a part after the caret's track, and puts the caret in it.
     *
     * A track is a column through every bar of the score, so this puts an
     * empty bar in every master bar as well: a score where one part has fewer
     * bars than another is not a score anything downstream can read.
     */
    Edit addTrack(const QString &instrumentId);

    /**
     * Takes a part out, and everything only it was using with it.
     *
     * Refused on the last one: a score with no parts is not a shorter score,
     * it is one the rest of the program treats as empty -- the same rule the
     * last bar has.
     *
     * Beats and notes are shared between parts where a file deduplicated them,
     * so what goes is what nothing left is reaching: the removal is a sweep
     * from the remaining tracks rather than a walk through the departing one.
     */
    Edit removeTrack(int track);

    /** What a part is called on the page, in the list and in the mixer. */
    Edit renameTrack(int track, const QString &name);

    /**
     * Changes what a part is, and therefore what it sounds like.
     *
     * The tuning is left exactly as it was. Turning a guitar into a bass does
     * not retune the guitar -- the strings a part is written for are a
     * separate decision with its own editor, and doing both at once would move
     * every pitch in the part in answer to a question about its sound.
     */
    Edit setTrackInstrument(int track, const QString &instrumentId);

    /** Moves a part up or down the list, taking its bars with it. */
    Edit moveTrack(int track, int by);

    /** A score with one guitar and one empty bar, which is where writing starts. */
    static Score blankScore();

    // ---- the instrument ----

    /**
     * Retunes the strings of the caret's track.
     *
     * The frets stay where they are and the pitches move, which is what
     * retuning an instrument does: dropping the low string to D leaves fret
     * three at fret three and makes it sound a tone lower. The tab on the page
     * does not change at all, because nobody rewrote it.
     *
     * The other reading -- keep the pitches and move the frets -- is
     * transcribing a part for a different tuning, which is a different act
     * with a different answer for every note, and is reachable afterwards by
     * transposing. Doing it here would rewrite the page in response to a
     * change to the instrument, which is the wrong way round.
     *
     * Refused where a string would land outside the range of a fretted
     * instrument, or where the list is the wrong length for the track.
     */
    Edit retune(const QList<int> &tuning);

    /**
     * Moves the capo of the caret's track.
     *
     * Every note in the track goes with it, because a capo raises every string
     * at once and the fret numbers under it are counted from the capo rather
     * than from the nut. Refused past the twelfth fret, where a capo stops
     * being a capo and starts being a shorter instrument.
     */
    Edit setCapo(int fret);

    // ---- how long a beat lasts ----

    /**
     * Sets the beat under the caret to a note value, named the way musicians
     * name it: 4 is a crotchet, 8 a quaver, 1 a semibreve. Any dots it had are
     * dropped, because asking for a quaver asks for a quaver.
     *
     * The bar is left to add up to whatever it now adds up to. A program that
     * quietly took the difference out of the next note along would be
     * rewriting music nobody asked it to touch; the page marks the bar
     * instead.
     */
    void setDuration(int denominator);

    /** Adds an augmentation dot to the beat under the caret, or takes it away. */
    void toggleDot();

    /** Doubles or halves the value under the caret, keeping its dots. */
    void scaleDuration(int steps);

    // ---- history ----

    QUndoStack *undoStack() const;
    bool canUndo() const;
    bool canRedo() const;
    QString undoText() const;
    QString redoText() const;
    void undo();
    void redo();

    /** True when the score has been changed since it was opened or saved. */
    bool isModified() const;
    void setUnmodified();

    // ---- what the commands use, and nothing else should ----

    Score &mutableScore();
    void noteEdited(int bar);
    static int freshNoteId(const Score &score);
    static int freshBeatId(const Score &score);
    static int freshBarId(const Score &score);
    static int freshVoiceId(const Score &score);

    /**
     * The voice at the cursor, put into the bar if it is not there yet.
     *
     * Returns -1 where there is no bar to put one in, which is the only case
     * an editor cannot talk its way out of. `created` says whether one was
     * made, so undo can take it away again.
     */
    static int voiceForEditing(Score &score, const Cursor &cursor, bool *created);

    /** Takes back exactly what `voiceForEditing` added. */
    static void dropVoice(Score &score, const Cursor &cursor);

    /**
     * The id of a duration in the score's rhythm table, adding it if it is not
     * there yet.
     *
     * Deduplicated, the way gpif itself stores them: a score has hundreds of
     * beats and about twenty distinct durations. An id left unreferenced by an
     * undo stays in the table rather than being collected -- there are only so
     * many durations in music, and the next edit is likely to want it back.
     */
    static int rhythmIdFor(Score &score, const Rational &duration);
    static int midiFor(const Score &score, const Cursor &cursor, int fret);

Q_SIGNALS:
    /** Something in `bar` changed; -1 means the whole score did. */
    void scoreEdited(int bar);
    void cursorChanged();
    void historyChanged();

private:
    /** Pushes a duration change, if it is a change and there is a beat to change. */
    void applyDuration(const Rational &duration);

    /** The notes an edit means: the selection's, or the caret's own. */
    QList<int> notesToMove() const;

    /** Every note id in a track, in no particular order. */
    QList<int> notesOfTrack(int track) const;

    Score m_score;
    Cursor m_cursor;
    QUndoStack *m_undo;

    Cursor m_anchor;
    bool m_selecting = false;
    Clip m_clip;

    QElapsedTimer m_typing;
    int m_digitRun = 0;         //< bumped whenever a run of digits ends
    int m_pendingFret = -1;
};
