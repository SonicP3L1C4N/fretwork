// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "editor.h"

#include <KLocalizedString>

#include <QUndoCommand>
#include <QUndoStack>

#include <QHash>

#include <algorithm>
#include <utility>

namespace
{
/** A digit typed within this long of the last one extends it rather than replacing it. */
constexpr qint64 DigitRunMilliseconds = 900;

/** Above this is a typing mistake rather than a note. */
constexpr int HighestFret = 36;

/** Commands of the same kind may merge; different kinds never do. */
constexpr int SetFretId = 1;

/** Whether the score has a bar at the cursor for a beat to go into. */
bool hasBarAt(const Score &score, const Cursor &cursor)
{
    if (cursor.bar < 0 || cursor.bar >= score.masterBars.size() || cursor.voice < 0
        || cursor.voice > 3) {
        return false;
    }
    const MasterBar &master = score.masterBars.at(cursor.bar);
    if (cursor.track < 0 || cursor.track >= master.bars.size()) {
        return false;
    }
    return score.bars.contains(master.bars.at(cursor.track));
}

/**
 * How long a beat put in at the caret should last.
 *
 * As long as the one it displaces, or as long as the one before it where there
 * is nothing to displace. A bar of quavers wants another quaver; handing it a
 * crotchet and a bar that no longer adds up would be answering a question
 * nobody asked.
 */
Rational durationForNewBeat(const Score &score, const Cursor &cursor)
{
    Cursor probe = cursor;
    if (Editing::beatIdAt(score, probe) < 0 && probe.beat > 0) {
        --probe.beat;
    }
    return Editing::durationAt(score, probe);
}

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
        // A caret one past the end of a voice, or in a bar with no voice at
        // all, is pointing at a beat that has to be made before a fret can go
        // on it. Made here rather than by a second command, so that one undo
        // takes away one act.
        m_madeBeat = Editing::beatIdAt(score, cursor) < 0;
        m_duration = durationForNewBeat(score, cursor);
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
        const int beatId = m_madeBeat ? makeBeat(score) : Editing::beatIdAt(score, m_cursor);
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
        if (m_madeBeat) {
            unmakeBeat(score);
        }
        m_editor->noteEdited(m_cursor.bar);
    }

private:
    /** The beat this note needs, appended where the caret is sitting. */
    int makeBeat(Score &score)
    {
        const int voiceId = Editor::voiceForEditing(score, m_cursor, &m_madeVoice);
        if (voiceId < 0) {
            return -1;
        }
        if (m_beatId < 0) {
            m_beatId = Editor::freshBeatId(score);
        }
        Beat beat;
        beat.rhythm = Editor::rhythmIdFor(score, m_duration);
        score.beats.insert(m_beatId, beat);
        QList<int> &beats = score.voices[voiceId].beats;
        beats.insert(std::clamp(m_cursor.beat, 0, int(beats.size())), m_beatId);
        return m_beatId;
    }

    void unmakeBeat(Score &score)
    {
        const int voiceId = Editing::voiceIdAt(score, m_cursor);
        if (voiceId >= 0) {
            score.voices[voiceId].beats.removeAll(m_beatId);
        }
        score.beats.remove(m_beatId);
        if (m_madeVoice) {
            Editor::dropVoice(score, m_cursor);
        }
    }

    Editor *m_editor;
    Cursor m_cursor;
    int m_fret;
    int m_run;
    int m_noteId = -1;
    bool m_created = false;
    Note m_previous;

    bool m_madeBeat = false;
    bool m_madeVoice = false;
    int m_beatId = -1;
    Rational m_duration;
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

/**
 * Making room: an empty beat at the caret, and everything after it later.
 *
 * A rest rather than a note, because there is no way to know what is going on
 * it yet, and because a beat with nothing on it is a real thing to want -- it
 * is how a rest gets written.
 */
class InsertBeatCommand : public QUndoCommand
{
public:
    InsertBeatCommand(Editor *editor, const Cursor &cursor)
        : m_editor(editor)
        , m_cursor(cursor)
        , m_duration(durationForNewBeat(editor->score(), cursor))
    {
        setText(i18n("Insert beat"));
    }

    void redo() override
    {
        Score &score = m_editor->mutableScore();
        const int voiceId = Editor::voiceForEditing(score, m_cursor, &m_madeVoice);
        if (voiceId < 0) {
            return;
        }
        if (m_beatId < 0) {
            m_beatId = Editor::freshBeatId(score);
        }
        Beat beat;
        beat.rhythm = Editor::rhythmIdFor(score, m_duration);
        score.beats.insert(m_beatId, beat);
        QList<int> &beats = score.voices[voiceId].beats;
        beats.insert(std::clamp(m_cursor.beat, 0, int(beats.size())), m_beatId);
        m_editor->noteEdited(m_cursor.bar);
    }

    void undo() override
    {
        Score &score = m_editor->mutableScore();
        const int voiceId = Editing::voiceIdAt(score, m_cursor);
        if (voiceId >= 0) {
            score.voices[voiceId].beats.removeAll(m_beatId);
        }
        score.beats.remove(m_beatId);
        if (m_madeVoice) {
            Editor::dropVoice(score, m_cursor);
        }
        m_editor->noteEdited(m_cursor.bar);
    }

private:
    Editor *m_editor;
    Cursor m_cursor;
    Rational m_duration;
    int m_beatId = -1;
    bool m_madeVoice = false;
};

/**
 * Taking a beat out, and putting it back with its notes and their ids.
 *
 * The ids matter: a note is referred to by one, and restoring a chord under
 * different numbers would be a different chord as far as anything holding on
 * to them is concerned.
 */
class DeleteBeatCommand : public QUndoCommand
{
public:
    DeleteBeatCommand(Editor *editor, const Cursor &cursor)
        : m_editor(editor)
        , m_cursor(cursor)
    {
        const Score &score = editor->score();
        m_beatId = Editing::beatIdAt(score, cursor);
        if (m_beatId >= 0) {
            m_beat = score.beats.value(m_beatId);
            for (const int noteId : m_beat.notes) {
                m_notes.insert(noteId, score.notes.value(noteId));
            }
            const int voiceId = Editing::voiceIdAt(score, cursor);
            m_position = int(score.voices.value(voiceId).beats.indexOf(m_beatId));
        }
        setText(i18n("Delete beat"));
    }

    bool isValid() const
    {
        return m_beatId >= 0;
    }

    void redo() override
    {
        Score &score = m_editor->mutableScore();
        const int voiceId = Editing::voiceIdAt(score, m_cursor);
        if (voiceId < 0) {
            return;
        }
        score.voices[voiceId].beats.removeAll(m_beatId);
        score.beats.remove(m_beatId);
        for (auto note = m_notes.constBegin(); note != m_notes.constEnd(); ++note) {
            score.notes.remove(note.key());
        }
        m_editor->noteEdited(m_cursor.bar);
    }

    void undo() override
    {
        Score &score = m_editor->mutableScore();
        const int voiceId = Editing::voiceIdAt(score, m_cursor);
        if (voiceId < 0) {
            return;
        }
        for (auto note = m_notes.constBegin(); note != m_notes.constEnd(); ++note) {
            score.notes.insert(note.key(), note.value());
        }
        score.beats.insert(m_beatId, m_beat);
        QList<int> &beats = score.voices[voiceId].beats;
        beats.insert(std::clamp(m_position, 0, int(beats.size())), m_beatId);
        m_editor->noteEdited(m_cursor.bar);
    }

private:
    Editor *m_editor;
    Cursor m_cursor;
    int m_beatId = -1;
    int m_position = 0;
    Beat m_beat;
    QHash<int, Note> m_notes;
};

/**
 * Taking a run of beats out, across as many bars as it covers.
 *
 * Everything is remembered by id and by value both: the beats and their notes
 * go back under the numbers they had, in the places they were, because a
 * selection deleted and undone has to leave a score that is not merely
 * equivalent to the one before but identical to it.
 */
class DeleteRangeCommand : public QUndoCommand
{
public:
    DeleteRangeCommand(Editor *editor, const Editing::Range &range)
        : m_editor(editor)
    {
        const Score &score = editor->score();
        for (int bar = range.from.bar; bar <= range.to.bar; ++bar) {
            Cursor at = range.from;
            at.bar = bar;
            const int beats = Editing::beatCount(score, at);
            for (int beat = 0; beat < beats; ++beat) {
                if (!range.holds(bar, beat)) {
                    continue;
                }
                at.beat = beat;
                Removed removed;
                removed.at = at;
                removed.beatId = Editing::beatIdAt(score, at);
                if (removed.beatId < 0) {
                    continue;
                }
                removed.beat = score.beats.value(removed.beatId);
                for (const int noteId : removed.beat.notes) {
                    removed.notes.insert(noteId, score.notes.value(noteId));
                }
                m_removed.append(removed);
            }
        }
        setText(i18np("Delete beat", "Delete %1 beats", int(m_removed.size())));
    }

    bool isValid() const
    {
        return !m_removed.isEmpty();
    }

    void redo() override
    {
        Score &score = m_editor->mutableScore();
        for (const Removed &removed : m_removed) {
            const int voiceId = Editing::voiceIdAt(score, removed.at);
            if (voiceId >= 0) {
                // By id rather than by index: the indices behind this one have
                // already moved by the time we reach it.
                score.voices[voiceId].beats.removeAll(removed.beatId);
            }
            score.beats.remove(removed.beatId);
            for (auto note = removed.notes.constBegin(); note != removed.notes.constEnd();
                 ++note) {
                score.notes.remove(note.key());
            }
        }
        m_editor->noteEdited(-1);
    }

    void undo() override
    {
        Score &score = m_editor->mutableScore();
        // Forwards, so that each beat finds the index it came from already
        // occupied by the ones that were in front of it.
        for (const Removed &removed : m_removed) {
            const int voiceId = Editor::voiceForEditing(score, removed.at, nullptr);
            if (voiceId < 0) {
                continue;
            }
            for (auto note = removed.notes.constBegin(); note != removed.notes.constEnd();
                 ++note) {
                score.notes.insert(note.key(), note.value());
            }
            score.beats.insert(removed.beatId, removed.beat);
            QList<int> &beats = score.voices[voiceId].beats;
            beats.insert(std::clamp(removed.at.beat, 0, int(beats.size())), removed.beatId);
        }
        m_editor->noteEdited(-1);
    }

private:
    struct Removed {
        Cursor at;
        int beatId = -1;
        Beat beat;
        QHash<int, Note> notes;
    };

    Editor *m_editor;
    QList<Removed> m_removed;
};

/**
 * Putting the clipboard in at the caret, bar for bar.
 *
 * The ids are worked out once and then kept, so that undoing and redoing a
 * paste puts the same music back under the same numbers rather than a fresh
 * copy each time. That is safe because a stack cannot redo past a command that
 * has been superseded: nothing else can have taken the numbers in the meantime.
 */
class PasteCommand : public QUndoCommand
{
public:
    PasteCommand(Editor *editor, const Cursor &at, const Clip &clip)
        : m_editor(editor)
    {
        const Score &score = editor->score();
        int nextBeat = Editor::freshBeatId(score);
        int nextNote = Editor::freshNoteId(score);

        for (int index = 0; index < clip.bars.size(); ++index) {
            Landing landing;
            landing.at = at;
            landing.at.bar = at.bar + index;
            // The first bar's worth lands at the caret; the rest start their
            // own bars, which is where they were when they were copied.
            landing.at.beat = index == 0 ? at.beat : 0;
            for (const Clip::Item &item : clip.bars.at(index)) {
                landing.items.append(item);
                landing.beatIds.append(nextBeat++);
                QList<int> noteIds;
                for (int note = 0; note < item.notes.size(); ++note) {
                    noteIds.append(nextNote++);
                }
                landing.noteIds.append(noteIds);
            }
            m_landings.append(landing);
        }
        setText(i18np("Paste beat", "Paste %1 beats", clip.beatCount()));
    }

    void redo() override
    {
        Score &score = m_editor->mutableScore();
        for (Landing &landing : m_landings) {
            const int voiceId = Editor::voiceForEditing(score, landing.at, &landing.madeVoice);
            if (voiceId < 0) {
                continue;
            }
            QList<int> &beats = score.voices[voiceId].beats;
            for (int index = 0; index < landing.items.size(); ++index) {
                const Clip::Item &item = landing.items.at(index);
                Beat beat = item.beat;
                beat.rhythm = Editor::rhythmIdFor(score, item.duration);
                beat.notes.clear();
                for (int note = 0; note < item.notes.size(); ++note) {
                    const int noteId = landing.noteIds.at(index).at(note);
                    score.notes.insert(noteId, item.notes.at(note));
                    beat.notes.append(noteId);
                }
                score.beats.insert(landing.beatIds.at(index), beat);
                beats.insert(std::clamp(landing.at.beat + index, 0, int(beats.size())),
                             landing.beatIds.at(index));
            }
        }
        m_editor->noteEdited(-1);
    }

    void undo() override
    {
        Score &score = m_editor->mutableScore();
        for (const Landing &landing : m_landings) {
            const int voiceId = Editing::voiceIdAt(score, landing.at);
            for (int index = 0; index < landing.beatIds.size(); ++index) {
                if (voiceId >= 0) {
                    score.voices[voiceId].beats.removeAll(landing.beatIds.at(index));
                }
                score.beats.remove(landing.beatIds.at(index));
                for (const int noteId : landing.noteIds.at(index)) {
                    score.notes.remove(noteId);
                }
            }
            if (landing.madeVoice) {
                Editor::dropVoice(score, landing.at);
            }
        }
        m_editor->noteEdited(-1);
    }

private:
    struct Landing {
        Cursor at;
        QList<Clip::Item> items;
        QList<int> beatIds;
        QList<QList<int>> noteIds;
        bool madeVoice = false;
    };

    Editor *m_editor;
    QList<Landing> m_landings;
};

/**
 * Changing how long a beat lasts, which is the one thing a fret cannot say.
 *
 * The beat keeps its notes: a quaver that becomes a crotchet is the same
 * fingering held longer, and anybody who wanted a different note would have
 * typed one. What changes is which duration the beat points at, so this is a
 * command about an id and nothing else -- which is also why undoing it is
 * exact rather than approximately exact.
 */
class SetDurationCommand : public QUndoCommand
{
public:
    SetDurationCommand(Editor *editor, const Cursor &cursor, const Rational &duration)
        : m_editor(editor)
        , m_cursor(cursor)
        , m_duration(duration)
    {
        const Score &score = editor->score();
        m_previous = score.beats.value(Editing::beatIdAt(score, cursor)).rhythm;
        setText(i18n("Set duration"));
    }

    void redo() override
    {
        Score &score = m_editor->mutableScore();
        const int beatId = Editing::beatIdAt(score, m_cursor);
        if (beatId < 0) {
            return;
        }
        score.beats[beatId].rhythm = Editor::rhythmIdFor(score, m_duration);
        m_editor->noteEdited(m_cursor.bar);
    }

    void undo() override
    {
        Score &score = m_editor->mutableScore();
        const int beatId = Editing::beatIdAt(score, m_cursor);
        if (beatId < 0) {
            return;
        }
        score.beats[beatId].rhythm = m_previous;
        m_editor->noteEdited(m_cursor.bar);
    }

private:
    Editor *m_editor;
    Cursor m_cursor;
    Rational m_duration;
    int m_previous = -1;
};

/**
 * Moving notes along the strings they are already on.
 *
 * A fret and a pitch move together or the score stops meaning anything: the
 * number on the page and the note that sounds are two descriptions of one
 * thing, and a program that changed one of them would be drawing music it
 * does not play.
 *
 * It carries a list rather than a note because transposing a phrase is one
 * act. Eight notes moved and a ninth left where it was because it would not
 * fit is not the phrase anybody asked for, so the editor checks all of them
 * before pushing this at all.
 */
class TransposeCommand : public QUndoCommand
{
public:
    TransposeCommand(Editor *editor, const QList<int> &notes, int frets)
        : m_editor(editor)
        , m_notes(notes)
        , m_frets(frets)
    {
        setText(i18np("Transpose note", "Transpose %1 notes", int(notes.size())));
    }

    void redo() override
    {
        move(m_frets);
    }

    void undo() override
    {
        move(-m_frets);
    }

private:
    void move(int frets)
    {
        Score &score = m_editor->mutableScore();
        for (const int noteId : std::as_const(m_notes)) {
            const auto found = score.notes.find(noteId);
            if (found == score.notes.end()) {
                continue;
            }
            found->fret += frets;
            // A note the importer could not give a pitch to keeps not having
            // one: -1 means "no pitch", and arithmetic on it would invent a
            // note somewhere near the bottom of the piano.
            if (found->midi >= 0) {
                found->midi += frets;
            }
        }
        m_editor->noteEdited(-1);
    }

    Editor *m_editor;
    QList<int> m_notes;
    int m_frets;
};

/**
 * The same note, played somewhere else on the neck.
 *
 * This is the one edit that deliberately changes the fret and *not* the pitch:
 * moving a note to the next string is a fingering decision, and the whole
 * point of it is that the music does not change. The fret it lands on is
 * whatever makes that true, which is why it is worked out from the tuning
 * rather than carried across.
 */
class MoveNoteAcrossCommand : public QUndoCommand
{
public:
    MoveNoteAcrossCommand(Editor *editor, int noteId, int string, int fret)
        : m_editor(editor)
        , m_noteId(noteId)
        , m_string(string)
        , m_fret(fret)
    {
        const Note note = editor->score().notes.value(noteId);
        m_wasString = note.string;
        m_wasFret = note.fret;
        setText(i18n("Move note to another string"));
    }

    void redo() override
    {
        put(m_string, m_fret);
    }

    void undo() override
    {
        put(m_wasString, m_wasFret);
    }

private:
    void put(int string, int fret)
    {
        Score &score = m_editor->mutableScore();
        const auto found = score.notes.find(m_noteId);
        if (found == score.notes.end()) {
            return;
        }
        found->string = string;
        found->fret = fret;
        m_editor->noteEdited(-1);
    }

    Editor *m_editor;
    int m_noteId;
    int m_string;
    int m_fret;
    int m_wasString = 0;
    int m_wasFret = 0;
};

/** Where a mark lives on a note. One place, so nothing can disagree about it. */
bool *markOf(Note &note, Editor::Mark mark)
{
    switch (mark) {
    case Editor::Mark::Dead:
        return &note.muted;
    case Editor::Mark::Ghost:
        return &note.ghost;
    case Editor::Mark::PalmMute:
        return &note.palmMuted;
    case Editor::Mark::LetRing:
        return &note.letRing;
    }
    return nullptr;
}

bool hasMark(const Note &note, Editor::Mark mark)
{
    Note copy = note;
    const bool *flag = markOf(copy, mark);
    return flag && *flag;
}

/**
 * Marking notes, and unmarking them.
 *
 * The previous value of every note is kept rather than assumed, because a
 * selection is rarely uniform: palm-muting four bars where the first bar
 * already was has to leave that first bar alone when it is undone, or the undo
 * has quietly edited music the person never touched.
 */
class ToggleMarkCommand : public QUndoCommand
{
public:
    ToggleMarkCommand(Editor *editor, const QList<int> &notes, Editor::Mark mark, bool on)
        : m_editor(editor)
        , m_mark(mark)
        , m_on(on)
    {
        const Score &score = editor->score();
        for (const int noteId : notes) {
            m_was.insert(noteId, hasMark(score.notes.value(noteId), mark));
        }
        setText(nameOf(mark, on, int(notes.size())));
    }

    void redo() override
    {
        Score &score = m_editor->mutableScore();
        for (auto note = m_was.constBegin(); note != m_was.constEnd(); ++note) {
            set(score, note.key(), m_on);
        }
        m_editor->noteEdited(-1);
    }

    void undo() override
    {
        Score &score = m_editor->mutableScore();
        for (auto note = m_was.constBegin(); note != m_was.constEnd(); ++note) {
            set(score, note.key(), note.value());
        }
        m_editor->noteEdited(-1);
    }

private:
    static QString nameOf(Editor::Mark mark, bool on, int count)
    {
        switch (mark) {
        case Editor::Mark::Dead:
            return on ? i18np("Dead note", "Dead notes", count) : i18n("Undo dead note");
        case Editor::Mark::Ghost:
            return on ? i18np("Ghost note", "Ghost notes", count) : i18n("Undo ghost note");
        case Editor::Mark::PalmMute:
            return on ? i18n("Palm mute") : i18n("Stop palm muting");
        case Editor::Mark::LetRing:
            return on ? i18n("Let ring") : i18n("Stop letting ring");
        }
        return i18n("Mark note");
    }

    void set(Score &score, int noteId, bool on)
    {
        const auto found = score.notes.find(noteId);
        if (found == score.notes.end()) {
            return;
        }
        bool *flag = markOf(*found, m_mark);
        if (flag) {
            *flag = on;
        }
    }

    Editor *m_editor;
    Editor::Mark m_mark;
    bool m_on;
    QHash<int, bool> m_was;
};

/**
 * A bar put into the score, across every track at once.
 *
 * A master bar is the score's own unit of time, and a bar added to one track
 * and not the others would put every track after it out of step for the rest
 * of the piece. So this makes a bar for each track and one master bar that
 * names them, and there is no way to ask for less.
 *
 * The time signature comes from the bar being displaced, because that is the
 * one whose music is having room made in it -- a bar added to a piece in 6/8
 * is in 6/8. The section name and the repeat signs do not come with it: those
 * were written on a particular bar, and a "Chorus" that suddenly starts a bar
 * early is a worse mistake than one that has to be typed again.
 */
class InsertBarCommand : public QUndoCommand
{
public:
    InsertBarCommand(Editor *editor, int index)
        : m_editor(editor)
    {
        const Score &score = editor->score();
        m_index = std::clamp(index, 0, int(score.masterBars.size()));

        // The bar being displaced, or the last one where there is nothing to
        // displace. A score with no bars at all gets four four, which is the
        // only answer available.
        const int model = std::min(m_index, int(score.masterBars.size()) - 1);
        if (model >= 0) {
            m_numerator = score.masterBars.at(model).numerator;
            m_denominator = score.masterBars.at(model).denominator;
        }

        // Worked out once and then kept, so that undoing and redoing puts the
        // same bar back under the same numbers rather than a fresh one each
        // time -- the same reason a paste keeps its ids.
        int nextBar = Editor::freshBarId(score);
        for (int track = 0; track < score.tracks.size(); ++track) {
            m_barIds.append(nextBar++);
        }
        setText(i18n("Insert bar"));
    }

    void redo() override
    {
        Score &score = m_editor->mutableScore();
        for (const int barId : m_barIds) {
            // Four empty slots: a voice is addressed by its position, and one
            // gets made the moment something is typed into it.
            score.bars.insert(barId, Bar{{-1, -1, -1, -1}});
        }
        MasterBar master;
        master.bars = m_barIds;
        master.numerator = m_numerator;
        master.denominator = m_denominator;
        score.masterBars.insert(m_index, master);

        // Everything written at or after the new bar is a bar later than it
        // was. A tempo change left pointing at the index it used to have is a
        // tempo change that moved.
        for (TempoChange &tempo : score.tempos) {
            if (tempo.bar >= m_index) {
                ++tempo.bar;
            }
        }
        m_editor->noteEdited(-1);
    }

    void undo() override
    {
        Score &score = m_editor->mutableScore();
        if (m_index < score.masterBars.size()) {
            score.masterBars.removeAt(m_index);
        }
        for (const int barId : m_barIds) {
            score.bars.remove(barId);
        }
        // Past the bar rather than at it: the bar being taken away carries no
        // tempo of its own, so everything still sitting at its index is what
        // the redo pushed there.
        for (TempoChange &tempo : score.tempos) {
            if (tempo.bar > m_index) {
                --tempo.bar;
            }
        }
        m_editor->noteEdited(-1);
    }

private:
    Editor *m_editor;
    int m_index = 0;
    int m_numerator = 4;
    int m_denominator = 4;
    QList<int> m_barIds;
};

/**
 * Taking a bar out of the score, with every track's share of it.
 *
 * Everything in it is remembered by id as well as by value -- the bars, their
 * voices, the beats and the notes -- so that undoing puts back the score that
 * was there rather than one that merely sounds the same. A bar is the largest
 * thing this editor can delete in one act, and it is the one where getting
 * reversal wrong loses the most.
 *
 * The last bar of a score cannot be deleted. A score with no bars is not a
 * shorter score; it is a thing the rest of the program treats as empty.
 */
class DeleteBarCommand : public QUndoCommand
{
public:
    DeleteBarCommand(Editor *editor, int index)
        : m_editor(editor)
        , m_index(index)
    {
        const Score &score = editor->score();
        m_valid = index >= 0 && index < score.masterBars.size() && score.masterBars.size() > 1;
        if (m_valid) {
            m_master = score.masterBars.at(index);
            for (const int barId : m_master.bars) {
                const auto bar = score.bars.constFind(barId);
                if (bar == score.bars.constEnd()) {
                    continue;
                }
                m_bars.insert(barId, *bar);
                for (const int voiceId : bar->voices) {
                    const auto voice = score.voices.constFind(voiceId);
                    if (voice == score.voices.constEnd()) {
                        continue;
                    }
                    m_voices.insert(voiceId, *voice);
                    for (const int beatId : voice->beats) {
                        const auto beat = score.beats.constFind(beatId);
                        if (beat == score.beats.constEnd()) {
                            continue;
                        }
                        m_beats.insert(beatId, *beat);
                        for (const int noteId : beat->notes) {
                            m_notes.insert(noteId, score.notes.value(noteId));
                        }
                    }
                }
            }
            m_tempos = score.tempos;
        }
        setText(i18n("Delete bar"));
    }

    bool isValid() const
    {
        return m_valid;
    }

    void redo() override
    {
        Score &score = m_editor->mutableScore();
        for (auto note = m_notes.constBegin(); note != m_notes.constEnd(); ++note) {
            score.notes.remove(note.key());
        }
        for (auto beat = m_beats.constBegin(); beat != m_beats.constEnd(); ++beat) {
            score.beats.remove(beat.key());
        }
        for (auto voice = m_voices.constBegin(); voice != m_voices.constEnd(); ++voice) {
            score.voices.remove(voice.key());
        }
        for (auto bar = m_bars.constBegin(); bar != m_bars.constEnd(); ++bar) {
            score.bars.remove(bar.key());
        }
        if (m_index < score.masterBars.size()) {
            score.masterBars.removeAt(m_index);
        }

        // A tempo change written in this bar goes with it -- it was a change
        // made at a moment that is no longer in the piece. The exception is
        // the one the score starts at: losing that would not shorten the score
        // but silently re-time all of it, so it moves to the front of whatever
        // bar takes this one's place.
        QList<TempoChange> kept;
        for (int index = 0; index < m_tempos.size(); ++index) {
            TempoChange tempo = m_tempos.at(index);
            if (tempo.bar == m_index) {
                if (index > 0) {
                    continue;
                }
                tempo.bar = std::min(m_index, int(score.masterBars.size()) - 1);
                tempo.position = 0;
            } else if (tempo.bar > m_index) {
                --tempo.bar;
            }
            kept.append(tempo);
        }
        score.tempos = kept;
        m_editor->noteEdited(-1);
    }

    void undo() override
    {
        Score &score = m_editor->mutableScore();
        for (auto note = m_notes.constBegin(); note != m_notes.constEnd(); ++note) {
            score.notes.insert(note.key(), note.value());
        }
        for (auto beat = m_beats.constBegin(); beat != m_beats.constEnd(); ++beat) {
            score.beats.insert(beat.key(), beat.value());
        }
        for (auto voice = m_voices.constBegin(); voice != m_voices.constEnd(); ++voice) {
            score.voices.insert(voice.key(), voice.value());
        }
        for (auto bar = m_bars.constBegin(); bar != m_bars.constEnd(); ++bar) {
            score.bars.insert(bar.key(), bar.value());
        }
        score.masterBars.insert(std::clamp(m_index, 0, int(score.masterBars.size())), m_master);
        // Kept whole rather than shifted back: a bar that carried the tempo
        // the score starts at cannot be restored by moving indices around.
        score.tempos = m_tempos;
        m_editor->noteEdited(-1);
    }

private:
    Editor *m_editor;
    int m_index = 0;
    bool m_valid = false;
    MasterBar m_master;
    QHash<int, Bar> m_bars;
    QHash<int, Voice> m_voices;
    QHash<int, Beat> m_beats;
    QHash<int, Note> m_notes;
    QList<TempoChange> m_tempos;
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
    m_selecting = false;
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

void Editor::setCursor(const Cursor &cursor, bool extend)
{
    const Cursor next = Editing::clamped(m_score, cursor);
    const bool wasSelecting = m_selecting;
    const Cursor wasAnchor = m_anchor;

    if (extend) {
        if (!m_selecting) {
            // The selection starts from where the caret was, not from where it
            // is going: the beat you were on is part of what you are selecting.
            m_anchor = m_cursor;
            m_selecting = true;
        }
        if (next.track != m_anchor.track || next.voice != m_anchor.voice) {
            m_selecting = false;
        }
    } else {
        m_selecting = false;
    }

    if (next == m_cursor && m_selecting == wasSelecting && m_anchor == wasAnchor) {
        return;
    }
    m_cursor = next;
    // Moving ends a number: the next digit starts a new fret rather than
    // extending one typed somewhere else.
    endDigitEntry();
    Q_EMIT cursorChanged();
}

void Editor::move(Editing::Move move, bool extend)
{
    setCursor(Editing::moved(m_score, m_cursor, move), extend);
}

bool Editor::hasSelection() const
{
    return m_selecting && !(m_anchor == m_cursor);
}

Editing::Range Editor::selection() const
{
    return hasSelection() ? Editing::ordered(m_anchor, m_cursor)
                          : Editing::Range{m_cursor, m_cursor};
}

void Editor::clearSelection()
{
    if (!m_selecting) {
        return;
    }
    m_selecting = false;
    Q_EMIT cursorChanged();
}

const Clip &Editor::clip() const
{
    return m_clip;
}

bool Editor::canPaste() const
{
    return !m_clip.isEmpty();
}

void Editor::copy()
{
    endDigitEntry();
    const Editing::Range range = selection();

    Clip clip;
    for (int bar = range.from.bar; bar <= range.to.bar; ++bar) {
        Cursor at = m_cursor;
        at.bar = bar;
        QList<Clip::Item> items;
        const int beats = Editing::beatCount(m_score, at);
        for (int beat = 0; beat < beats; ++beat) {
            if (!range.holds(bar, beat)) {
                continue;
            }
            at.beat = beat;
            const int beatId = Editing::beatIdAt(m_score, at);
            if (beatId < 0) {
                continue;
            }
            Clip::Item item;
            item.beat = m_score.beats.value(beatId);
            item.duration = Editing::durationAt(m_score, at);
            for (const int noteId : item.beat.notes) {
                item.notes.append(m_score.notes.value(noteId));
            }
            items.append(item);
        }
        clip.bars.append(items);
    }

    // Copying nothing leaves what was copied before, the way every other
    // clipboard behaves: a missed selection should not lose your last copy.
    if (!clip.isEmpty()) {
        m_clip = clip;
    }
}

void Editor::deleteSelection()
{
    endDigitEntry();
    auto *command = new DeleteRangeCommand(this, selection());
    if (!command->isValid()) {
        delete command;
        return;
    }
    m_undo->push(command);
    clearSelection();
    setCursor(m_cursor);
}

void Editor::cut()
{
    copy();
    deleteSelection();
}

bool Editor::paste()
{
    endDigitEntry();
    if (m_clip.isEmpty()) {
        return false;
    }

    // Every bar it would land in has to exist first. Half a paste is worse
    // than none: the half that landed has to be found and undone by hand.
    const int lastBar = m_cursor.bar + int(m_clip.bars.size()) - 1;
    if (lastBar >= m_score.masterBars.size()) {
        return false;
    }
    for (int bar = m_cursor.bar; bar <= lastBar; ++bar) {
        Cursor at = m_cursor;
        at.bar = bar;
        if (!hasBarAt(m_score, at)) {
            return false;
        }
    }

    clearSelection();
    m_undo->push(new PasteCommand(this, m_cursor, m_clip));
    return true;
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
    // An edit that touches one beat must not leave a selection on screen
    // saying it touched several.
    clearSelection();
    // Where there is no beat under the caret the command makes one, so the only
    // thing that stops it here is a score with no bar to put it in.
    if (Editing::beatIdAt(m_score, m_cursor) < 0 && !hasBarAt(m_score, m_cursor)) {
        return;
    }
    m_undo->push(new SetFretCommand(this, m_cursor, fret, m_digitRun));
}

void Editor::clearNote()
{
    endDigitEntry();
    clearSelection();
    auto *command = new ClearNoteCommand(this, m_cursor);
    if (!command->isValid()) {
        delete command;
        return;
    }
    m_undo->push(command);
}

QList<int> Editor::notesToMove() const
{
    QList<int> notes;
    if (!hasSelection()) {
        const int noteId = Editing::noteIdAt(m_score, m_cursor);
        if (noteId >= 0) {
            notes.append(noteId);
        }
        return notes;
    }

    // Every note of every selected beat, on every string: a phrase is
    // transposed as a phrase, and picking out the caret's own string would be
    // answering a question nobody asked.
    const Editing::Range range = selection();
    for (int bar = range.from.bar; bar <= range.to.bar; ++bar) {
        Cursor at = m_cursor;
        at.bar = bar;
        const int beats = Editing::beatCount(m_score, at);
        for (int beat = 0; beat < beats; ++beat) {
            if (!range.holds(bar, beat)) {
                continue;
            }
            at.beat = beat;
            const int beatId = Editing::beatIdAt(m_score, at);
            if (beatId < 0) {
                continue;
            }
            notes.append(m_score.beats.value(beatId).notes);
        }
    }
    return notes;
}

Editor::Edit Editor::transpose(int frets)
{
    endDigitEntry();
    const QList<int> notes = notesToMove();
    if (notes.isEmpty() || frets == 0) {
        return Edit::Nothing;
    }

    // All of them or none of them, checked before anything moves. A phrase
    // with one note left behind because it would not fit is not the phrase
    // that was asked for, and it is worse than a refusal because it looks
    // like it worked.
    for (const int noteId : notes) {
        const int fret = m_score.notes.value(noteId).fret + frets;
        if (fret < 0 || fret > HighestFret) {
            return Edit::Refused;
        }
    }

    // Its own command rather than a merged one: transposing twice is two
    // deliberate acts, unlike typing two digits of one number. The selection
    // stays where it is, because it is the thing being worked on.
    m_undo->push(new TransposeCommand(this, notes, frets));
    return Edit::Done;
}

Editor::Edit Editor::toggleMark(Mark mark)
{
    endDigitEntry();
    const QList<int> notes = notesToMove();
    if (notes.isEmpty()) {
        return Edit::Nothing;
    }

    // On unless every one of them is on already. "Palm mute this" is what a
    // person means the first time and "stop" is what they mean the second, and
    // flipping each note separately would turn a half-marked phrase inside out
    // rather than finishing the job.
    bool marked = true;
    for (const int noteId : notes) {
        if (!hasMark(m_score.notes.value(noteId), mark)) {
            marked = false;
            break;
        }
    }

    // The selection stays: marking a phrase and then marking it differently is
    // two edits to the same phrase.
    m_undo->push(new ToggleMarkCommand(this, notes, mark, !marked));
    return Edit::Done;
}

Editor::Edit Editor::moveNoteAcross(int strings)
{
    endDigitEntry();
    const int noteId = Editing::noteIdAt(m_score, m_cursor);
    if (noteId < 0 || strings == 0) {
        return Edit::Nothing;
    }
    if (m_cursor.track < 0 || m_cursor.track >= m_score.tracks.size()) {
        return Edit::Nothing;
    }

    const QList<int> tuning = m_score.tracks.at(m_cursor.track).tuning;
    const int string = m_cursor.string + strings;
    if (string < 0 || string >= tuning.size()) {
        return Edit::Refused;
    }

    // The fret that sounds the same note on the string it is landing on. A
    // drum kit has no tuning and so has no answer to this, which is the same
    // as saying the question does not apply to it.
    const Note note = m_score.notes.value(noteId);
    const int fret = note.midi - tuning.at(string);
    if (fret < 0 || fret > HighestFret) {
        return Edit::Refused;
    }

    Cursor destination = m_cursor;
    destination.string = string;
    // Two notes on one string at one moment is not a chord, it is a mistake.
    if (Editing::noteIdAt(m_score, destination) >= 0) {
        return Edit::Refused;
    }

    clearSelection();
    m_undo->push(new MoveNoteAcrossCommand(this, noteId, string, fret));
    // The caret goes with the note: it is still the note being worked on.
    setCursor(destination);
    return Edit::Done;
}

int Editor::freshBeatId(const Score &score)
{
    int highest = -1;
    for (auto beat = score.beats.constBegin(); beat != score.beats.constEnd(); ++beat) {
        highest = std::max(highest, beat.key());
    }
    return highest + 1;
}

int Editor::freshBarId(const Score &score)
{
    int highest = -1;
    for (auto bar = score.bars.constBegin(); bar != score.bars.constEnd(); ++bar) {
        highest = std::max(highest, bar.key());
    }
    return highest + 1;
}

int Editor::freshVoiceId(const Score &score)
{
    int highest = -1;
    for (auto voice = score.voices.constBegin(); voice != score.voices.constEnd(); ++voice) {
        highest = std::max(highest, voice.key());
    }
    return highest + 1;
}

int Editor::voiceForEditing(Score &score, const Cursor &cursor, bool *created)
{
    if (created) {
        *created = false;
    }
    if (!hasBarAt(score, cursor)) {
        return -1;
    }

    Bar &bar = score.bars[score.masterBars.at(cursor.bar).bars.at(cursor.track)];
    // Four slots, whatever the file happened to write: a voice is addressed by
    // its position and an absent one is a -1 rather than a missing entry.
    while (bar.voices.size() <= cursor.voice) {
        bar.voices.append(-1);
    }
    if (bar.voices.at(cursor.voice) >= 0) {
        return bar.voices.at(cursor.voice);
    }

    const int voiceId = freshVoiceId(score);
    score.voices.insert(voiceId, Voice());
    bar.voices[cursor.voice] = voiceId;
    if (created) {
        *created = true;
    }
    return voiceId;
}

void Editor::dropVoice(Score &score, const Cursor &cursor)
{
    const int voiceId = Editing::voiceIdAt(score, cursor);
    if (voiceId < 0) {
        return;
    }
    score.voices.remove(voiceId);
    const int barId = score.masterBars.at(cursor.bar).bars.at(cursor.track);
    score.bars[barId].voices[cursor.voice] = -1;
}

void Editor::insertBeat()
{
    endDigitEntry();
    clearSelection();
    if (!hasBarAt(m_score, m_cursor)) {
        return;
    }
    m_undo->push(new InsertBeatCommand(this, m_cursor));
}

void Editor::deleteBeat()
{
    endDigitEntry();
    clearSelection();
    auto *command = new DeleteBeatCommand(this, m_cursor);
    if (!command->isValid()) {
        delete command;
        return;
    }
    m_undo->push(command);
    // The beat the caret was on is gone; it now points at whatever moved up,
    // or at the end of the voice if nothing did.
    setCursor(m_cursor);
}

bool Editor::canDeleteBar() const
{
    return m_cursor.bar >= 0 && m_cursor.bar < m_score.masterBars.size()
        && m_score.masterBars.size() > 1;
}

void Editor::insertBar()
{
    endDigitEntry();
    clearSelection();
    if (m_score.tracks.isEmpty() || m_score.masterBars.isEmpty()) {
        return;
    }
    m_undo->push(new InsertBarCommand(this, m_cursor.bar));
    // The caret has not moved, and what it was on has: it is now sitting at
    // the front of an empty bar, which is where a number wants typing next.
    setCursor(m_cursor);
}

void Editor::appendBar()
{
    endDigitEntry();
    clearSelection();
    if (m_score.tracks.isEmpty()) {
        return;
    }
    m_undo->push(new InsertBarCommand(this, int(m_score.masterBars.size())));
    // Into the new bar, because a bar added at the end of a piece is a bar
    // somebody is about to write in.
    Cursor at = m_cursor;
    at.bar = int(m_score.masterBars.size()) - 1;
    at.beat = 0;
    setCursor(at);
}

void Editor::deleteBar()
{
    endDigitEntry();
    clearSelection();
    auto *command = new DeleteBarCommand(this, m_cursor.bar);
    if (!command->isValid()) {
        delete command;
        return;
    }
    m_undo->push(command);
    // The bar the caret was in is gone; it now points at whatever moved up,
    // or at the last bar of the score if nothing did.
    setCursor(m_cursor);
}

int Editor::rhythmIdFor(Score &score, const Rational &duration)
{
    int highest = -1;
    for (auto rhythm = score.rhythms.constBegin(); rhythm != score.rhythms.constEnd();
         ++rhythm) {
        if (rhythm.value() == duration) {
            return rhythm.key();
        }
        highest = std::max(highest, rhythm.key());
    }
    score.rhythms.insert(highest + 1, duration);
    return highest + 1;
}

void Editor::applyDuration(const Rational &duration)
{
    endDigitEntry();
    clearSelection();
    if (duration.isZero() || Editing::beatIdAt(m_score, m_cursor) < 0) {
        return;
    }
    if (duration == Editing::durationAt(m_score, m_cursor)) {
        return;
    }
    m_undo->push(new SetDurationCommand(this, m_cursor, duration));
}

void Editor::setDuration(int denominator)
{
    applyDuration(NoteValue::valueOf(denominator));
}

void Editor::toggleDot()
{
    NoteValue::Written written = NoteValue::of(Editing::durationAt(m_score, m_cursor));
    // One dot or none. Two is a thing that exists and not a thing a keystroke
    // should cycle through on the way back to none.
    written.dots = written.dots > 0 ? 0 : 1;
    applyDuration(NoteValue::durationOf(written));
}

void Editor::scaleDuration(int steps)
{
    NoteValue::Written written = NoteValue::of(Editing::durationAt(m_score, m_cursor));
    for (int step = 0; step < qAbs(steps); ++step) {
        const Rational next = steps > 0 ? written.value * Rational(2)
                                        : Rational(written.value.numerator,
                                                   written.value.denominator * 2);
        if (next < NoteValue::Shortest || NoteValue::Longest < next) {
            break;
        }
        written.value = next;
    }
    applyDuration(NoteValue::durationOf(written));
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
