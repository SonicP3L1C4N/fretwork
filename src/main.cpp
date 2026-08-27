// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "audioinput.h"
#include "fwformat.h"
#include "gpif.h"
#include "lv2chain.h"
#include "session.h"
#include "midi.h"
#include "pitchdetector.h"
#include "player.h"
#include "renderer.h"
#include "tablayout.h"
#include "tabpainter.h"
#include "swing.h"
#include "timeline.h"
#include "tuner.h"

#include <KAboutData>
#include <KLocalizedContext>
#include <KLocalizedString>

#include <QCommandLineParser>
#include <QApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QStandardPaths>
#include <QImage>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QHash>
#include <QTextStream>
#include <QTimer>

#include <algorithm>
#include <csignal>

#include <unistd.h>

namespace
{
/**
 * `--sfz 1=guitar.sfz`, repeated, into a table by track number.
 *
 * By track rather than for all of them, because a library exists for the
 * guitar and does not for the organ, and a score is usually both.
 */
QHash<int, QString> samplersFrom(const QStringList &given, QTextStream &error, int *bad)
{
    QHash<int, QString> samplers;
    for (const QString &pair : given) {
        const int split = pair.indexOf(QLatin1Char('='));
        bool number = false;
        const int track = split > 0 ? pair.left(split).toInt(&number) : -1;
        if (!number || track < 0 || split + 1 >= pair.size()) {
            error << QStringLiteral("fretwork: --sfz wants a track and a file, as in "
                                    "--sfz 1=guitar.sfz, not \"%1\"\n")
                         .arg(pair);
            ++*bad;
            continue;
        }
        samplers.insert(track, pair.mid(split + 1));
    }
    return samplers;
}

/** `--lv2 1=uri,uri` into a table of chains by track number. */
QHash<int, QStringList> effectsFrom(const QStringList &given, QTextStream &error, int *bad)
{
    QHash<int, QStringList> chains;
    for (const QString &pair : given) {
        const int split = pair.indexOf(QLatin1Char('='));
        bool number = false;
        const int track = split > 0 ? pair.left(split).toInt(&number) : -1;
        if (!number || track < 0 || split + 1 >= pair.size()) {
            error << QStringLiteral("fretwork: --lv2 wants a track and one or more plugin "
                                    "URIs, as in --lv2 1=urn:one,urn:two\n");
            ++*bad;
            continue;
        }
        chains.insert(track, pair.mid(split + 1).split(QLatin1Char(','), Qt::SkipEmptyParts));
    }
    return chains;
}

QString clock(double seconds)
{
    const int whole = int(seconds + 0.5);
    return QStringLiteral("%1:%2").arg(whole / 60).arg(whole % 60, 2, 10, QLatin1Char('0'));
}

/**
 * What this copy made of the file, in the shape a person can check against the
 * score in front of them: the tuning it read, and how many notes it found.
 *
 * A converter that silently produces something wrong is the failure mode worth
 * designing against, and the cheapest defence is printing what it understood.
 */
void describe(QTextStream &out, const Score &score, const QList<int> &order)
{
    out << (score.title.isEmpty() ? i18n("(untitled)") : score.title) << " — "
        << (score.artist.isEmpty() ? i18n("(no artist)") : score.artist) << "\n";

    // A score saved by Fretwork has no Guitar Pro version to report, and
    // saying "Guitar Pro ," is worse than saying nothing.
    const QString provenance = score.version.isEmpty()
        ? QString()
        : i18n("Guitar Pro %1, ", score.version);
    out << QStringLiteral("  %1%2 bars notated, %3 played, %4\n")
               .arg(provenance)
               .arg(score.masterBars.size())
               .arg(order.size())
               .arg(clock(Timeline::seconds(score, order)));

    const QList<Timeline::TempoEvent> tempos = Timeline::tempoMap(score, order);
    if (!score.tempos.isEmpty()) {
        QStringList written;
        for (const TempoChange &change : score.tempos) {
            written.append(QStringLiteral("%1 bpm at bar %2")
                               .arg(change.quarterBpm, 0, 'g', 4)
                               .arg(change.bar + 1));
            if (written.size() == 6) {
                break;
            }
        }
        out << "  tempo   " << written.join(QStringLiteral(", ")) << "\n";
    }

    // What a shuffle does to a score is large and entirely invisible in a note
    // count, so it is worth saying out loud that one was found.
    int swung = 0;
    TripletFeel feel = TripletFeel::None;
    for (const MasterBar &bar : score.masterBars) {
        if (bar.tripletFeel != TripletFeel::None) {
            ++swung;
            feel = bar.tripletFeel;
        }
    }
    if (swung > 0) {
        out << QStringLiteral("  feel    %1\n")
                   .arg(swung == score.masterBars.size()
                            ? i18n("%1 throughout", Swing::nameOf(feel))
                            : i18n("%1 on %2 of %3 bars", Swing::nameOf(feel),
                                   QString::number(swung),
                                   QString::number(score.masterBars.size())));
    }

    if (Timeline::hasAlternateEndings(score)) {
        out << i18n("  warning: this score has alternate endings, which are not "
                    "flattened yet — the played order is approximate\n");
    }

    for (int index = 0; index < score.tracks.size(); ++index) {
        const Track &track = score.tracks.at(index);
        QStringList tuning;
        for (const int pitch : track.tuning) {
            tuning.append(QString::number(pitch));
        }
        out << QStringLiteral("  [%1] %2 %3 prog %4 %5 notes   tuning %6\n")
                   .arg(index)
                   .arg(track.name, -22)
                   .arg(track.instrumentType, -16)
                   .arg(track.program, -4)
                   .arg(Timeline::notesFor(score, index, order).size(), 6)
                   .arg(tuning.isEmpty() ? QStringLiteral("—") : tuning.join(QLatin1Char(' ')));
    }
}
}

/**
 * A MIDI file, or one per track.
 *
 * Whatever the format could not express is printed rather than swallowed: a
 * file that is quietly less than the score is worse than one that says so.
 */
bool writeMidi(QTextStream &out, QTextStream &error, const Score &score,
               const QList<int> &order, const QString &source,
               const QString &single, const QString &directory)
{
    const auto report = [&out](const Midi::Compromises &compromises) {
        for (const QString &compromise : compromises) {
            out << "  note: " << compromise << "\n";
        }
    };

    if (!single.isEmpty()) {
        QString why;
        Midi::Compromises compromises;
        if (!Midi::write(score, order, single, -1, &why, &compromises)) {
            error << QStringLiteral("fretwork: %1: %2\n").arg(single, why);
            return false;
        }
        report(compromises);
        out << "  wrote " << single << "\n";
        return true;
    }

    QDir folder(directory);
    if (!folder.mkpath(QStringLiteral("."))) {
        error << QStringLiteral("fretwork: cannot make %1\n").arg(directory);
        return false;
    }

    const QString stem = QFileInfo(source).completeBaseName();
    bool ok = true;
    for (int index = 0; index < score.tracks.size(); ++index) {
        QString name = score.tracks.at(index).name;
        name.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9._-]+")),
                     QStringLiteral("_"));
        const QString path = folder.filePath(
            QStringLiteral("%1-%2.mid").arg(index, 2, 10, QLatin1Char('0')).arg(name));

        QString why;
        Midi::Compromises compromises;
        if (!Midi::write(score, order, path, index, &why, &compromises)) {
            error << QStringLiteral("fretwork: %1: %2\n").arg(path, why);
            ok = false;
            continue;
        }
        report(compromises);
        out << QStringLiteral("  wrote %1\n").arg(path);
    }

    const QString mix = folder.filePath(stem + QStringLiteral(".mid"));
    QString why;
    Midi::Compromises compromises;
    if (Midi::write(score, order, mix, -1, &why, &compromises)) {
        report(compromises);
        out << "  wrote " << mix << "\n";
    } else {
        error << QStringLiteral("fretwork: %1: %2\n").arg(mix, why);
        ok = false;
    }
    return ok;
}

/**
 * Audio, a track at a time.
 *
 * The peak of each stem is printed because a silent file is the failure this
 * cannot otherwise notice: everything succeeds, the disk fills up, and only
 * listening finds out.
 */
bool renderAudio(QTextStream &out, QTextStream &error, const Score &score,
                 const QList<int> &order, const QString &directory,
                 const QString &soundFont, bool click, const QHash<int, QString> &samplers,
                 const QHash<int, QStringList> &effects)
{
    Render::Options options;
    options.soundFont = soundFont;
    options.click = click;
    options.samplers = samplers;
    options.effects = effects;
    options.effects = effects;

    QString why;
    QList<Render::Written> written;
    if (!Render::stems(score, order, directory, options, &why, &written)) {
        error << QStringLiteral("fretwork: %1\n").arg(why);
        return false;
    }

    for (const Render::Written &file : std::as_const(written)) {
        out << QStringLiteral("  %1  %2  peak %3\n")
                   .arg(QFileInfo(file.path).fileName(), -28)
                   .arg(clock(file.seconds))
                   .arg(file.peak, 0, 'f', 2);
        if (file.peak <= 0.0001f) {
            out << i18n("    warning: this one is silent\n");
        }
    }
    return true;
}

/**
 * The tablature, on paper or as an image.
 *
 * How many pages it came to is printed, because that is the cheapest signal
 * that the layout did something sensible: a score that lays out in one page or
 * in four hundred has gone wrong in a way no test of mine would catch.
 */
bool drawTab(QTextStream &out, QTextStream &error, const Score &score, int trackIndex,
             const QString &pdfPath, const QString &pngPath, int page)
{
    if (trackIndex < 0 || trackIndex >= score.tracks.size()) {
        error << QStringLiteral("fretwork: no track %1 in this score\n").arg(trackIndex);
        return false;
    }

    const Tab::Layout layout = Tab::layOut(score, trackIndex);
    if (layout.isEmpty()) {
        error << i18n("fretwork: there is nothing to draw\n");
        return false;
    }

    bool ok = true;
    if (!pdfPath.isEmpty()) {
        QString why;
        if (Tab::toPdf(layout, pdfPath, &why)) {
            out << QStringLiteral("  wrote %1  (%2, %3 pages)\n")
                       .arg(pdfPath, layout.trackName).arg(layout.pages.size());
        } else {
            error << QStringLiteral("fretwork: %1: %2\n").arg(pdfPath, why);
            ok = false;
        }
    }

    if (!pngPath.isEmpty()) {
        const int index = std::clamp(page - 1, 0, int(layout.pages.size()) - 1);
        const QImage image = Tab::toImage(layout, index);
        if (image.isNull() || !image.save(pngPath)) {
            error << QStringLiteral("fretwork: cannot write %1\n").arg(pngPath);
            ok = false;
        } else {
            out << QStringLiteral("  wrote %1  (%2, page %3 of %4)\n")
                       .arg(pngPath, layout.trackName)
                       .arg(index + 1)
                       .arg(layout.pages.size());
        }
    }
    return ok;
}

/**
 * Playback, from the same engine that renders the stems.
 *
 * The position is polled on a timer rather than pushed from the audio thread,
 * which is the rule the whole player is built on: a thread that emits signals
 * is a thread that allocates, and a thread that allocates drops out.
 */
bool playScore(QTextStream &out, QTextStream &error, const Score &score,
               const QList<int> &order, const QString &soundFont, const QString &driver,
               const QStringList &solo, const QStringList &mute, bool click, bool ports,
               bool follow, const QHash<int, QString> &samplers,
               const QHash<int, QStringList> &effects)
{
    Player::Options options;
    options.soundFont = soundFont;
    options.audioDriver = driver;
    options.perTrackPorts = ports || follow;
    options.followTransport = follow;
    options.samplers = samplers;

    Player player(score, order, options);
    if (!player.isValid()) {
        error << QStringLiteral("fretwork: %1\n").arg(player.error());
        return false;
    }

    for (const QString &track : solo) {
        player.setSolo(track.toInt(), true);
    }
    for (const QString &track : mute) {
        player.setMuted(track.toInt(), true);
    }
    player.setClickEnabled(click);

    QStringList heard;
    for (int index = 0; index < player.trackCount(); ++index) {
        if (player.isAudible(index)) {
            heard.append(score.tracks.at(index).name);
        }
    }
    if (player.portCount() > 0) {
        out << i18n("  %1 pairs of ports in the graph — link them and record\n",
                    QString::number(player.portCount()));
    }
    if (player.isFollowing()) {
        // Said before it looks like nothing is happening: a follower sits
        // still until whatever it is following starts.
        out << i18n("  following the graph's transport — it rolls when the graph does\n");
    }
    out << QStringLiteral("  playing %1 through %2 — %3%4\n")
               .arg(clock(player.lengthSeconds()), player.driverName(),
                    heard.size() == player.trackCount()
                        ? i18n("all tracks")
                        : heard.join(QStringLiteral(", ")),
                    click ? i18n(", with a click") : QString());
    out.flush();

    player.play();

    QEventLoop loop;
    QTimer ticker;
    QObject::connect(&ticker, &QTimer::timeout, [&] {
        out << QStringLiteral("\r  %1 / %2   ")
                   .arg(clock(player.positionSeconds()), clock(player.lengthSeconds()));
        out.flush();
        // A follower has no end of its own: the piece is over when whatever
        // it is following says so, and until then it waits at the last bar.
        if (player.hasFinished() && !player.isFollowing()) {
            loop.quit();
        }
    });
    ticker.start(200);
    loop.exec();

    out << "\n";
    return true;
}

namespace
{
/**
 * Ctrl-C, caught rather than obeyed.
 *
 * The tuner is the first thing in the program that runs until it is stopped,
 * and a terminal left holding a half-written line with no newline on it is a
 * terminal somebody has to press return in. One flag, checked on the same
 * timer as everything else.
 */
volatile std::sig_atomic_t interrupted = 0;

void onInterrupt(int)
{
    interrupted = 1;
}

/**
 * The needle: fifty cents either side of the mark.
 *
 * Fifty because that is the point at which the answer stops being "this string
 * is out" and starts being "this is the wrong string" -- the scale a person
 * reads while turning a peg wants to be over the range they are turning it
 * through, not over the whole octave.
 */
QString meter(double cents, bool inTune)
{
    constexpr int arm = 12;         // characters either side of the centre
    QString dial(arm * 2 + 1, QLatin1Char('.'));
    dial[arm] = QLatin1Char('|');

    const int at = std::clamp(int(std::lround(cents / 50.0 * arm)), -arm, arm);
    dial[arm + at] = inTune ? QLatin1Char('#') : QLatin1Char('o');
    return dial;
}

/** One line of tuner, whatever it currently has to say. */
QString readout(const Pitch::Detection &detection, const Tuner::Reading &reading,
                const QList<Tuner::StringTarget> &targets, const Pitch::Settings &settings)
{
    if (!reading.heard) {
        // Silence and noise are different answers and a tuner that gave the
        // same one for both would have somebody checking their cable when the
        // real trouble is that they strummed a chord at it.
        return detection.level < settings.minimumLevel
            ? i18n("listening — play a string")
            : i18n("hearing something, but no note in it");
    }
    if (reading.string < 0) {
        // Something was played and it is not one of these strings. Saying what
        // it was is more use than saying nothing, and much more use than
        // picking the nearest string anyway.
        return QStringLiteral("%1 %2   %3   %4 Hz")
            .arg(reading.noteName, -4)
            .arg(meter(reading.nearestCents, false), Tuner::describe(reading))
            .arg(reading.hertz, 0, 'f', 1);
    }

    const Tuner::StringTarget &target = targets.at(reading.string);
    const bool inTune = std::abs(reading.cents) <= Tuner::InTuneCents;
    return QStringLiteral("%1 %2 %3   %4   %5   %6 Hz")
        .arg(i18n("string %1", reading.string + 1), -9)
        .arg(target.name, -4)
        .arg(meter(reading.cents, inTune))
        .arg(inTune ? i18n("in tune")
                    : i18n("%1%2 ¢", reading.cents > 0 ? QStringLiteral("+") : QString(),
                           QString::number(std::lround(reading.cents))),
             -10)
        .arg(Tuner::describe(reading), -10)
        .arg(reading.hertz, 0, 'f', 1);
}
}

/**
 * Tuning to the score in front of you.
 *
 * The whole of the difference between this and any other tuner is the
 * `targets` argument: they came out of the file, so a piece in drop C asks for
 * a C and says which string it is, rather than reporting a C and leaving the
 * player to know whether that was the right answer.
 */
bool runTuner(QTextStream &out, QTextStream &error, const QList<Tuner::StringTarget> &targets,
              const QString &what, const QString &device)
{
    if (targets.isEmpty()) {
        error << i18n("fretwork: that track has no strings to tune\n");
        return false;
    }

    QStringList names;
    for (const Tuner::StringTarget &target : targets) {
        names.append(target.name);
    }
    out << QStringLiteral("  %1: %2\n").arg(what, names.join(QLatin1Char(' ')));

    AudioInput::Options options;
    options.device = device;
    AudioInput input(options);
    if (!input.isValid()) {
        error << QStringLiteral("fretwork: %1\n").arg(input.error());
        return false;
    }

    // The stream is connected when the graph says so and not when it is asked,
    // and the sample rate it settled on is not known until then -- which the
    // detector needs before it can be built at all.
    QElapsedTimer waiting;
    waiting.start();
    while (!input.isRunning() && waiting.elapsed() < 3000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }
    if (!input.isRunning()) {
        error << i18n("fretwork: nothing is sending any audio in\n");
        return false;
    }

    // Connected is not the same as delivering. An input that is streaming and
    // sending nothing would otherwise sit there asking to be played to for as
    // long as anybody was willing to keep playing to it.
    while (input.framesCaptured() == 0 && waiting.elapsed() < 3000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }
    if (input.framesCaptured() == 0) {
        error << i18n("fretwork: that input is connected but no audio is arriving on it\n");
        return false;
    }

    Pitch::Settings settings;
    settings.sampleRate = input.sampleRate();
    Pitch::Detector detector(settings);
    const int window = Pitch::windowFor(settings);
    std::vector<float> heard(size_t(window), 0.0f);

    out << i18n("  listening on %1 at %2 Hz — Ctrl-C to stop\n\n",
                device.isEmpty() ? i18n("the default input") : device,
                QString::number(int(input.sampleRate())));
    out.flush();

    const bool terminal = isatty(fileno(stdout)) != 0;
    std::signal(SIGINT, onInterrupt);

    QEventLoop loop;
    QTimer ticker;
    QString last;
    QObject::connect(&ticker, &QTimer::timeout, [&] {
        if (interrupted) {
            loop.quit();
            return;
        }
        Pitch::Detection detection;
        Tuner::Reading reading;
        if (input.latest(heard.data(), window) == window) {
            detection = detector.detect(heard.data(), window);
            if (detection.voiced) {
                reading = Tuner::read(detection.hertz, detection.clarity, targets);
            }
        }

        const QString line = readout(detection, reading, targets, settings);
        if (terminal) {
            // Padded to the longest line it has drawn, so a short one does not
            // leave the tail of a long one behind it.
            out << QStringLiteral("\r  %1").arg(line, -64);
        } else if (line != last) {
            out << QStringLiteral("  %1\n").arg(line);
        }
        last = line;
        out.flush();
    });
    // Fifteen times a second: faster than the ear settles and slower than the
    // detection window slides, so every tick is looking at new audio.
    ticker.start(66);
    loop.exec();

    std::signal(SIGINT, SIG_DFL);
    out << "\n";
    return true;
}

int main(int argc, char *argv[])
{
    // Drawing needs a font database, and a font database needs a GUI
    // application -- but this also has to work over ssh, where there is no
    // screen to ask for. With one, the window opens; without, the command line
    // still draws and renders.
    if (qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY")
        && qEnvironmentVariableIsEmpty("DISPLAY")) {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }
    QApplication app(argc, argv);
    KLocalizedString::setApplicationDomain(QByteArrayLiteral("fretwork"));

    // KAboutData carries a desktop file name of its own, defaulting to
    // org.kde.<name>, and the portal then looks for an application that does
    // not exist. Claimed only when the .desktop file is really installed, so
    // that running out of the build directory does not lie about itself --
    // the same lesson Signpost learned the hard way.
    const QString desktopId = QStringLiteral("io.github.sonicp3l1c4n.fretwork");
    const bool installed = !QStandardPaths::locate(QStandardPaths::GenericDataLocation,
                                                   QStringLiteral("applications/") + desktopId
                                                       + QStringLiteral(".desktop"))
                                .isEmpty();

    // The installed icon where there is one; the copy compiled in otherwise,
    // so running out of the build directory still looks like the application.
    QIcon icon = QIcon::fromTheme(desktopId);
    if (icon.isNull()) {
        icon = QIcon(QStringLiteral(":/sc-apps-io.github.sonicp3l1c4n.fretwork.svg"));
    }
    QGuiApplication::setWindowIcon(icon);

    KAboutData about(QStringLiteral("fretwork"),
                     i18n("Fretwork"),
                     QStringLiteral(FRETWORK_VERSION),
                     i18n("A tablature program that plays every track as its own stem"),
                     KAboutLicense::GPL_V3,
                     i18n("© 2026 Gary Bissett"));
    about.addAuthor(i18n("Gary Bissett"), i18n("Author"),
                    QStringLiteral("gary.bissett@gmail.com"));
    if (installed) {
        about.setDesktopFileName(desktopId);
    }
    about.setHomepage(QStringLiteral("https://github.com/SonicP3L1C4N/fretwork"));
    about.setBugAddress(QByteArrayLiteral("https://github.com/SonicP3L1C4N/fretwork/issues"));
    KAboutData::setApplicationData(about);

    QCommandLineParser parser;
    about.setupCommandLine(&parser);
    parser.addPositionalArgument(QStringLiteral("file"),
                                 i18n("A Guitar Pro file (.gp) or a Fretwork score (.fw)"));
    const QCommandLineOption info(QStringLiteral("info"),
                                  i18n("Print what the file holds, and exit"));
    parser.addOption(info);
    const QCommandLineOption asNotated(QStringLiteral("no-repeats"),
                                       i18n("Read the score as notated rather than as played"));
    parser.addOption(asNotated);
    const QCommandLineOption midi(QStringList{QStringLiteral("m"), QStringLiteral("midi")},
                                  i18n("Write a MIDI file"), i18n("file.mid"));
    parser.addOption(midi);
    const QCommandLineOption stems(QStringLiteral("stems"),
                                   i18n("Write one MIDI file per track into a directory"),
                                   i18n("directory"));
    parser.addOption(stems);
    const QCommandLineOption render(QStringLiteral("render"),
                                    i18n("Render one WAV per track, and a mix, into a "
                                         "directory"),
                                    i18n("directory"));
    parser.addOption(render);
    const QCommandLineOption soundFont(QStringLiteral("soundfont"),
                                       i18n("The SoundFont to render with"), i18n("file.sf2"));
    parser.addOption(soundFont);
    const QCommandLineOption pdf(QStringLiteral("pdf"),
                                 i18n("Draw the tablature as a PDF"), i18n("file.pdf"));
    parser.addOption(pdf);
    const QCommandLineOption png(QStringLiteral("png"),
                                 i18n("Draw one page of tablature as an image"),
                                 i18n("file.png"));
    parser.addOption(png);
    const QCommandLineOption whichTrack(QStringLiteral("track"),
                                        i18n("Which track to draw; the first by default"),
                                        i18n("number"), QStringLiteral("0"));
    parser.addOption(whichTrack);
    const QCommandLineOption playing(QStringLiteral("play"),
                                     i18n("Play it now, through the speakers"));
    parser.addOption(playing);
    const QCommandLineOption audioDriver(QStringLiteral("audio-driver"),
                                         i18n("Which FluidSynth audio driver to play "
                                              "through; \"file\" writes to disk instead"),
                                         i18n("name"));
    parser.addOption(audioDriver);
    const QCommandLineOption porting(QStringLiteral("ports"),
                                     i18n("Give every track a pair of ports in the audio "
                                          "graph, for a DAW to record"));
    parser.addOption(porting);
    const QCommandLineOption lv2(QStringLiteral("lv2"),
                                 i18n("Put a chain of LV2 effects on a track, nearest the "
                                      "instrument first; repeatable"),
                                 i18n("track=uri,uri"));
    parser.addOption(lv2);
    const QCommandLineOption listEffects(QStringLiteral("effects"),
                                         i18n("List the LV2 effects installed, and exit"));
    parser.addOption(listEffects);
    const QCommandLineOption sfz(QStringLiteral("sfz"),
                                 i18n("Play a track from an SFZ instrument rather than a "
                                      "General MIDI programme; repeatable"),
                                 i18n("track=file.sfz"));
    parser.addOption(sfz);
    const QCommandLineOption following(QStringLiteral("follow"),
                                       i18n("Take the transport from the audio graph, so "
                                            "a DAW starts and locates it; implies --ports"));
    parser.addOption(following);
    const QCommandLineOption saving(QStringLiteral("save"),
                                    i18n("Convert to a Fretwork score"), i18n("file.fw"));
    parser.addOption(saving);
    const QCommandLineOption clicking(QStringLiteral("click"),
                                     i18n("Count it out: a metronome on every beat, "
                                          "played live or written as a stem of its own"));
    parser.addOption(clicking);
    const QCommandLineOption soloed(QStringLiteral("solo"),
                                    i18n("Hear only these tracks; repeatable"),
                                    i18n("track"));
    parser.addOption(soloed);
    const QCommandLineOption muted(QStringLiteral("mute"),
                                   i18n("Silence these tracks; repeatable"), i18n("track"));
    parser.addOption(muted);
    const QCommandLineOption tune(QStringLiteral("tune"),
                                 i18n("Tune to this score, listening on an audio input"));
    parser.addOption(tune);
    const QCommandLineOption audioInput(QStringLiteral("input"),
                                        i18n("Which audio input to listen on; the "
                                             "desktop's own by default"),
                                        i18n("name"));
    parser.addOption(audioInput);
    const QCommandLineOption whichPage(QStringLiteral("page"),
                                       i18n("Which page to draw as an image"),
                                       i18n("number"), QStringLiteral("1"));
    parser.addOption(whichPage);
    parser.process(app);
    about.processCommandLine(&parser);

    QTextStream out(stdout);
    QTextStream error(stderr);

    const QStringList files = parser.positionalArguments();

    // Asked what it can do rather than what a file holds: no score needed.
    if (parser.isSet(listEffects)) {
        const QList<Lv2::Description> found = Lv2::installed();
        for (const Lv2::Description &plugin : found) {
            out << QStringLiteral("  %1  %2 in %3 out  %4\n")
                       .arg(plugin.name, -34)
                       .arg(plugin.audioInputs)
                       .arg(plugin.audioOutputs)
                       .arg(plugin.uri);
        }
        out << i18n("  %1 usable in a chain\n", QString::number(found.size()));
        return 0;
    }

    // Asked to produce something, this is a command line tool; asked for
    // nothing in particular, it is an application and opens a window.
    const bool asked = parser.isSet(info) || parser.isSet(midi) || parser.isSet(stems)
        || parser.isSet(render) || parser.isSet(pdf) || parser.isSet(png)
        || parser.isSet(playing) || parser.isSet(tune) || parser.isSet(saving);
    if (!asked) {
        QQuickStyle::setStyle(QStringLiteral("org.kde.desktop"));
        QQmlApplicationEngine engine;
        // The module is put at the root of the resources rather than under
        // qt/qml, which is where the engine looks by default.
        engine.addImportPath(QStringLiteral(":/"));
        engine.rootContext()->setContextObject(new KLocalizedContext(&engine));
        engine.setInitialProperties({{QStringLiteral("initialFile"), files.value(0)}});
        engine.loadFromModule("org.kde.fretwork", "Main");
        if (engine.rootObjects().isEmpty()) {
            error << i18n("fretwork: the window could not be created\n");
            return 1;
        }
        return app.exec();
    }

    // The one thing here that is useful with no file at all: a guitar is in
    // standard tuning until a score says otherwise, and somebody who wants to
    // check the low E before opening anything should not have to open
    // something.
    if (parser.isSet(tune) && files.isEmpty()) {
        out << i18n("no score — standard tuning\n");
        return runTuner(out, error, Tuner::standardGuitar(), i18n("guitar"),
                        parser.value(audioInput))
            ? 0
            : 1;
    }

    if (files.isEmpty()) {
        error << i18n("fretwork: name a .gp file to read\n");
        return 2;
    }

    int failures = 0;
    const QHash<int, QString> samplers = samplersFrom(parser.values(sfz), error, &failures);
    const QHash<int, QStringList> effects = effectsFrom(parser.values(lv2), error, &failures);

    for (const QString &path : files) {
        QString why;
        // Whatever the window can open, this can: a score saved from Fretwork
        // must not be a file only half the program understands.
        const Score score = Fw::looksLikeOurs(path) ? Fw::read(path, &why)
                                                    : Gpif::read(path, &why);
        if (score.isEmpty()) {
            error << QStringLiteral("fretwork: %1: %2\n")
                         .arg(QFileInfo(path).fileName(),
                              why.isEmpty() ? i18n("could not be read") : why);
            ++failures;
            continue;
        }
        if (files.size() > 1) {
            out << QFileInfo(path).fileName() << "\n";
        }
        const QList<int> order = Timeline::playedOrder(score, !parser.isSet(asNotated));
        describe(out, score, order);

        // For one of ours, which version of the format it is. A file attached
        // to a bug report should not need unzipping to answer that.
        if (Fw::looksLikeOurs(path)) {
            out << QStringLiteral("  format  %1\n")
                       .arg(i18n("Fretwork %1", QString::number(Fw::versionOf(path))));
        }

        if (parser.isSet(midi) || parser.isSet(stems)) {
            if (!writeMidi(out, error, score, order, path, parser.value(midi),
                           parser.value(stems))) {
                ++failures;
            }
        }
        if (parser.isSet(pdf) || parser.isSet(png)) {
            if (!drawTab(out, error, score, parser.value(whichTrack).toInt(),
                         parser.value(pdf), parser.value(png),
                         parser.value(whichPage).toInt())) {
                ++failures;
            }
        }
        if (parser.isSet(playing)) {
            if (!playScore(out, error, score, order, parser.value(soundFont),
                           parser.value(audioDriver), parser.values(soloed),
                           parser.values(muted), parser.isSet(clicking),
                           parser.isSet(porting), parser.isSet(following), samplers, effects)) {
                ++failures;
            }
        }
        if (parser.isSet(saving)) {
            QString why;
            if (Fw::write(score, parser.value(saving), &why)) {
                out << QStringLiteral("  %1  %2\n")
                           .arg(QFileInfo(parser.value(saving)).fileName(), -28)
                           .arg(i18n("Fretwork %1",
                                     QString::number(Fw::FormatVersion)));
            } else {
                error << QStringLiteral("fretwork: %1\n").arg(why);
                ++failures;
            }
        }
        if (parser.isSet(tune)) {
            // Whichever track was asked for, unless it has no strings: a
            // request to tune to the drums is a request that cannot be
            // honoured, and the first track that can be is a better answer
            // than an error.
            int chosen = std::clamp(parser.value(whichTrack).toInt(), 0,
                                    int(score.tracks.size()) - 1);
            if (Tuner::targetsFor(score.tracks.at(chosen)).isEmpty()) {
                for (int index = 0; index < score.tracks.size(); ++index) {
                    if (!Tuner::targetsFor(score.tracks.at(index)).isEmpty()) {
                        chosen = index;
                        break;
                    }
                }
            }
            const Track &track = score.tracks.at(chosen);
            const QString what = track.capo > 0
                ? i18n("%1, capo %2", track.name, QString::number(track.capo))
                : track.name;
            if (!runTuner(out, error, Tuner::targetsFor(track), what,
                          parser.value(audioInput))) {
                ++failures;
            }
        }
        if (parser.isSet(render)) {
            if (!renderAudio(out, error, score, order, parser.value(render),
                             parser.value(soundFont), parser.isSet(clicking), samplers, effects)) {
                ++failures;
            }
        }
        if (files.size() > 1) {
            out << "\n";
        }
    }
    return failures == 0 ? 0 : 1;
}
