// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "session.h"

#include "fwformat.h"
#include "gpif.h"
#include "instruments.h"
#include "lv2chain.h"
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
    // A rig is written a moment after it stops changing rather than while it
    // is changing: a knob under a finger moves continuously, and the file is
    // worth writing once when the finger comes off it.
    m_rigWriter.setSingleShot(true);
    m_rigWriter.setInterval(800);
    connect(&m_rigWriter, &QTimer::timeout, this, &Session::writeRig);

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
    // Read once: lilv parses every manifest on the machine to answer the
    // first question asked of it.
    m_plugins = Lv2::installed();
    // The same argument: banks are a handful of files, read once at the start
    // rather than every time a menu opens.
    const QStringList banks = Gx::banks();
    for (const QString &bank : banks) {
        m_voicings += Gx::read(bank);
    }

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

Session::~Session()
{
    // Whatever the timer was still holding. Quitting in the middle of a drag
    // is exactly the moment this feature exists for.
    if (m_rigWriter.isActive()) {
        m_rigWriter.stop();
        writeRig();
    }
}

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
QString iconFor(const Track &track, bool onInk = false)
{
    // The same badge in two tones. Every one of them is a filled disc, so the
    // ink version is a hole in a panel the colour of ink -- which is what the
    // effects deck is drawn on.
    const QString tone = onInk ? QStringLiteral("-paper") : QString();
    const auto badge = [&tone](const char *name) {
        return QStringLiteral("qrc:/instrument-%1%2.svg")
            .arg(QLatin1String(name), tone);
    };

    if (track.isPercussion()) {
        return badge("drums");
    }
    const QString kind = track.instrumentType.toLower();
    if (kind.contains(QLatin1String("bass"))) {
        return badge("bass");
    }
    if (kind.contains(QLatin1String("guitar")) || kind.contains(QLatin1String("ukulele"))
        || kind.contains(QLatin1String("banjo")) || kind.contains(QLatin1String("mandolin"))) {
        return badge("guitar");
    }
    if (kind.contains(QLatin1String("piano")) || kind.contains(QLatin1String("synth"))
        || kind.contains(QLatin1String("organ")) || kind.contains(QLatin1String("keyboard"))) {
        return badge("keys");
    }
    return badge("note");
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
    // Before the player, so the first one built is the one the rig describes
    // rather than a dry one thrown away a moment later.
    restoreRig();
    rebuildPlayer();

    Q_EMIT scoreChanged();
    Q_EMIT effectsChanged();
    Q_EMIT samplersChanged();
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
        // Nothing to play is not the same as being unable to play: an empty
        // score is where every new one starts.
        setProblem(QString());
        return;
    }

    Player::Options options;
    options.soundFont = m_soundFont;
    options.samplers = m_samplers;
    options.effects = chains();
    options.perTrackPorts = m_ports || m_following;
    options.followTransport = m_following;
    auto player = std::make_unique<Player>(m_editor.score(), m_order, options);
    if (!player->isValid()) {
        // Both channels: the status bar says it now, and the problem keeps
        // saying it -- including to somebody who has closed the status bar,
        // for whom this is the only thing that will open it again.
        setStatus(player->error());
        setProblem(player->error());
        return;
    }
    setProblem(QString());
    m_player = std::move(player);

    // Whatever the knobs were left at. A new Player is what happens when a
    // note is edited, and an amplifier that reset itself every time would be
    // one nobody could use.
    for (auto track = m_rig.constBegin(); track != m_rig.constEnd(); ++track) {
        const QList<Fitted> &chain = track.value();
        for (int stage = 0; stage < chain.size(); ++stage) {
            const QHash<quint32, float> &knobs = chain.at(stage).knobs;
            for (auto knob = knobs.constBegin(); knob != knobs.constEnd(); ++knob) {
                m_player->setEffectControl(track.key(), stage, knob.key(), knob.value());
            }
        }
    }
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

void Session::setProblem(const QString &problem)
{
    if (m_problem != problem) {
        m_problem = problem;
        Q_EMIT problemChanged();
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

bool Session::isEffectsShown() const
{
    return m_effectsShown;
}

void Session::setEffectsShown(bool shown)
{
    if (m_effectsShown != shown) {
        m_effectsShown = shown;
        Q_EMIT effectsShownChanged();
    }
}

QString Session::problem() const
{
    return m_problem;
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
    Q_EMIT effectsChanged();
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

QStringList Session::effectsHere() const
{
    return m_player ? m_player->effectsOn(m_currentTrack) : QStringList();
}

QVariantList Session::chainHere() const
{
    QVariantList chain;
    if (!m_player) {
        return chain;
    }
    const QList<Lv2::Stage> stages = m_player->chainOn(m_currentTrack);
    for (int index = 0; index < stages.size(); ++index) {
        QVariantList knobs;
        for (const Lv2::Control &control : stages.at(index).controls) {
            knobs.append(QVariantMap{
                {QStringLiteral("index"), control.index},
                {QStringLiteral("name"), control.name},
                {QStringLiteral("value"), control.value},
                {QStringLiteral("minimum"), control.minimum},
                {QStringLiteral("maximum"), control.maximum},
                {QStringLiteral("toggled"), control.toggled},
                {QStringLiteral("integer"), control.integer},
                // Empty for most plugins, which is why the panel sizes a
                // reading by the control's range as well: a number with no
                // unit still has a scale, and 20 out of 100 and 20 out of 20
                // are not the same reading.
                {QStringLiteral("unit"), control.unit},
                {QStringLiteral("choices"), control.choices},
                // The numbers behind the names. A control whose choices are
                // 0, 2 and 5 is not a control whose choices are 0, 1 and 2,
                // and a menu that sent back a position would set the wrong
                // amplifier.
                {QStringLiteral("choiceValues"), QVariant::fromValue(control.choiceValues)},
            });
        }
        // The URI as well as the name: a window that wants to draw a cabinet
        // as a cabinet has to know which plugin it is looking at, and a name
        // is what somebody called it rather than what it is.
        chain.append(QVariantMap{{QStringLiteral("name"), stages.at(index).name},
                                 {QStringLiteral("uri"), stages.at(index).uri},
                                 {QStringLiteral("stereo"),
                                  Lv2::describe(stages.at(index).uri).audioInputs == 2},
                                 {QStringLiteral("stage"), index},
                                 {QStringLiteral("controls"), knobs}});
    }
    return chain;
}

void Session::rememberRig()
{
    // A score with no file of its own has nowhere to keep a rig. Nothing is
    // lost by saying so quietly: saving the score writes the rig beside it.
    if (m_filePath.isEmpty()) {
        return;
    }
    m_rigWriter.start();
}

QStringList Session::chainOn(int track) const
{
    QStringList uris;
    for (const Fitted &stage : m_rig.value(track)) {
        uris.append(stage.uri);
    }
    return uris;
}

QHash<int, QStringList> Session::chains() const
{
    QHash<int, QStringList> all;
    for (auto track = m_rig.constBegin(); track != m_rig.constEnd(); ++track) {
        all.insert(track.key(), chainOn(track.key()));
    }
    return all;
}

Rig::Document Session::currentRig() const
{
    Rig::Document rig;

    // Every track named by either half of a rig, in track order so the file
    // reads the way the mixer does.
    QList<int> tracks = m_samplers.keys();
    for (const int track : m_rig.keys()) {
        if (!tracks.contains(track)) {
            tracks.append(track);
        }
    }
    std::sort(tracks.begin(), tracks.end());

    for (const int track : tracks) {
        Rig::Track one;
        one.track = track;
        one.sampler = m_samplers.value(track);
        one.chain = chainOn(track);

        const QList<Fitted> &chain = m_rig.value(track);
        for (int stage = 0; stage < chain.size(); ++stage) {
            if (!chain.at(stage).voicing.isEmpty()) {
                one.voicings.insert(stage, chain.at(stage).voicing);
            }

            // Knobs are held by port index and written by symbol. The plugin's
            // own manifest is what maps one to the other, and reading it costs
            // nothing -- `controlsOf` does not instantiate anything.
            const QHash<quint32, float> &values = chain.at(stage).knobs;
            if (values.isEmpty()) {
                continue;
            }
            for (const Lv2::Control &control : Lv2::controlsOf(chain.at(stage).uri)) {
                if (values.contains(control.index)) {
                    one.knobs.append({stage, control.symbol, values.value(control.index)});
                }
            }
        }
        rig.tracks.append(one);
    }
    return rig;
}

void Session::writeRig()
{
    if (m_filePath.isEmpty()) {
        return;
    }
    QString why;
    if (!Rig::write(currentRig(), Rig::pathFor(m_filePath), &why)) {
        // Said once, in the status bar, and not turned into a problem: the
        // score still plays, and a rig that could not be kept is a smaller
        // thing than one that could not be built.
        setStatus(i18n("The rig could not be kept: %1", why));
    }
}

void Session::restoreRig()
{
    m_samplers.clear();
    m_rig.clear();
    if (m_filePath.isEmpty()) {
        return;
    }

    QString why;
    const Rig::Document rig = Rig::read(Rig::pathFor(m_filePath), &why);
    if (!why.isEmpty()) {
        setStatus(i18n("The rig beside this score could not be read: %1", why));
        return;
    }
    if (rig.isEmpty()) {
        return;
    }

    const int trackCount = int(m_editor.score().tracks.size());
    for (const Rig::Track &track : rig.tracks) {
        // A rig written against a score that has since lost a part names a
        // track nobody has. Read past it rather than refusing the rest.
        if (track.track >= trackCount) {
            continue;
        }
        if (!track.sampler.isEmpty()) {
            m_samplers.insert(track.track, track.sampler);
        }
        if (track.chain.isEmpty()) {
            continue;
        }
        QList<Fitted> chain;
        for (const QString &uri : track.chain) {
            chain.append(Fitted{uri, {}, {}, {}});
        }
        for (auto stage = track.voicings.constBegin();
             stage != track.voicings.constEnd(); ++stage) {
            if (stage.key() >= 0 && stage.key() < chain.size()) {
                chain[stage.key()].voicing = stage.value();
            }
        }
        for (const Rig::Knob &knob : track.knobs) {
            if (knob.stage < 0 || knob.stage >= chain.size()) {
                continue;
            }
            // Back from the symbol to the port index this build of the plugin
            // uses, which is the whole reason the file holds the symbol.
            for (const Lv2::Control &control : Lv2::controlsOf(chain.at(knob.stage).uri)) {
                if (control.symbol == knob.symbol) {
                    chain[knob.stage].knobs.insert(control.index, knob.value);
                    break;
                }
            }
        }
        m_rig.insert(track.track, chain);
    }
}

QString Session::voicingOn(int stage) const
{
    const QList<Fitted> &chain = m_rig[m_currentTrack];
    return stage >= 0 && stage < chain.size() ? chain.at(stage).voicing : QString();
}

QStringList Session::voicingDeclinedOn(int stage) const
{
    const QList<Fitted> &chain = m_rig[m_currentTrack];
    return stage >= 0 && stage < chain.size() ? chain.at(stage).declined : QStringList();
}

void Session::applyKnobs(const QStringList &knobs, const QStringList &voicings)
{
    if (!m_player) {
        return;
    }

    // Voicings first: one sets a handful of controls at once, and a `--knob`
    // after it is somebody correcting the preset rather than being overruled
    // by it.
    for (const QString &given : voicings) {
        const qsizetype equals = given.indexOf(QLatin1Char('='));
        const qsizetype colon = given.indexOf(QLatin1Char(':'));
        if (equals < 0 || colon < 0 || colon > equals) {
            continue;
        }
        const int track = QStringView(given).left(colon).toInt();
        const int stage = QStringView(given).mid(colon + 1, equals - colon - 1).toInt();
        if (track != m_currentTrack) {
            continue;
        }
        applyVoicing(stage, given.mid(equals + 1));
    }

    for (const QString &given : knobs) {
        // track:stage:Symbol=value
        const qsizetype equals = given.indexOf(QLatin1Char('='));
        if (equals < 0) {
            continue;
        }
        const QStringList where = given.left(equals).split(QLatin1Char(':'));
        if (where.size() != 3) {
            continue;
        }
        const int track = where.at(0).toInt();
        const int stage = where.at(1).toInt();
        if (track != m_currentTrack) {
            continue;
        }
        const QList<Lv2::Stage> stages = m_player->chainOn(track);
        if (stage < 0 || stage >= stages.size()) {
            continue;
        }
        for (const Lv2::Control &control : stages.at(stage).controls) {
            if (control.symbol == where.at(2)) {
                setEffectControl(stage, int(control.index), given.mid(equals + 1).toDouble());
                break;
            }
        }
    }
    Q_EMIT effectsChanged();
}

void Session::useSoundFont(const QString &file)
{
    if (m_soundFont == file) {
        return;
    }
    m_soundFont = file;
    rebuildPlayer();
}

void Session::applyRig(const QVariantMap &samplers, const QVariantMap &effects)
{
    if (samplers.isEmpty() && effects.isEmpty()) {
        return;
    }
    for (auto entry = samplers.constBegin(); entry != samplers.constEnd(); ++entry) {
        m_samplers.insert(entry.key().toInt(), entry.value().toString());
    }
    for (auto entry = effects.constBegin(); entry != effects.constEnd(); ++entry) {
        const int track = entry.key().toInt();
        // The knobs go with the chain they belonged to. A knob is held by port
        // index, and an index means something only in the plugin it came from
        // -- so keeping them across a chain being *replaced* would set
        // somebody else's controls to numbers nobody chose. Appending a pedal
        // is the other case and deliberately keeps them; this is not that.
        const QStringList wanted = entry.value().toStringList();
        if (chainOn(track) != wanted) {
            QList<Fitted> chain;
            for (const QString &uri : wanted) {
                chain.append(Fitted{uri, {}, {}, {}});
            }
            m_rig.insert(track, chain);
        }
    }

    rebuildPlayer();
    if (!canPlay()) {
        // Whatever was asked for would not load. Dry rather than unplayable,
        // with the reason already in the status bar.
        m_samplers.clear();
        m_rig.clear();
        rebuildPlayer();
    }
    // What was typed on the command line is a rig like any other, and is kept
    // beside the score the same way.
    rememberRig();
    Q_EMIT samplersChanged();
    Q_EMIT effectsChanged();
}

void Session::setEffectControl(int stage, int index, double value)
{
    if (!m_player) {
        return;
    }
    // Straight to the running chain: turning a knob must not rebuild anything,
    // or every movement would silence the part for as long as a soundfont
    // takes to load.
    m_player->setEffectControl(m_currentTrack, stage, quint32(index), float(value));
    QList<Fitted> &chain = m_rig[m_currentTrack];
    if (stage >= 0 && stage < chain.size()) {
        chain[stage].knobs.insert(quint32(index), float(value));
    }
    rememberRig();
}

QVariantList Session::voicings() const
{
    QVariantList found;
    for (const Gx::Voicing &voicing : m_voicings) {
        found.append(QVariantMap{
            {QStringLiteral("name"), voicing.name},
            {QStringLiteral("bank"), voicing.bank},
            // What it is, in the words the bank uses, so the menu says more
            // than a song title: two voicings named for records can be the
            // same amplifier, and the valve is how you tell.
            {QStringLiteral("summary"),
             voicing.usesAmplifier()
                 ? QStringLiteral("%1 \u00b7 %2").arg(voicing.valve, voicing.toneStack)
                 : i18n("no amplifier")},
            {QStringLiteral("amplified"), voicing.usesAmplifier()}});
    }
    return found;
}

void Session::applyVoicing(int stage, const QString &name)
{
    if (!m_player) {
        return;
    }
    // The same resolver the command line uses, so that a name which sets the
    // amplifier for --render sets it here too. It used to be an exact match
    // that returned in silence when nothing answered, which meant a --voicing
    // the window could not resolve left no amplifier, no message and no entry
    // in the rig -- three ways of saying nothing at once.
    const Gx::Match match = Gx::named(m_voicings, name);
    if (match.outcome == Gx::Match::Unknown) {
        setStatus(i18n("No voicing is called %1.", name));
        return;
    }
    if (match.outcome == Gx::Match::Ambiguous) {
        setStatus(i18n("%1 could be %2.", name,
                       match.candidates.join(QStringLiteral(", "))));
        return;
    }

    const Gx::Voicing &voicing = match.voicing;
    const Gx::Fitting fitting = m_player->applyVoicing(m_currentTrack, stage, voicing);
    if (fitting.isEmpty()) {
        setStatus(i18n("%1 takes nothing from %2.", effectsHere().value(stage), voicing.name));
        return;
    }

    // Remembered the same way a hand-turned knob is, so that rebuilding the
    // chain later -- adding a pedal after it -- does not lose the voicing.
    // The chain held in a local first: `chainOn` returns a temporary, and
    // a loop over a member of one reads memory that has already gone.
    const QList<Lv2::Stage> stages = m_player->chainOn(m_currentTrack);
    for (const Gx::Setting &setting : fitting.settings) {
        for (const Lv2::Control &control : stages.at(stage).controls) {
            if (control.symbol == setting.symbol) {
                m_rig[m_currentTrack][stage].knobs.insert(control.index, setting.value);
                break;
            }
        }
    }

    // The name the bank gives it, not the fragment somebody typed. What goes
    // into the rig is read back by a later run, which will look it up again,
    // and "iron" is only unambiguous until somebody installs another bank.
    m_rig[m_currentTrack][stage].voicing = voicing.name;
    m_rig[m_currentTrack][stage].declined = fitting.declined;
    rememberRig();
    // The whole of it, unless the deck is open and already saying the whole
    // of it beside the tape. Two elided copies of one sentence is one sentence
    // nobody can read.
    setStatus(fitting.declined.isEmpty() || m_effectsShown
                  ? i18n("%1: %2 settings.", voicing.name,
                         QString::number(fitting.settings.size()))
                  : i18n("%1: %2 settings. %3", voicing.name,
                         QString::number(fitting.settings.size()),
                         fitting.declined.join(QStringLiteral(" "))));
    Q_EMIT effectsChanged();
}

QVariantList Session::availableEffects() const
{
    QVariantList found;
    for (const Lv2::Description &plugin : m_plugins) {
        found.append(QVariantMap{{QStringLiteral("name"), plugin.name},
                                 {QStringLiteral("uri"), plugin.uri},
                                 {QStringLiteral("stereo"), plugin.audioInputs == 2}});
    }
    return found;
}

void Session::addEffect(const QString &uri)
{
    const QHash<int, QList<Fitted>> was = m_rig;
    m_rig[m_currentTrack].append(Fitted{uri, {}, {}, {}});

    stop();
    rebuildPlayer();
    if (!canPlay()) {
        // It would not load. Back to the chain that worked rather than a part
        // that cannot be played; the status bar carries what the host said.
        m_rig = was;
        rebuildPlayer();
    } else {
        setStatus(i18n("%1 on %2", Lv2::describe(uri).name, trackNameHere()));
    }
    rememberRig();
    Q_EMIT effectsChanged();
}

void Session::removeEffect(int stage)
{
    QList<Fitted> &chain = m_rig[m_currentTrack];
    if (stage < 0 || stage >= chain.size()) {
        return;
    }
    // The knobs on the plugin that just left go with it, because they are its
    // own -- which is now a property of the stage rather than something this
    // has to remember to do.
    const QString name = Lv2::describe(chain.at(stage).uri).name;
    chain.removeAt(stage);
    if (chain.isEmpty()) {
        m_rig.remove(m_currentTrack);
    }
    stop();
    rebuildPlayer();
    rememberRig();
    setStatus(i18n("%1 off %2", name, trackNameHere()));
    Q_EMIT effectsChanged();
}

void Session::moveEffect(int stage, int by)
{
    QList<Fitted> &chain = m_rig[m_currentTrack];
    const int to = stage + by;
    if (stage < 0 || stage >= chain.size() || to < 0 || to >= chain.size()) {
        return;
    }
    chain.move(stage, to);

    // Rebuilt rather than reordered in place: a chain is instantiated in the
    // order it is given, and a plugin that has already been told how many
    // channels arrive at it cannot be handed a different neighbour without
    // being built again. The settings are safe across it because they moved
    // with the stage rather than staying at an index.
    stop();
    rebuildPlayer();
    rememberRig();
    setStatus(i18n("%1 is now %2 of %3 on %4",
                   Lv2::describe(chain.at(to).uri).name, QString::number(to + 1),
                   QString::number(chain.size()), trackNameHere()));
    Q_EMIT effectsChanged();
}

void Session::clearEffects()
{
    if (!m_rig.contains(m_currentTrack)) {
        return;
    }
    // Dry means dry, and nothing has to be remembered to make it so: the
    // stages held their own settings and went with them.
    m_rig.remove(m_currentTrack);
    stop();
    rebuildPlayer();
    rememberRig();
    setStatus(i18n("%1 is dry again", trackNameHere()));
    Q_EMIT effectsChanged();
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
    rememberRig();
    Q_EMIT samplersChanged();
}

int Session::portLinks() const
{
    return m_player ? m_player->portLinkCount() : 0;
}

bool Session::isGraphRolling() const
{
    return m_player && m_player->isPlaying();
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
    setStatus(on ? (m_player && m_player->canDriveTransport()
                        ? i18n("Following the graph — and able to start it, so play still "
                               "works here")
                        : i18n("Following the graph — it rolls when the graph does"))
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
        setStatus(on ? (portLinks() > 0
                            ? i18n("%1 pairs of ports, and the speakers are plugged in",
                                   QString::number(portCount()))
                            : i18n("%1 pairs of ports — nothing is linked to them yet",
                                   QString::number(portCount())))
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
    // Following, the transport belongs to the graph -- but pressing play can
    // still start the graph's, where there is a way to. Only where there is
    // not does the button go dead, because then it really could do nothing.
    return m_player && m_player->isValid()
        && (!m_following || m_player->canDriveTransport());
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

QString Session::trackIconHereOnInk() const
{
    if (m_currentTrack < 0 || m_currentTrack >= m_editor.score().tracks.size()) {
        return QString();
    }
    return iconFor(m_editor.score().tracks.at(m_currentTrack), true);
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


/**
 * The word for a speed, from the table every metronome prints on its side.
 *
 * The boundaries differ by a few beats between one book and the next and are
 * given here as the widely printed ones. Deliberately one-way: a term is a
 * description of a number and never a way to set it, because "Allegro" is a
 * range and a tempo is a value.
 */
QString Session::tempoTermHere() const
{
    const double beats = tempoHere();
    if (beats < 25) return i18nc("tempo", "Larghissimo");
    if (beats < 45) return i18nc("tempo", "Grave");
    if (beats < 60) return i18nc("tempo", "Largo");
    if (beats < 66) return i18nc("tempo", "Larghetto");
    if (beats < 76) return i18nc("tempo", "Adagio");
    if (beats < 92) return i18nc("tempo", "Andante");
    if (beats < 120) return i18nc("tempo", "Moderato");
    if (beats < 156) return i18nc("tempo", "Allegro");
    if (beats < 176) return i18nc("tempo", "Vivace");
    if (beats < 200) return i18nc("tempo", "Presto");
    return i18nc("tempo", "Prestissimo");
}

int Session::beatsHere() const
{
    if (!hasScore()) {
        return 0;
    }
    const MasterBar &bar = m_editor.score().masterBars.at(m_editor.cursor().bar);
    const Rational beat = Timeline::beatOf(bar);
    if (!(Rational(0) < beat)) {
        return 0;
    }
    // Rounded rather than truncated: a bar whose length is not a whole number
    // of beats -- an incomplete one, which this program marks rather than
    // corrects -- still has a number of beats somebody counts it in.
    return std::max(1, int(std::lround(bar.length().toDouble() / beat.toDouble())));
}

int Session::beatNow() const
{
    if (!m_player || !m_clock || !hasScore() || !m_player->isPlaying()) {
        return -1;
    }
    const double seconds = m_player->positionSeconds();
    const int pass = Timeline::barAt(m_editor.score(), m_order, *m_clock, seconds);
    if (pass < 0) {
        return -1;
    }
    const MasterBar &bar = m_editor.score().masterBars.at(m_order.at(pass));
    const Rational beat = Timeline::beatOf(bar);
    if (!(Rational(0) < beat)) {
        return -1;
    }

    // Each beat asked of the clock rather than the elapsed time divided by
    // one: a tempo may change inside a bar, and the clock is the only thing
    // that knows where the beats land when it does. A bar is a dozen beats at
    // the outside, so this is a walk of a dozen.
    const Rational start = Timeline::quartersAtPass(m_editor.score(), m_order, pass);
    int sounding = -1;
    int index = 0;
    for (Rational at; at < bar.length(); at += beat, ++index) {
        if (seconds + 1e-6 >= m_clock->secondsAt(start + at)) {
            sounding = index;
        }
    }
    return sounding;
}

QString Session::effectsSummary(int track) const
{
    if (!m_player) {
        return QString();
    }
    const QStringList names = m_player->effectsOn(track);
    return names.join(QStringLiteral(" \u00b7 "));
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
    // The rig goes with it. Saving under a new name is the one moment a score
    // acquires a path it did not have, so it is also the moment a rig built
    // before the first save has somewhere to live.
    writeRig();
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
