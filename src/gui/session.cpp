// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "session.h"

#include "fwformat.h"
#include "gpif.h"
#include "instruments.h"
#include "notename.h"
#include "notevalue.h"

#include <KLocalizedString>

#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QStandardPaths>
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
    connect(&m_editor, &Editor::scoreEdited, this, [this](int bar) {
        m_playerStale = true;

        // A bar of -1 means the shape of the score changed rather than
        // something inside one bar of it: a part added, removed or moved. The
        // track list, the mixer and everything else counting parts is reading
        // a number that has just changed, and none of them are listening for
        // an edit.
        if (bar < 0) {
            m_currentTrack = std::clamp(m_currentTrack, 0, std::max(0, trackCount() - 1));
        }

        rebuildLayout();
        Q_EMIT historyChanged();
        if (bar < 0) {
            Q_EMIT scoreChanged();
            Q_EMIT currentTrackChanged();
            Q_EMIT mixerChanged();
        }
        // What the caret is sitting on may have changed under it, and the
        // status bar is reading that out.
        Q_EMIT cursorMoved();
    });
    connect(&m_editor, &Editor::cursorChanged, this, &Session::cursorMoved);
    connect(&m_editor, &Editor::historyChanged, this, &Session::historyChanged);

    rescanLibraries();

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

namespace
{
/**
 * Which drawing stands for an instrument.
 *
 * By what it is rather than by its General MIDI programme: the programme of a
 * drum kit is an acoustic piano, and a track called "Guitar II" that somebody
 * has pointed at a string patch is still a guitar to the person reading the
 * tab. Anything unrecognised gets a note, which says "a part" and does not
 * claim to know which.
 */
QString iconFor(const Track &track)
{
    if (track.isPercussion()) {
        return QStringLiteral("qrc:/instrument-drums.svg");
    }
    const QString kind = track.instrumentType.toLower();
    if (kind.contains(QLatin1String("bass"))) {
        return QStringLiteral("qrc:/instrument-bass.svg");
    }
    if (kind.contains(QLatin1String("guitar")) || kind.contains(QLatin1String("ukulele"))
        || kind.contains(QLatin1String("banjo")) || kind.contains(QLatin1String("mandolin"))) {
        return QStringLiteral("qrc:/instrument-guitar.svg");
    }
    if (kind.contains(QLatin1String("piano")) || kind.contains(QLatin1String("synth"))
        || kind.contains(QLatin1String("organ")) || kind.contains(QLatin1String("keyboard"))) {
        return QStringLiteral("qrc:/instrument-keys.svg");
    }
    return QStringLiteral("qrc:/instrument-note.svg");
}
}

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
    // What the window should say the moment a score opens: what is true of the
    // whole piece and is not visible anywhere on the page. A triplet feel used
    // to be said here and is not any more -- the page prints it now, over the
    // bar it starts on, and a status bar repeating it would be the second of
    // two places to read the same thing.
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
    // The room for it goes with it; the labels above the first line keep
    // theirs.
    style.showTitle = false;

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
    options.samplers = m_samplers;
    options.perTrackPorts = m_ports || m_following;
    options.followTransport = m_following;
    auto player = std::make_unique<Player>(m_editor.score(), m_order, options);
    if (!player->isValid()) {
        setStatus(player->error());
        return;
    }
    m_player = std::move(player);
    // Whatever the mixer said before the player was rebuilt, it still says: a
    // new Player is an implementation detail of editing a note, and nothing a
    // person did to the click should be undone by one.
    m_player->setClickEnabled(m_click);
    m_player->setClickGain(float(m_clickGain));
    m_ticker.start();
}

QString Session::caretText() const
{
    if (!hasScore()) {
        return QString();
    }
    const Cursor cursor = m_editor.cursor();
    const int strings = Editing::stringCount(m_editor.score(), cursor);
    // Strings are counted from the thin one down, the way a guitarist names
    // them: the top string is the first, whatever the model calls it.
    const int named = strings - cursor.string;

    if (Editing::beatIdAt(m_editor.score(), cursor) < 0) {
        // One past the end of a bar is a real place to be, and it has no note
        // value to report because there is nothing there yet.
        return i18nc("where the caret is", "Bar %1 · string %2 · end of the bar",
                     cursor.bar + 1, named);
    }
    return i18nc("where the caret is", "Bar %1 · string %2 · %3", cursor.bar + 1, named,
                 NoteValue::nameOf(Editing::durationAt(m_editor.score(), cursor)));
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

QVariantList Session::stringPitches(int track) const
{
    QVariantList pitches;
    if (track < 0 || track >= m_editor.score().tracks.size()) {
        return pitches;
    }
    const Track &part = m_editor.score().tracks.at(track);
    if (part.isPercussion()) {
        return pitches;
    }
    for (const int pitch : part.tuning) {
        pitches.append(pitch + part.capo);
    }
    return pitches;
}

bool Session::hasSections() const
{
    for (const MasterBar &bar : m_editor.score().masterBars) {
        if (!bar.section.isEmpty()) {
            return true;
        }
    }
    return false;
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

    // The caret goes with it. Until now it did not, and the caret is what
    // every edit is aimed at -- so choosing a part in the list showed one
    // track and typed into another, until somebody happened to click on the
    // page and bring the two back together.
    Cursor moved = m_editor.cursor();
    moved.track = track;
    m_editor.setCursor(moved);

    rebuildLayout();
    Q_EMIT currentTrackChanged();
}

QString Session::samplerHere() const
{
    const QString path = m_samplers.value(m_currentTrack);
    if (path.isEmpty()) {
        return QString();
    }
    for (const Sfz::Library &library : m_libraries) {
        if (library.path == path) {
            return library.name;
        }
    }
    // Chosen from a file rather than from the list: the file's own name is
    // the best anybody can do, and it is what they picked.
    return QFileInfo(path).completeBaseName();
}

QVariantList Session::libraries() const
{
    QVariantList found;
    for (const Sfz::Library &library : m_libraries) {
        found.append(QVariantMap{{QStringLiteral("collection"), library.collection},
                                 {QStringLiteral("name"), library.name},
                                 {QStringLiteral("path"), library.path}});
    }
    return found;
}

QStringList Session::collections() const
{
    QStringList boxes;
    for (const Sfz::Library &library : m_libraries) {
        if (!boxes.contains(library.collection)) {
            boxes.append(library.collection);
        }
    }
    return boxes;
}

void Session::rescanLibraries()
{
    // Where a person's libraries actually are: the place this program would
    // put one, and the standard share directories, which is where a package
    // would put one.
    QStringList roots =
        QStandardPaths::standardLocations(QStandardPaths::AppDataLocation);
    for (QString &root : roots) {
        root += QStringLiteral("/instruments");
    }
    m_libraries = Sfz::found(roots);
    Q_EMIT samplersChanged();
}

void Session::setSamplerHere(const QString &path)
{
    const QString local = QUrl(path).isLocalFile() ? QUrl(path).toLocalFile() : path;
    if (m_samplers.value(m_currentTrack) == local) {
        return;
    }

    const QHash<int, QString> was = m_samplers;
    if (local.isEmpty()) {
        m_samplers.remove(m_currentTrack);
    } else {
        m_samplers.insert(m_currentTrack, local);
    }

    stop();
    rebuildPlayer();
    if (!canPlay() && !local.isEmpty()) {
        // It would not load. Back to the programme rather than leaving
        // somebody with a part that cannot be played at all; the status bar
        // already carries whatever the player said was wrong with it.
        m_samplers = was;
        rebuildPlayer();
    } else {
        setStatus(local.isEmpty()
                      ? i18n("%1 is back on a General MIDI programme", trackNameHere())
                      : i18n("%1 is playing from %2", trackNameHere(), samplerHere()));
    }
    Q_EMIT samplersChanged();
}

bool Session::isFollowing() const
{
    return m_following;
}

void Session::setFollowing(bool on)
{
    if (m_following == on) {
        return;
    }
    stop();
    m_following = on;
    // Following needs the ports, so asking for one asks for the other. Turning
    // it off leaves them open: somebody who wanted ports and then stopped
    // following did not ask to have the ports taken away.
    if (on) {
        m_ports = true;
    }
    rebuildPlayer();
    setStatus(on ? i18n("Following the graph — it rolls when the graph does")
                 : QString());
    Q_EMIT portsChanged();
    Q_EMIT playingChanged();
}

bool Session::isPortsOn() const
{
    return m_ports;
}

int Session::portCount() const
{
    return m_player ? m_player->portCount() : 0;
}

void Session::setPortsOn(bool on)
{
    if (m_ports == on) {
        return;
    }
    // Stopped first: the output is being replaced, not adjusted, and a piece
    // that carried on through that would be carrying on through a gap.
    stop();
    m_ports = on;
    rebuildPlayer();

    if (on && portCount() == 0) {
        // It did not open. Back to the ordinary way out rather than leaving
        // somebody with a transport that will not play.
        m_ports = false;
        rebuildPlayer();
        setStatus(i18n("The ports could not be opened, so the sound is going out the "
                       "usual way"));
    } else {
        setStatus(on ? i18n("%1 pairs of ports in the graph — link them and record",
                            QString::number(portCount()))
                     : QString());
    }
    Q_EMIT portsChanged();
}

bool Session::isClickOn() const
{
    return m_click;
}

void Session::setClickOn(bool on)
{
    if (m_click == on) {
        return;
    }
    m_click = on;
    if (m_player) {
        m_player->setClickEnabled(on);
    }
    Q_EMIT clickChanged();
}

double Session::clickGain() const
{
    return m_clickGain;
}

void Session::setClickGain(double gain)
{
    if (qFuzzyCompare(m_clickGain, gain)) {
        return;
    }
    m_clickGain = gain;
    if (m_player) {
        m_player->setClickGain(float(gain));
    }
    Q_EMIT clickChanged();
}

bool Session::isPlaying() const
{
    return m_player && m_player->isPlaying();
}

bool Session::canPlay() const
{
    // Not while following: the transport belongs to the graph then, and a play
    // button that did nothing would be worse than one that says it cannot.
    return m_player && m_player->isValid() && !m_following;
}

double Session::position() const
{
    return m_player ? m_player->positionSeconds() : 0;
}

double Session::length() const
{
    return m_player ? m_player->lengthSeconds() : 0;
}

QStringList Session::trackIcons() const
{
    QStringList icons;
    for (const Track &track : m_editor.score().tracks) {
        icons.append(iconFor(track));
    }
    return icons;
}

int Session::barCount() const
{
    return int(m_editor.score().masterBars.size());
}

int Session::caretBar() const
{
    return hasScore() ? m_editor.cursor().bar : -1;
}

QString Session::sectionAt(int bar) const
{
    const Score &score = m_editor.score();
    if (bar < 0 || bar >= score.masterBars.size()) {
        return QString();
    }
    return score.masterBars.at(bar).section;
}

void Session::goToBar(int bar)
{
    if (!hasScore()) {
        return;
    }
    const int wanted = std::clamp(bar, 0, int(m_editor.score().masterBars.size()) - 1);

    Cursor at = m_editor.cursor();
    at.bar = wanted;
    at.beat = 0;
    m_editor.setCursor(at);

    // And the playhead with it, playing or not: stopped, this is where the
    // next press of play starts from, which is what somebody clicking a bar
    // number almost always wants next.
    if (m_player && m_clock) {
        const int pass = int(m_order.indexOf(wanted));
        if (pass >= 0) {
            seek(Timeline::secondsAtPass(m_editor.score(), m_order, *m_clock, pass));
        }
    }
}

int Session::currentBar() const
{
    if (!m_player || !m_clock) {
        return -1;
    }
    // Stopped at the beginning is not "playing bar 1". There is no playhead
    // yet, and lighting a bar up would have the page say the program is doing
    // something it is not.
    if (!m_player->isPlaying() && m_player->positionSeconds() <= 0.0) {
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

double Session::tempoHere() const
{
    return hasScore() ? Timeline::tempoAtBar(m_editor.score(), m_editor.cursor().bar) : 120;
}

bool Session::tempoWrittenHere() const
{
    return hasScore() && m_editor.hasTempoHere();
}

void Session::setTempoHere(double quarterBpm)
{
    switch (m_editor.setTempo(quarterBpm)) {
    case Editor::Edit::Done:
        setStatus(i18n("Tempo %1 from bar %2",
                       QString::number(quarterBpm, 'g', 4),
                       QString::number(m_editor.cursor().bar + 1)));
        break;
    case Editor::Edit::Refused:
        // A number outside the range is a slipped digit rather than a tempo,
        // and saying so is the difference between a field that refused and a
        // field that appears not to work.
        setStatus(i18n("A tempo has to be between 20 and 400"));
        break;
    case Editor::Edit::Nothing:
        break;
    }
}

void Session::clearTempoHere()
{
    if (m_editor.clearTempo() == Editor::Edit::Refused) {
        setStatus(i18n("The first bar keeps its tempo: there is nothing before it to "
                       "take one from"));
    }
}

QString Session::sectionHere() const
{
    return hasScore() ? m_editor.score().masterBars.at(m_editor.cursor().bar).section
                      : QString();
}

void Session::setSectionHere(const QString &name)
{
    if (m_editor.setSection(name) != Editor::Edit::Done) {
        return;
    }
    setStatus(name.trimmed().isEmpty()
                  ? i18n("Bar %1 is not the start of a section any more",
                         QString::number(m_editor.cursor().bar + 1))
                  : i18n("Bar %1 is \"%2\"", QString::number(m_editor.cursor().bar + 1),
                         name.trimmed()));
}

QString Session::timeHere() const
{
    if (!hasScore()) {
        return QString();
    }
    const MasterBar &bar = m_editor.score().masterBars.at(m_editor.cursor().bar);
    return QStringLiteral("%1/%2").arg(bar.numerator).arg(bar.denominator);
}

bool Session::timeWrittenHere() const
{
    return hasScore() && m_editor.timeSignatureWrittenHere();
}

void Session::setTimeHere(const QString &signature)
{
    const QStringList parts = signature.split(QLatin1Char('/'));
    if (parts.size() != 2) {
        setStatus(i18n("A time signature is two numbers with a slash between them"));
        return;
    }
    switch (m_editor.setTimeSignature(parts.at(0).toInt(), parts.at(1).toInt())) {
    case Editor::Edit::Done:
        setStatus(i18n("%1 from bar %2", timeHere(),
                       QString::number(m_editor.cursor().bar + 1)));
        break;
    case Editor::Edit::Refused:
        // The denominator is the half of this people get wrong, and saying
        // which half is wrong beats saying that something is.
        setStatus(i18n("The lower number has to be 1, 2, 4, 8, 16, 32 or 64"));
        break;
    case Editor::Edit::Nothing:
        break;
    }
}

QStringList Session::instrumentNames() const
{
    QStringList names;
    for (const Instruments::Kind &kind : Instruments::all()) {
        names.append(kind.name);
    }
    return names;
}

QStringList Session::instrumentIds() const
{
    QStringList ids;
    for (const Instruments::Kind &kind : Instruments::all()) {
        ids.append(kind.id);
    }
    return ids;
}

QString Session::instrumentHere() const
{
    if (!hasScore()) {
        return QString();
    }
    return Instruments::byId(m_editor.score().tracks.at(m_currentTrack).instrumentType).name;
}

QString Session::trackNameHere() const
{
    return hasScore() ? m_editor.score().tracks.at(m_currentTrack).name : QString();
}

void Session::newScore()
{
    m_player.reset();
    m_editor.setScore(Editor::blankScore());
    m_order = Timeline::playedOrder(m_editor.score());
    m_clock = std::make_unique<Timeline::Clock>(m_editor.score(), m_order);
    // No file behind it yet, so the first save asks where to put one.
    m_fileName = QString();
    m_filePath = QString();
    m_currentTrack = 0;
    m_currentBar = -1;
    m_wasPlaying = false;

    rebuildLayout();
    rebuildPlayer();
    Q_EMIT scoreChanged();
    Q_EMIT currentTrackChanged();
    Q_EMIT mixerChanged();
    setStatus(i18n("A new score: one guitar, one bar, and somewhere to start"));
}

void Session::addTrack(const QString &instrumentId)
{
    if (m_editor.addTrack(instrumentId) == Editor::Edit::Done) {
        setCurrentTrack(m_editor.cursor().track);
        setStatus(i18n("Added %1", Instruments::byId(instrumentId).name));
    }
}

void Session::removeTrack(int track)
{
    if (m_editor.removeTrack(track) == Editor::Edit::Refused) {
        setStatus(i18n("A score keeps its last part: there would be nothing left to play"));
    }
}

void Session::renameTrack(int track, const QString &name)
{
    if (m_editor.renameTrack(track, name) == Editor::Edit::Refused) {
        setStatus(i18n("A part needs a name to be found by"));
    }
}

void Session::setTrackInstrument(int track, const QString &instrumentId)
{
    if (m_editor.setTrackInstrument(track, instrumentId) == Editor::Edit::Done) {
        // Said out loud, because the one thing it deliberately does not do is
        // the thing somebody might expect it to.
        setStatus(i18n("%1, tuned as it was", Instruments::byId(instrumentId).name));
    }
}

void Session::moveTrack(int track, int by)
{
    if (m_editor.moveTrack(track, by) == Editor::Edit::Done) {
        setCurrentTrack(m_editor.cursor().track);
    }
}

QString Session::tuningHere() const
{
    if (!hasScore()) {
        return QString();
    }
    QStringList names;
    for (const int pitch : m_editor.score().tracks.at(m_currentTrack).tuning) {
        names.append(NoteName::of(pitch));
    }
    return names.join(QLatin1Char(' '));
}

int Session::stringsHere() const
{
    return hasScore() ? m_editor.score().tracks.at(m_currentTrack).stringCount() : 0;
}

int Session::capoHere() const
{
    return hasScore() ? m_editor.score().tracks.at(m_currentTrack).capo : 0;
}

void Session::setTuningHere(const QString &names)
{
    QList<int> tuning;
    const QStringList parts =
        names.split(QRegularExpression(QStringLiteral("[\\s,]+")), Qt::SkipEmptyParts);
    for (const QString &part : parts) {
        const int pitch = NoteName::parse(part);
        if (pitch < 0) {
            setStatus(i18n("\"%1\" is not a note: write them like E2 A2 D3 G3 B3 E4", part));
            return;
        }
        tuning.append(pitch);
    }

    switch (m_editor.retune(tuning)) {
    case Editor::Edit::Done:
        setStatus(i18n("Retuned to %1", tuningHere()));
        break;
    case Editor::Edit::Refused:
        // Which of the two refusals it was, because they need different fixes.
        setStatus(tuning.size() != m_editor.score().tracks.at(m_currentTrack).tuning.size()
                      ? i18n("That track has %1 strings",
                             QString::number(
                                 m_editor.score().tracks.at(m_currentTrack).tuning.size()))
                      : i18n("That is outside the range of anything with frets on it"));
        break;
    case Editor::Edit::Nothing:
        break;
    }
}

void Session::setCapoHere(int fret)
{
    switch (m_editor.setCapo(fret)) {
    case Editor::Edit::Done:
        setStatus(fret == 0 ? i18n("Capo off")
                            : i18n("Capo at fret %1", QString::number(fret)));
        break;
    case Editor::Edit::Refused:
        setStatus(i18n("A capo goes on the first twelve frets"));
        break;
    case Editor::Edit::Nothing:
        break;
    }
}

void Session::transpose(int frets)
{
    if (m_editor.transpose(frets) != Editor::Edit::Refused) {
        return;
    }
    // Nothing there to move is not worth saying anything about; something
    // there that would not fit is, because the alternative is a key that
    // silently does nothing.
    if (m_editor.hasSelection()) {
        setStatus(i18n("Not all of that would fit on the neck, so none of it moved"));
        return;
    }
    setStatus(frets > 0 ? i18n("There is no room for that further up the neck")
                        : i18n("That would put the note behind the nut"));
}

void Session::moveNoteAcross(int strings)
{
    if (m_editor.moveNoteAcross(strings) != Editor::Edit::Refused) {
        return;
    }
    setStatus(i18n("There is nowhere for that note on the next string"));
}

void Session::toggleMark(const QString &mark)
{
    static const QHash<QString, Editor::Mark> marks = {
        {QStringLiteral("dead"), Editor::Mark::Dead},
        {QStringLiteral("ghost"), Editor::Mark::Ghost},
        {QStringLiteral("palmMute"), Editor::Mark::PalmMute},
        {QStringLiteral("letRing"), Editor::Mark::LetRing},
    };
    if (marks.contains(mark)) {
        m_editor.toggleMark(marks.value(mark));
    }
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
