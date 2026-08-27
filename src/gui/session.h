// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#pragma once

#include "editor.h"
#include "player.h"
#include "score.h"
#include "tablayout.h"
#include "timeline.h"

#include <QObject>
#include <QQmlEngine>
#include <QStringList>
#include <QVariantList>
#include <QTimer>

#include <memory>

/**
 * One open score, and everything a window needs to ask about it.
 *
 * A facade rather than a model: the score, the layout, the played order and
 * the player already exist and are already tested, and this exposes them to
 * QML without any of them learning that QML is there. Nothing in `src/model`,
 * `src/playback` or `src/audio` includes a Qt Quick header, which is what
 * keeps them testable without a window.
 *
 * The playhead is **polled**, not pushed. `Player` deliberately emits nothing
 * from its audio thread, so a timer here reads the position a few times a
 * second and turns it into the signals QML wants. That is the correct
 * direction for this to flow: the audio thread must not know that a user
 * interface exists.
 */
class Session : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QString title READ title NOTIFY scoreChanged)
    Q_PROPERTY(QString artist READ artist NOTIFY scoreChanged)
    Q_PROPERTY(QString fileName READ fileName NOTIFY scoreChanged)
    Q_PROPERTY(bool hasScore READ hasScore NOTIFY scoreChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)

    /**
     * The tempo in force where the caret is, in crotchets a minute.
     *
     * What the bar would be played at, whether the bar writes a change of its
     * own or inherits one from an earlier bar. `tempoWrittenHere` is how a
     * window tells those two apart, which is worth showing: a number that
     * belongs to this bar and one it is merely living under look the same and
     * behave differently when they are edited.
     */
    Q_PROPERTY(double tempoHere READ tempoHere NOTIFY cursorMoved)
    Q_PROPERTY(bool tempoWrittenHere READ tempoWrittenHere NOTIFY cursorMoved)

    /**
     * The current track's instrument: how it is tuned and where its capo is.
     *
     * The tuning reads as names rather than numbers -- "E2 A2 D3 G3 B3 E4" --
     * because that is how a guitarist says one, and it is written back the
     * same way.
     */
    Q_PROPERTY(QString tuningHere READ tuningHere NOTIFY cursorMoved)
    Q_PROPERTY(int capoHere READ capoHere NOTIFY cursorMoved)

    /** How many strings the current track has; none for a drum kit. */
    Q_PROPERTY(int stringsHere READ stringsHere NOTIFY cursorMoved)

    /** What the current part is called and what it is, for the editor of both. */
    Q_PROPERTY(QString trackNameHere READ trackNameHere NOTIFY cursorMoved)
    Q_PROPERTY(QString instrumentHere READ instrumentHere NOTIFY cursorMoved)

    /** Everything a new part may be: names to show, and ids to ask for. */
    Q_PROPERTY(QStringList instrumentNames READ instrumentNames CONSTANT)
    Q_PROPERTY(QStringList instrumentIds READ instrumentIds CONSTANT)

    /** What the caret's bar is called, where the score calls it anything. */
    Q_PROPERTY(QString sectionHere READ sectionHere NOTIFY cursorMoved)

    /** The caret's bar's time signature, as "4/4", and whether it starts there. */
    Q_PROPERTY(QString timeHere READ timeHere NOTIFY cursorMoved)
    Q_PROPERTY(bool timeWrittenHere READ timeWrittenHere NOTIFY cursorMoved)

    /** Where the caret is, for the status bar: "Bar 4 · string 3 · quaver". */
    Q_PROPERTY(QString caretText READ caretText NOTIFY cursorMoved)

    Q_PROPERTY(QStringList trackNames READ trackNames NOTIFY scoreChanged)

    /** One icon name per track, by what the instrument is. */
    Q_PROPERTY(QStringList trackIcons READ trackIcons NOTIFY scoreChanged)
    Q_PROPERTY(int trackCount READ trackCount NOTIFY scoreChanged)
    Q_PROPERTY(int currentTrack READ currentTrack WRITE setCurrentTrack NOTIFY currentTrackChanged)

    /**
     * A metronome on every beat, which is not a track and not in the file.
     *
     * Kept on across a rebuild of the player: somebody practising a bar has
     * asked for a click, not for a click until the next time they edit a note.
     */
    Q_PROPERTY(bool click READ isClickOn WRITE setClickOn NOTIFY clickChanged)
    Q_PROPERTY(double clickGain READ clickGain WRITE setClickGain NOTIFY clickChanged)

    Q_PROPERTY(bool playing READ isPlaying NOTIFY playingChanged)
    Q_PROPERTY(bool canPlay READ canPlay NOTIFY scoreChanged)
    Q_PROPERTY(double position READ position NOTIFY positionChanged)
    Q_PROPERTY(double length READ length NOTIFY scoreChanged)
    Q_PROPERTY(int currentBar READ currentBar NOTIFY positionChanged)

    /** How many bars the score has, and which one the caret is in. */
    Q_PROPERTY(int barCount READ barCount NOTIFY scoreChanged)

    /**
     * Whether the score names any of its sections at all.
     *
     * Asked once so that the bar strip can decide whether to keep a line free
     * for section names in every cell of its grid. Most scores name a few and
     * some name none, and reserving the room unconditionally would cost a row
     * of pixels per row of bars to hold nothing.
     */
    Q_PROPERTY(bool hasSections READ hasSections NOTIFY scoreChanged)
    Q_PROPERTY(int caretBar READ caretBar NOTIFY cursorMoved)

    Q_PROPERTY(bool canUndo READ canUndo NOTIFY historyChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY historyChanged)
    Q_PROPERTY(QString undoText READ undoText NOTIFY historyChanged)
    Q_PROPERTY(QString redoText READ redoText NOTIFY historyChanged)
    Q_PROPERTY(bool modified READ isModified NOTIFY historyChanged)
    Q_PROPERTY(QString filePath READ filePath NOTIFY scoreChanged)
    Q_PROPERTY(bool savesInPlace READ savesInPlace NOTIFY scoreChanged)

public:
    explicit Session(QObject *parent = nullptr);
    ~Session() override;

    Q_INVOKABLE bool open(const QString &path);

    /** Starts again with one guitar and one empty bar. */
    Q_INVOKABLE void newScore();

    // ---- parts ----

    Q_INVOKABLE void addTrack(const QString &instrumentId);
    Q_INVOKABLE void removeTrack(int track);
    Q_INVOKABLE void renameTrack(int track, const QString &name);
    Q_INVOKABLE void setTrackInstrument(int track, const QString &instrumentId);

    /** Moves a part up or down the list by `by` places, bars and all. */
    Q_INVOKABLE void moveTrack(int track, int by);

    QStringList instrumentNames() const;
    QStringList instrumentIds() const;
    QString instrumentHere() const;
    QString trackNameHere() const;

    /**
     * Writes to the file it was opened from, if that is one of ours.
     *
     * Returns false where there is nowhere to write to yet -- a score imported
     * from a Guitar Pro file has no Fretwork file behind it, and saving over
     * the original is not something to do quietly. The window turns that false
     * into a Save As.
     */
    Q_INVOKABLE bool save();
    Q_INVOKABLE bool saveAs(const QString &path);

    QString filePath() const;
    bool savesInPlace() const;

    QString title() const;
    QString artist() const;
    QString fileName() const;
    bool hasScore() const;
    QString status() const;
    QString caretText() const;

    QStringList trackNames() const;
    QStringList trackIcons() const;
    int trackCount() const;
    int barCount() const;
    bool hasSections() const;
    int caretBar() const;
    int currentTrack() const;
    void setCurrentTrack(int track);

    bool isClickOn() const;
    void setClickOn(bool on);
    double clickGain() const;
    void setClickGain(double gain);

    bool isPlaying() const;
    bool canPlay() const;
    double position() const;
    double length() const;
    int currentBar() const;

    // ---- transport ----

    Q_INVOKABLE void play();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void seek(double seconds);

    /**
     * Puts the caret at the start of a bar and the playhead with it.
     *
     * The first time through, where the bar is inside a repeat: a click on bar
     * 12 means the twelfth bar of the score, and which pass through it was
     * meant is not a question a bar number can answer.
     */
    Q_INVOKABLE void goToBar(int bar);

    /** The name the score gives a bar, where it names one: "Intro", "Chorus". */
    Q_INVOKABLE QString sectionAt(int bar) const;

    // ---- the mixer ----

    /**
     * The open pitches of a track's strings, with any capo already in them.
     *
     * For the tuner, which wants what the string will sound when it is
     * plucked. Empty for a drum kit, which has none, and for a track the
     * importer found no tuning for.
     */
    Q_INVOKABLE QVariantList stringPitches(int track) const;

    Q_INVOKABLE bool isMuted(int track) const;
    Q_INVOKABLE bool isSolo(int track) const;
    Q_INVOKABLE bool isAudible(int track) const;
    Q_INVOKABLE double gain(int track) const;
    Q_INVOKABLE void setMuted(int track, bool muted);
    Q_INVOKABLE void setSolo(int track, bool solo);
    Q_INVOKABLE void setGain(int track, double gain);

    // ---- editing ----

    Q_INVOKABLE void moveCursor(const QString &direction, bool extend = false);
    Q_INVOKABLE void typeDigit(int digit);
    Q_INVOKABLE void clearNote();

    double tempoHere() const;
    bool tempoWrittenHere() const;

    /** Sets the tempo from the caret's bar on. Says so where it will not. */
    Q_INVOKABLE void setTempoHere(double quarterBpm);

    /** Takes the caret's bar's own tempo change away, where it has one. */
    Q_INVOKABLE void clearTempoHere();

    QString tuningHere() const;
    int capoHere() const;
    int stringsHere() const;

    /** Retunes the current track. Frets stay where they are; pitches move. */
    Q_INVOKABLE void setTuningHere(const QString &names);
    Q_INVOKABLE void setCapoHere(int fret);

    QString sectionHere() const;

    /** Names the caret's bar, or takes its name off when given nothing. */
    Q_INVOKABLE void setSectionHere(const QString &name);

    QString timeHere() const;
    bool timeWrittenHere() const;

    /**
     * Sets the time signature from the caret's bar until the next change.
     *
     * Takes it written down -- "6/8" -- because that is how a musician says
     * one and how the field they type it into reads.
     */
    Q_INVOKABLE void setTimeHere(const QString &signature);

    /** Moves the note under the caret along its string, or the whole selection. */
    Q_INVOKABLE void transpose(int frets);

    /** Moves the note under the caret to the next string, keeping its pitch. */
    Q_INVOKABLE void moveNoteAcross(int strings);

    /**
     * Marks the note under the caret, or the whole selection: "dead", "ghost",
     * "palmMute" or "letRing".
     */
    Q_INVOKABLE void toggleMark(const QString &mark);

    /** 4 is a crotchet, 8 a quaver: the name musicians already use. */
    Q_INVOKABLE void setDuration(int denominator);
    Q_INVOKABLE void toggleDot();
    Q_INVOKABLE void scaleDuration(int steps);

    Q_INVOKABLE void insertBeat();
    Q_INVOKABLE void deleteBeat();

    /** An empty bar at the caret, across every track. */
    Q_INVOKABLE void insertBar();

    /** An empty bar on the end of the score, and the caret in it. */
    Q_INVOKABLE void appendBar();

    /** Takes the bar under the caret out, unless it is the only one. */
    Q_INVOKABLE void deleteBar();
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    /** Puts the caret where the score was clicked, in the view's own coordinates. */
    Q_INVOKABLE void placeCursorAt(qreal x, qreal y, bool extend = false);

    // ---- selection and the clipboard ----

    Q_INVOKABLE void copy();
    Q_INVOKABLE void cut();
    Q_INVOKABLE void paste();

    bool hasSelection() const;
    Editing::Range selection() const;

    bool canUndo() const;
    bool canRedo() const;
    QString undoText() const;
    QString redoText() const;
    bool isModified() const;

    /** Where the caret is, for whatever draws it. */
    Cursor cursor() const;

    /** Seconds to hours:minutes:seconds, for a label. */
    Q_INVOKABLE QString clock(double seconds) const;

    /** The laid-out tablature, for whatever draws it. */
    const Tab::Layout &layout() const;

    /** Lays the current track out again at a new width, in points. */
    void relayout(qreal width);

Q_SIGNALS:
    void scoreChanged();
    void statusChanged();
    void currentTrackChanged();
    void playingChanged();
    void clickChanged();
    void positionChanged();
    void layoutChanged();
    void mixerChanged();
    void historyChanged();
    void cursorMoved();

private:
    void rebuildLayout();
    void rebuildPlayer();
    void setStatus(const QString &status);

    Editor m_editor;
    QList<int> m_order;
    bool m_playerStale = false;
    std::unique_ptr<Timeline::Clock> m_clock;
    std::unique_ptr<Player> m_player;

    Tab::Layout m_layout;
    qreal m_width = 900;
    int m_currentTrack = 0;
    bool m_click = false;
    double m_clickGain = 1.0;
    int m_currentBar = -1;
    bool m_wasPlaying = false;

    QString m_fileName;
    QString m_filePath;
    QString m_status;
    QTimer m_ticker;
};
