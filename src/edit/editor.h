// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#pragma once

#include "clip.h"
#include "cursor.h"
#include "notevalue.h"
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

    /** Moves the note under the caret by a number of frets, staying on its string. */
    void transposeNote(int frets);

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
