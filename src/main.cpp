// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "fwformat.h"
#include "gpif.h"
#include "session.h"
#include "midi.h"
#include "player.h"
#include "renderer.h"
#include "tablayout.h"
#include "tabpainter.h"
#include "timeline.h"

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
#include <QEventLoop>
#include <QTextStream>
#include <QTimer>

#include <algorithm>

namespace
{
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
                 const QString &soundFont)
{
    Render::Options options;
    options.soundFont = soundFont;

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
               const QStringList &solo, const QStringList &mute)
{
    Player::Options options;
    options.soundFont = soundFont;
    options.audioDriver = driver;

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

    QStringList heard;
    for (int index = 0; index < player.trackCount(); ++index) {
        if (player.isAudible(index)) {
            heard.append(score.tracks.at(index).name);
        }
    }
    out << QStringLiteral("  playing %1 through %2 — %3\n")
               .arg(clock(player.lengthSeconds()), player.driverName(),
                    heard.size() == player.trackCount()
                        ? i18n("all tracks")
                        : heard.join(QStringLiteral(", ")));
    out.flush();

    player.play();

    QEventLoop loop;
    QTimer ticker;
    QObject::connect(&ticker, &QTimer::timeout, [&] {
        out << QStringLiteral("\r  %1 / %2   ")
                   .arg(clock(player.positionSeconds()), clock(player.lengthSeconds()));
        out.flush();
        if (player.hasFinished()) {
            loop.quit();
        }
    });
    ticker.start(200);
    loop.exec();

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
    const QCommandLineOption soloed(QStringLiteral("solo"),
                                    i18n("Hear only these tracks; repeatable"),
                                    i18n("track"));
    parser.addOption(soloed);
    const QCommandLineOption muted(QStringLiteral("mute"),
                                   i18n("Silence these tracks; repeatable"), i18n("track"));
    parser.addOption(muted);
    const QCommandLineOption whichPage(QStringLiteral("page"),
                                       i18n("Which page to draw as an image"),
                                       i18n("number"), QStringLiteral("1"));
    parser.addOption(whichPage);
    parser.process(app);
    about.processCommandLine(&parser);

    QTextStream out(stdout);
    QTextStream error(stderr);

    const QStringList files = parser.positionalArguments();

    // Asked to produce something, this is a command line tool; asked for
    // nothing in particular, it is an application and opens a window.
    const bool asked = parser.isSet(info) || parser.isSet(midi) || parser.isSet(stems)
        || parser.isSet(render) || parser.isSet(pdf) || parser.isSet(png)
        || parser.isSet(playing);
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

    if (files.isEmpty()) {
        error << i18n("fretwork: name a .gp file to read\n");
        return 2;
    }

    int failures = 0;
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
                           parser.values(muted))) {
                ++failures;
            }
        }
        if (parser.isSet(render)) {
            if (!renderAudio(out, error, score, order, parser.value(render),
                             parser.value(soundFont))) {
                ++failures;
            }
        }
        if (files.size() > 1) {
            out << "\n";
        }
    }
    return failures == 0 ? 0 : 1;
}
