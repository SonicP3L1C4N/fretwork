// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "gpif.h"
#include "midi.h"
#include "renderer.h"
#include "timeline.h"

#include <KAboutData>
#include <KLocalizedString>

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTextStream>

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

    out << QStringLiteral("  Guitar Pro %1, %2 bars notated, %3 played, %4\n")
               .arg(score.version)
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

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    KLocalizedString::setApplicationDomain(QByteArrayLiteral("fretwork"));

    KAboutData about(QStringLiteral("fretwork"),
                     i18n("Fretwork"),
                     QStringLiteral(FRETWORK_VERSION),
                     i18n("A tablature program that plays every track as its own stem"),
                     KAboutLicense::GPL_V3,
                     i18n("© 2026 Gary Bissett"));
    about.addAuthor(i18n("Gary Bissett"), i18n("Author"),
                    QStringLiteral("gary.bissett@gmail.com"));
    about.setHomepage(QStringLiteral("https://github.com/SonicP3L1C4N/fretwork"));
    about.setBugAddress(QByteArrayLiteral("https://github.com/SonicP3L1C4N/fretwork/issues"));
    KAboutData::setApplicationData(about);

    QCommandLineParser parser;
    about.setupCommandLine(&parser);
    parser.addPositionalArgument(QStringLiteral("file"),
                                 i18n("A Guitar Pro 7 or 8 file (.gp)"));
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
    parser.process(app);
    about.processCommandLine(&parser);

    QTextStream out(stdout);
    QTextStream error(stderr);

    const QStringList files = parser.positionalArguments();
    if (files.isEmpty()) {
        error << i18n("fretwork: name a .gp file to read\n");
        return 2;
    }

    int failures = 0;
    for (const QString &path : files) {
        QString why;
        const Score score = Gpif::read(path, &why);
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
