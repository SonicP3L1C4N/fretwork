// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "session.h"

#include "fwformat.h"
#include "gpif.h"

#include <KLocalizedString>

#include <QFileInfo>
#include <QHash>
#include <QUrl>

#include <algorithm>
#include <cmath>

Session::Session(QObject *parent)
    : QObject(parent)
{
    // Twenty times a second: enough that a playhead looks continuous, few
    // enough that it costs nothing. The audio thread is not involved.
    // An edit changes the page immediately and the sound the next time it is
    // asked for: relaying out is microseconds, and rebuilding the player means
    // loading a SoundFont for every track, which is not something to do
    // between two keystrokes.
    connect(&m_editor, &Editor::scoreEdited, this, [this](int) {
        m_playerStale = true;
        rebuildLayout();
        Q_EMIT historyChanged();
    });
    connect(&m_editor, &Editor::cursorChanged, this, &Session::cursorMoved);
    connect(&m_editor, &Editor::historyChanged, this, &Session::historyChanged);

    m_ticker.setInterval(50);
    connect(&m_ticker, &QTimer::timeout, this, [this] {
        if (!m_player) {
            return;
        }
        Q_EMIT positionChanged();

        const bool playing = m_player->isPlaying();
        if (playing != m_wasPlaying) {
            m_wasPlaying = playing;
            Q_EMIT playingChanged();
        }
    });
}

Session::~Session() = default;

bool Session::open(const QString &path)
{
    const QString local = QUrl(path).isLocalFile() ? QUrl(path).toLocalFile() : path;

    QString why;
    // Ours or Guitar Pro's, decided by the name: the two are both ZIPs and
    // telling them apart by content would only matter if someone renamed one.
    const Score score = Fw::looksLikeOurs(local) ? Fw::read(local, &why)
                                                 : Gpif::read(local, &why);
    if (score.isEmpty()) {
        setStatus(i18n("%1: %2", QFileInfo(local).fileName(),
                       why.isEmpty() ? i18n("could not be read") : why));
        return false;
    }

    // The player goes before the score it is playing.
    m_player.reset();

    m_editor.setScore(score);
    m_order = Timeline::playedOrder(m_editor.score());
    m_clock = std::make_unique<Timeline::Clock>(m_editor.score(), m_order);
    m_fileName = QFileInfo(local).fileName();
    m_filePath = local;
    m_currentTrack = 0;
    m_currentBar = -1;
    m_wasPlaying = false;

    rebuildLayout();
    rebuildPlayer();

    Q_EMIT scoreChanged();
    Q_EMIT currentTrackChanged();
    Q_EMIT mixerChanged();
    setStatus(Timeline::hasAlternateEndings(m_editor.score())
                  ? i18n("Alternate endings are not flattened yet, so the playback "
                         "order is approximate")
                  : QString());
    return true;
}

void Session::rebuildLayout()
{
    if (m_editor.score().isEmpty()) {
        m_layout = Tab::Layout();
        Q_EMIT layoutChanged();
        return;
    }

    Tab::Style style;
    style.pageWidth = std::max<qreal>(360, m_width);
    // One page as tall as the music needs: a window scrolls, and breaking a
    // continuous view into A4 sheets would be paper thinking.
    style.pageHeight = 1e9;
    // The window's own title bar carries the name of the piece, so the page
    // does not: on screen that would cost a system of music to say it twice.
    style.showTitle = false;
    style.titleHeight = style.labelSize * 4.2;

    m_layout = Tab::layOut(m_editor.score(), m_currentTrack, style);
    Q_EMIT layoutChanged();
}

void Session::rebuildPlayer()
{
    m_player.reset();
    if (m_editor.score().isEmpty()) {
        return;
    }

    Player::Options options;
    auto player = std::make_unique<Player>(m_editor.score(), m_order, options);
    if (!player->isValid()) {
        setStatus(player->error());
        return;
    }
    m_player = std::move(player);
    m_ticker.start();
}

void Session::setStatus(const QString &status)
{
    if (m_status != status) {
        m_status = status;
        Q_EMIT statusChanged();
    }
}

QString Session::title() const
{
    return m_editor.score().title.isEmpty() ? m_fileName : m_editor.score().title;
}

QString Session::artist() const
{
    return m_editor.score().artist;
}

QString Session::fileName() const
{
    return m_fileName;
}

bool Session::hasScore() const
{
    return !m_editor.score().isEmpty();
}

QString Session::status() const
{
    return m_status;
}

QStringList Session::trackNames() const
{
    QStringList names;
    names.reserve(int(m_editor.score().tracks.size()));
    for (const Track &track : m_editor.score().tracks) {
        names.append(track.name);
    }
    return names;
}

int Session::trackCount() const
{
    return int(m_editor.score().tracks.size());
}

int Session::currentTrack() const
{
    return m_currentTrack;
}

void Session::setCurrentTrack(int track)
{
    if (track < 0 || track >= trackCount() || track == m_currentTrack) {
        return;
    }
    m_currentTrack = track;
    rebuildLayout();
    Q_EMIT currentTrackChanged();
}

bool Session::isPlaying() const
{
    return m_player && m_player->isPlaying();
}

bool Session::canPlay() const
{
    return m_player && m_player->isValid();
}

double Session::position() const
{
    return m_player ? m_player->positionSeconds() : 0;
}

double Session::length() const
{
    return m_player ? m_player->lengthSeconds() : 0;
}

int Session::currentBar() const
{
    if (!m_player || !m_clock) {
        return -1;
    }
    const int pass = Timeline::barAt(m_editor.score(), m_order, *m_clock, m_player->positionSeconds());
    // The bar as the score writes it, which is what the reader is looking at:
    // the fourth time through a repeat still lights up the bar on the page.
    return pass < 0 ? -1 : m_order.at(pass);
}

void Session::play()
{
    if (m_playerStale) {
        // The edited score, heard: this is where the cost of rebuilding is
        // paid, once, rather than on every keystroke.
        rebuildPlayer();
        m_playerStale = false;
        // Changing a duration changes how long the piece is, and the transport
        // has been showing the length of the score as it was opened.
        Q_EMIT scoreChanged();
    }
    if (m_player) {
        m_player->play();
        Q_EMIT playingChanged();
    }
}

void Session::pause()
{
    if (m_player) {
        m_player->pause();
        Q_EMIT playingChanged();
    }
}

void Session::stop()
{
    if (m_player) {
        m_player->stop();
        Q_EMIT playingChanged();
        Q_EMIT positionChanged();
    }
}

void Session::seek(double seconds)
{
    if (m_player) {
        m_player->seekSeconds(seconds);
        Q_EMIT positionChanged();
    }
}

bool Session::isMuted(int track) const
{
    return m_player && m_player->isMuted(track);
}

bool Session::isSolo(int track) const
{
    return m_player && m_player->isSolo(track);
}

bool Session::isAudible(int track) const
{
    return m_player && m_player->isAudible(track);
}

double Session::gain(int track) const
{
    return m_player ? m_player->gain(track) : 1.0;
}

void Session::setMuted(int track, bool muted)
{
    if (m_player) {
        m_player->setMuted(track, muted);
        Q_EMIT mixerChanged();
    }
}

void Session::setSolo(int track, bool solo)
{
    if (m_player) {
        m_player->setSolo(track, solo);
        Q_EMIT mixerChanged();
    }
}

void Session::setGain(int track, double gain)
{
    if (m_player) {
        m_player->setGain(track, float(gain));
        Q_EMIT mixerChanged();
    }
}

QString Session::clock(double seconds) const
{
    const int whole = int(std::max(0.0, seconds) + 0.5);
    return QStringLiteral("%1:%2").arg(whole / 60).arg(whole % 60, 2, 10, QLatin1Char('0'));
}

const Tab::Layout &Session::layout() const
{
    return m_layout;
}

void Session::relayout(qreal width)
{
    if (qAbs(width - m_width) < 1.0) {
        return;
    }
    m_width = width;
    rebuildLayout();
}


// ---- editing ----

void Session::moveCursor(const QString &direction, bool extend)
{
    static const QHash<QString, Editing::Move> moves = {
        {QStringLiteral("left"), Editing::Move::Left},
        {QStringLiteral("right"), Editing::Move::Right},
        {QStringLiteral("up"), Editing::Move::Up},
        {QStringLiteral("down"), Editing::Move::Down},
        {QStringLiteral("barBack"), Editing::Move::BarBack},
        {QStringLiteral("barForward"), Editing::Move::BarForward},
        {QStringLiteral("start"), Editing::Move::Start},
        {QStringLiteral("end"), Editing::Move::End},
    };
    if (moves.contains(direction)) {
        m_editor.move(moves.value(direction), extend);
    }
}

void Session::typeDigit(int digit)
{
    m_editor.typeDigit(digit);
}

void Session::clearNote()
{
    // Delete takes the selection where there is one, which is what it does
    // everywhere else a person has ever pressed it.
    if (m_editor.hasSelection()) {
        m_editor.deleteSelection();
    } else {
        m_editor.clearNote();
    }
}

void Session::transposeNote(int frets)
{
    m_editor.transposeNote(frets);
}

void Session::setDuration(int denominator)
{
    m_editor.setDuration(denominator);
}

void Session::toggleDot()
{
    m_editor.toggleDot();
}

void Session::scaleDuration(int steps)
{
    m_editor.scaleDuration(steps);
}

void Session::insertBeat()
{
    m_editor.insertBeat();
}

void Session::deleteBeat()
{
    m_editor.deleteBeat();
}

void Session::insertBar()
{
    m_editor.insertBar();
}

void Session::appendBar()
{
    m_editor.appendBar();
}

void Session::deleteBar()
{
    if (!hasScore()) {
        return;
    }
    if (!m_editor.canDeleteBar()) {
        // The only bar there is. Say so, because a key that does nothing and
        // does not explain itself reads as a broken key.
        setStatus(i18n("A score has to have a bar in it, so the last one is kept"));
        return;
    }
    m_editor.deleteBar();
}

void Session::undo()
{
    m_editor.undo();
}

void Session::redo()
{
    m_editor.redo();
}

void Session::placeCursorAt(qreal x, qreal y, bool extend)
{
    Cursor found;
    if (!Tab::hitTest(m_layout, x, y, &found.bar, &found.voice, &found.beat, &found.string)) {
        return;
    }
    found.track = m_currentTrack;
    m_editor.setCursor(found, extend);
}

void Session::copy()
{
    m_editor.copy();
}

void Session::cut()
{
    m_editor.cut();
}

void Session::paste()
{
    if (!m_editor.paste() && m_editor.canPaste()) {
        // Refused rather than half done: say which, because "nothing
        // happened" is the least useful thing a program can do.
        setStatus(i18n("There are not enough bars left to paste %1 into",
                       i18np("one bar", "%1 bars", int(m_editor.clip().bars.size()))));
    }
}

bool Session::hasSelection() const
{
    return m_editor.hasSelection();
}

Editing::Range Session::selection() const
{
    return m_editor.selection();
}

bool Session::canUndo() const
{
    return m_editor.canUndo();
}

bool Session::canRedo() const
{
    return m_editor.canRedo();
}

QString Session::undoText() const
{
    return m_editor.undoText();
}

QString Session::redoText() const
{
    return m_editor.redoText();
}

bool Session::isModified() const
{
    return m_editor.isModified();
}

Cursor Session::cursor() const
{
    return m_editor.cursor();
}


bool Session::save()
{
    // Only over one of ours. An imported .gp stays as its author wrote it:
    // Fretwork does not write that format, and would not overwrite it if it
    // could.
    if (m_filePath.isEmpty() || !Fw::looksLikeOurs(m_filePath)) {
        return false;
    }
    return saveAs(m_filePath);
}

bool Session::saveAs(const QString &path)
{
    const QString local = QUrl(path).isLocalFile() ? QUrl(path).toLocalFile() : path;
    if (local.isEmpty()) {
        return false;
    }

    QString target = local;
    if (!Fw::looksLikeOurs(target)) {
        target += QLatin1Char('.') + Fw::extension();
    }

    QString why;
    if (!Fw::write(m_editor.score(), target, &why)) {
        setStatus(i18n("Could not save: %1", why));
        return false;
    }

    m_filePath = target;
    m_fileName = QFileInfo(target).fileName();
    m_editor.setUnmodified();
    setStatus(i18n("Saved to %1", m_fileName));
    Q_EMIT scoreChanged();
    Q_EMIT historyChanged();
    return true;
}

QString Session::filePath() const
{
    return m_filePath;
}

bool Session::savesInPlace() const
{
    return !m_filePath.isEmpty() && Fw::looksLikeOurs(m_filePath);
}
