// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#pragma once

#include "cursor.h"
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
    void setCursor(const Cursor &cursor);
    void move(Editing::Move move);

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

    /** Sets the fret under the caret outright, creating the note if need be. */
    void setFret(int fret);

    /** Removes the note under the caret, if there is one. */
    void clearNote();

    /** Moves the note under the caret by a number of frets, staying on its string. */
    void transposeNote(int frets);

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
    static int midiFor(const Score &score, const Cursor &cursor, int fret);

Q_SIGNALS:
    /** Something in `bar` changed; -1 means the whole score did. */
    void scoreEdited(int bar);
    void cursorChanged();
    void historyChanged();

private:
    Score m_score;
    Cursor m_cursor;
    QUndoStack *m_undo;

    QElapsedTimer m_typing;
    int m_digitRun = 0;         //< bumped whenever a run of digits ends
    int m_pendingFret = -1;
};
