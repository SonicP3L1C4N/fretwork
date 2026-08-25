// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "editor.h"

#include <KLocalizedString>

#include <QUndoCommand>
#include <QUndoStack>

#include <algorithm>

namespace
{
/** A digit typed within this long of the last one extends it rather than replacing it. */
constexpr qint64 DigitRunMilliseconds = 900;

/** Above this is a typing mistake rather than a note. */
constexpr int HighestFret = 36;

/** Commands of the same kind may merge; different kinds never do. */
constexpr int SetFretId = 1;

/**
 * Putting a fret on a string, where there may or may not already be one.
 *
 * The awkward case is the one that matters: typing on an empty string creates
 * a note, and undoing that has to remove it again rather than leave a fret 0
 * behind, which is a different sound and a different-looking bar.
 */
class SetFretCommand : public QUndoCommand
{
public:
    SetFretCommand(Editor *editor, const Cursor &cursor, int fret, int run)
        : m_editor(editor)
        , m_cursor(cursor)
        , m_fret(fret)
        , m_run(run)
    {
        const Score &score = editor->score();
        m_noteId = Editing::noteIdAt(score, cursor);
        m_created = m_noteId < 0;
        if (!m_created) {
            m_previous = score.notes.value(m_noteId);
        }
        setText(i18n("Type fret %1", fret));
    }

    int id() const override
    {
        return SetFretId;
    }

    bool mergeWith(const QUndoCommand *other) override
    {
        const auto *next = static_cast<const SetFretCommand *>(other);
        // Only within one run of digits on one string: two separate edits that
        // happen to be next to each other in the history are two undos.
        if (next->m_run != m_run || !(next->m_cursor == m_cursor)) {
            return false;
        }
        m_fret = next->m_fret;
        setText(i18n("Type fret %1", m_fret));
        return true;
    }

    void redo() override
    {
        Score &score = m_editor->mutableScore();
        const int beatId = Editing::beatIdAt(score, m_cursor);
        if (beatId < 0) {
            return;
        }

        if (m_created) {
            if (m_noteId < 0) {
                m_noteId = Editor::freshNoteId(score);
            }
            Note note;
            note.string = m_cursor.string;
            note.fret = m_fret;
            note.midi = Editor::midiFor(score, m_cursor, m_fret);
            score.notes.insert(m_noteId, note);
            score.beats[beatId].notes.append(m_noteId);
        } else {
            Note note = score.notes.value(m_noteId);
            note.fret = m_fret;
            note.midi = Editor::midiFor(score, m_cursor, m_fret);
            // A number typed over a dead note means a note again.
            note.muted = false;
            score.notes.insert(m_noteId, note);
        }
        m_editor->noteEdited(m_cursor.bar);
    }

    void undo() override
    {
        Score &score = m_editor->mutableScore();
        const int beatId = Editing::beatIdAt(score, m_cursor);
        if (beatId < 0) {
            return;
        }

        if (m_created) {
            score.beats[beatId].notes.removeAll(m_noteId);
            score.notes.remove(m_noteId);
        } else {
            score.notes.insert(m_noteId, m_previous);
        }
        m_editor->noteEdited(m_cursor.bar);
    }

private:
    Editor *m_editor;
    Cursor m_cursor;
    int m_fret;
    int m_run;
    int m_noteId = -1;
    bool m_created = false;
    Note m_previous;
};

/** Taking a note off a string, and putting it back exactly as it was. */
class ClearNoteCommand : public QUndoCommand
{
public:
    ClearNoteCommand(Editor *editor, const Cursor &cursor)
        : m_editor(editor)
        , m_cursor(cursor)
    {
        const Score &score = editor->score();
        m_noteId = Editing::noteIdAt(score, cursor);
        m_previous = score.notes.value(m_noteId);
        // Where it sat in the beat, so undo does not reorder the chord.
        const int beatId = Editing::beatIdAt(score, cursor);
        m_position = int(score.beats.value(beatId).notes.indexOf(m_noteId));
        setText(i18n("Delete note"));
    }

    bool isValid() const
    {
        return m_noteId >= 0;
    }

    void redo() override
    {
        Score &score = m_editor->mutableScore();
        const int beatId = Editing::beatIdAt(score, m_cursor);
        if (beatId < 0) {
            return;
        }
        score.beats[beatId].notes.removeAll(m_noteId);
        score.notes.remove(m_noteId);
        m_editor->noteEdited(m_cursor.bar);
    }

    void undo() override
    {
        Score &score = m_editor->mutableScore();
        const int beatId = Editing::beatIdAt(score, m_cursor);
        if (beatId < 0) {
            return;
        }
        score.notes.insert(m_noteId, m_previous);
        score.beats[beatId].notes.insert(
            std::clamp(m_position, 0, int(score.beats.value(beatId).notes.size())), m_noteId);
        m_editor->noteEdited(m_cursor.bar);
    }

private:
    Editor *m_editor;
    Cursor m_cursor;
    int m_noteId = -1;
    int m_position = 0;
    Note m_previous;
};
}

Editor::Editor(QObject *parent)
    : QObject(parent)
    , m_undo(new QUndoStack(this))
{
    connect(m_undo, &QUndoStack::indexChanged, this, &Editor::historyChanged);
    connect(m_undo, &QUndoStack::cleanChanged, this, &Editor::historyChanged);
    m_typing.start();
}

Editor::~Editor() = default;

void Editor::setScore(const Score &score)
{
    m_score = score;
    m_undo->clear();
    m_cursor = Editing::clamped(m_score, Cursor());
    endDigitEntry();
    Q_EMIT scoreEdited(-1);
    Q_EMIT cursorChanged();
    Q_EMIT historyChanged();
}

const Score &Editor::score() const
{
    return m_score;
}

Score &Editor::mutableScore()
{
    return m_score;
}

void Editor::noteEdited(int bar)
{
    Q_EMIT scoreEdited(bar);
}

Cursor Editor::cursor() const
{
    return m_cursor;
}

void Editor::setCursor(const Cursor &cursor)
{
    const Cursor clamped = Editing::clamped(m_score, cursor);
    if (clamped == m_cursor) {
        return;
    }
    m_cursor = clamped;
    // Moving ends a number: the next digit starts a new fret rather than
    // extending one typed somewhere else.
    endDigitEntry();
    Q_EMIT cursorChanged();
}

void Editor::move(Editing::Move move)
{
    setCursor(Editing::moved(m_score, m_cursor, move));
}

int Editor::freshNoteId(const Score &score)
{
    int highest = -1;
    for (auto note = score.notes.constBegin(); note != score.notes.constEnd(); ++note) {
        highest = std::max(highest, note.key());
    }
    return highest + 1;
}

int Editor::midiFor(const Score &score, const Cursor &cursor, int fret)
{
    if (cursor.track < 0 || cursor.track >= score.tracks.size()) {
        return -1;
    }
    const Track &track = score.tracks.at(cursor.track);
    if (cursor.string < 0 || cursor.string >= track.tuning.size()) {
        return -1;
    }
    // The identity the corpus holds to throughout: the open string plus the
    // fret is the note that sounds.
    return track.tuning.at(cursor.string) + fret;
}

void Editor::endDigitEntry()
{
    if (m_pendingFret >= 0) {
        ++m_digitRun;
        m_pendingFret = -1;
    }
}

void Editor::typeDigit(int digit)
{
    if (digit < 0 || digit > 9) {
        return;
    }

    const bool continuing = m_pendingFret >= 0
        && m_typing.elapsed() < DigitRunMilliseconds;
    if (!continuing) {
        endDigitEntry();
    }

    const int fret = continuing ? m_pendingFret * 10 + digit : digit;
    m_typing.restart();

    if (fret > HighestFret) {
        // 37 is a slip rather than a note. The run ends and the digit starts a
        // number of its own, which is what the typist almost certainly meant.
        endDigitEntry();
        m_pendingFret = digit;
        setFret(digit);
        return;
    }

    m_pendingFret = fret;
    setFret(fret);
}

void Editor::setFret(int fret)
{
    if (fret < 0 || fret > HighestFret) {
        return;
    }
    if (Editing::beatIdAt(m_score, m_cursor) < 0) {
        // Nothing to put a note on: adding beats is a separate command, and
        // not one this understands yet.
        return;
    }
    m_undo->push(new SetFretCommand(this, m_cursor, fret, m_digitRun));
}

void Editor::clearNote()
{
    endDigitEntry();
    auto *command = new ClearNoteCommand(this, m_cursor);
    if (!command->isValid()) {
        delete command;
        return;
    }
    m_undo->push(command);
}

void Editor::transposeNote(int frets)
{
    endDigitEntry();
    const int noteId = Editing::noteIdAt(m_score, m_cursor);
    if (noteId < 0) {
        return;
    }
    const int fret = m_score.notes.value(noteId).fret + frets;
    if (fret < 0 || fret > HighestFret) {
        return;
    }
    // Its own command rather than a merged one: transposing twice is two
    // deliberate acts, unlike typing two digits of one number.
    endDigitEntry();
    m_undo->push(new SetFretCommand(this, m_cursor, fret, m_digitRun));
    endDigitEntry();
}

QUndoStack *Editor::undoStack() const
{
    return m_undo;
}

bool Editor::canUndo() const
{
    return m_undo->canUndo();
}

bool Editor::canRedo() const
{
    return m_undo->canRedo();
}

QString Editor::undoText() const
{
    return m_undo->undoText();
}

QString Editor::redoText() const
{
    return m_undo->redoText();
}

void Editor::undo()
{
    endDigitEntry();
    m_undo->undo();
}

void Editor::redo()
{
    endDigitEntry();
    m_undo->redo();
}

bool Editor::isModified() const
{
    return !m_undo->isClean();
}

void Editor::setUnmodified()
{
    m_undo->setClean();
}
