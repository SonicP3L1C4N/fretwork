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

    Q_PROPERTY(QStringList trackNames READ trackNames NOTIFY scoreChanged)
    Q_PROPERTY(int trackCount READ trackCount NOTIFY scoreChanged)
    Q_PROPERTY(int currentTrack READ currentTrack WRITE setCurrentTrack NOTIFY currentTrackChanged)

    Q_PROPERTY(bool playing READ isPlaying NOTIFY playingChanged)
    Q_PROPERTY(bool canPlay READ canPlay NOTIFY scoreChanged)
    Q_PROPERTY(double position READ position NOTIFY positionChanged)
    Q_PROPERTY(double length READ length NOTIFY scoreChanged)
    Q_PROPERTY(int currentBar READ currentBar NOTIFY positionChanged)

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

    QStringList trackNames() const;
    int trackCount() const;
    int currentTrack() const;
    void setCurrentTrack(int track);

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

    // ---- the mixer ----

    Q_INVOKABLE bool isMuted(int track) const;
    Q_INVOKABLE bool isSolo(int track) const;
    Q_INVOKABLE bool isAudible(int track) const;
    Q_INVOKABLE double gain(int track) const;
    Q_INVOKABLE void setMuted(int track, bool muted);
    Q_INVOKABLE void setSolo(int track, bool solo);
    Q_INVOKABLE void setGain(int track, double gain);

    // ---- editing ----

    Q_INVOKABLE void moveCursor(const QString &direction);
    Q_INVOKABLE void typeDigit(int digit);
    Q_INVOKABLE void clearNote();
    Q_INVOKABLE void transposeNote(int frets);

    /** 4 is a crotchet, 8 a quaver: the name musicians already use. */
    Q_INVOKABLE void setDuration(int denominator);
    Q_INVOKABLE void toggleDot();
    Q_INVOKABLE void scaleDuration(int steps);
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    /** Puts the caret where the score was clicked, in the view's own coordinates. */
    Q_INVOKABLE void placeCursorAt(qreal x, qreal y);

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
    int m_currentBar = -1;
    bool m_wasPlaying = false;

    QString m_fileName;
    QString m_filePath;
    QString m_status;
    QTimer m_ticker;
};
