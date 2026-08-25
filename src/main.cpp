// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "gpif.h"
#include "timeline.h"

#include <KAboutData>
#include <KLocalizedString>

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QFileInfo>
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
        describe(out, score, Timeline::playedOrder(score, !parser.isSet(asNotated)));
        if (files.size() > 1) {
            out << "\n";
        }
    }
    return failures == 0 ? 0 : 1;
}
