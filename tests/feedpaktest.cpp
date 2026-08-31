// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "feedpak.h"
#include "timeline.h"

#include <KZip>

#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QTest>

/**
 * A practice pack, written out of a score.
 *
 * The data is tested and the audio is not, deliberately: the manifest and the
 * arrangement are the parts with decisions in them, and rendering four minutes
 * of sound to check a JSON field is a slow way to learn nothing.
 *
 * The test that matters most is the one about string numbering. Getting it
 * backwards produces a pack that plays, is wrong, and is wrong in a way that
 * looks like a transcription error rather than a bug -- and a learner would be
 * marked against it.
 */
class FeedpakTest : public QObject
{
    Q_OBJECT

private:
    /** A guitar and a bass, one bar each, plus a kit that has no arrangement. */
    static Score twoPartsAndAKit()
    {
        Score score;
        Track guitar;
        guitar.name = QStringLiteral("Lead Guitar");
        guitar.instrumentType = QStringLiteral("electricGuitar");
        guitar.tuning = {40, 45, 50, 55, 59, 64};
        Track bass;
        bass.name = QStringLiteral("Bass");
        bass.instrumentType = QStringLiteral("electricBass");
        bass.tuning = {28, 33, 38, 43};
        Track kit;
        kit.name = QStringLiteral("Drums");
        kit.instrumentType = QStringLiteral("drumKit");
        score.tracks = {guitar, bass, kit};

        score.title = QStringLiteral("A Piece");
        score.artist = QStringLiteral("Somebody");
        score.rhythms.insert(0, Rational(1));
        score.tempos.append({0, 0, 120});

        MasterBar master;
        master.bars = {0, 1, 2};
        score.masterBars.append(master);
        for (int track = 0; track < 3; ++track) {
            score.bars.insert(track, Bar{{track, -1, -1, -1}});
            score.voices.insert(track, Voice{{}});
        }

        // Four notes on the guitar's top string, walking up.
        QList<int> beats;
        for (int index = 0; index < 4; ++index) {
            Note note;
            note.string = 5;
            note.fret = index * 2;
            note.midi = 64 + note.fret;
            note.palmMuted = index == 1;
            note.accent = index == 2;
            score.notes.insert(index, note);
            Beat beat;
            beat.rhythm = 0;
            beat.notes = {index};
            score.beats.insert(index, beat);
            beats.append(index);
        }
        score.voices.insert(0, Voice{beats});
        return score;
    }

    static QJsonArray notesOf(const Score &score, int track)
    {
        return Feedpak::arrangementFor(score, track, Timeline::playedOrder(score))
            .value(QStringLiteral("notes"))
            .toArray();
    }

private Q_SLOTS:
    /**
     * String nought is the lowest, in a pack as in this program.
     *
     * Measured rather than assumed. The sample packs are all in standard
     * tuning and write six zeros, which says nothing about which end counts
     * from -- so the check was to read a known melody off the numbers in a real
     * pack. `s: 4, f: 7` and `s: 5, f: 5` open Ode to Joy as F sharp and A only
     * if nought is the low string; counting from the other end gives a leap
     * down to A2 in the middle of a stepwise tune, which is not a melody
     * anybody wrote. This test pins the conclusion.
     */
    void stringNoughtIsTheLowestOne()
    {
        const Score score = twoPartsAndAKit();
        const QJsonArray notes = notesOf(score, 0);
        QCOMPARE(notes.size(), 4);
        // The notes were written on the top string of six, which is five.
        for (const QJsonValue &note : notes) {
            QCOMPARE(note.toObject().value(QStringLiteral("s")).toInt(), 5);
        }
        QCOMPARE(notes.first().toObject().value(QStringLiteral("f")).toInt(), 0);
        QCOMPARE(notes.last().toObject().value(QStringLiteral("f")).toInt(), 6);
    }

    /**
     * Times and lengths are seconds, which is what a highway scrolls in.
     *
     * And `sus` is how long the note *sounds* rather than how long it is
     * written, which is the same number for most notes and is not for a palm
     * mute: the second note here is muted with the hand and rings for half its
     * written length, so that is what the pack says. A learner watching the
     * highway is watching the sound.
     */
    void notesAreInSecondsAndSayHowLongTheyRing()
    {
        const QJsonArray notes = notesOf(twoPartsAndAKit(), 0);
        QCOMPARE(notes.size(), 4);
        // Four crotchets at 120: one every half second, starting at nought.
        for (int index = 0; index < notes.size(); ++index) {
            const QJsonObject note = notes.at(index).toObject();
            QVERIFY(qAbs(note.value(QStringLiteral("t")).toDouble() - index * 0.5) < 0.001);
        }
        for (const int index : {0, 2, 3}) {
            const double sus = notes.at(index).toObject().value(QStringLiteral("sus")).toDouble();
            QVERIFY2(qAbs(sus - 0.5) < 0.001, qPrintable(QString::number(sus)));
        }
        const QJsonObject muted = notes.at(1).toObject();
        QVERIFY(muted.value(QStringLiteral("pm")).toBool());
        QVERIFY(qAbs(muted.value(QStringLiteral("sus")).toDouble() - 0.25) < 0.001);
    }

    /** The techniques that survive the trip, and only those. */
    void onlyWhatTheProgramCanHonestlySayIsSaid()
    {
        const QJsonArray notes = notesOf(twoPartsAndAKit(), 0);
        QVERIFY(notes.at(1).toObject().value(QStringLiteral("pm")).toBool());
        QVERIFY(!notes.at(0).toObject().value(QStringLiteral("pm")).toBool());
        QVERIFY(notes.at(2).toObject().value(QStringLiteral("ac")).toBool());

        // Everything this program cannot say is written as absent rather than
        // left out, so that a reader does not fill the gap with a default and
        // mark somebody against a slide nobody played.
        const QJsonObject first = notes.first().toObject();
        for (const QString &flag : {QStringLiteral("hm"), QStringLiteral("tr"),
                                    QStringLiteral("vb"), QStringLiteral("tp")}) {
            QVERIFY2(first.contains(flag), qPrintable(flag));
            QVERIFY2(!first.value(flag).toBool(), qPrintable(flag));
        }
        for (const QString &flag : {QStringLiteral("sl"), QStringLiteral("pkd")}) {
            QCOMPARE(first.value(flag).toInt(), -1);
        }
    }

    /** A tuning is offsets from standard, which is what six zeros must mean. */
    void aTuningIsOffsetsAndNotPitches()
    {
        Score score = twoPartsAndAKit();
        const QByteArray standard = Feedpak::manifestFor(score, 2.0);
        QVERIFY2(standard.contains("tuning:\n  - 0\n  - 0"), standard.constData());

        // Drop D: the lowest string down a tone, and nothing else moved.
        score.tracks[0].tuning[0] = 38;
        const QJsonObject dropped =
            Feedpak::arrangementFor(score, 0, Timeline::playedOrder(score));
        const QJsonArray tuning = dropped.value(QStringLiteral("tuning")).toArray();
        QCOMPARE(tuning.at(0).toInt(), -2);
        QCOMPARE(tuning.at(1).toInt(), 0);
    }

    /**
     * A drum kit is a stem but not an arrangement, and it is recognised by
     * being a kit rather than by having no strings.
     *
     * The trap, and one this found on a real transcription rather than in
     * theory: a kit imports with a tuning of *six zeros*, not with none. So a
     * check that counted strings called it fretted and wrote it an
     * arrangement of frets on a drum. The importer already documents the same
     * shape of mistake about programme numbers.
     */
    void aKitIsAStemButNotAnArrangement()
    {
        Score score = twoPartsAndAKit();
        // As a real import gives it: six strings' worth of nothing.
        score.tracks[2].tuning = {0, 0, 0, 0, 0, 0};

        QCOMPARE(Feedpak::playableParts(score), QList<int>({0, 1}));

        const QByteArray manifest = Feedpak::manifestFor(score, 2.0);
        const QString text = QString::fromUtf8(manifest);
        // Every part has a stem, including the kit: a learner muting the drums
        // needs them as a file whether or not anything is written down.
        QVERIFY(text.contains(QStringLiteral("file: 'stems/drums.wav'")));
        // And only the fretted ones have an arrangement.
        QVERIFY(text.contains(QStringLiteral("file: 'arrangements/lead_guitar.json'")));
        QVERIFY(text.contains(QStringLiteral("file: 'arrangements/bass.json'")));
        QVERIFY2(!text.contains(QStringLiteral("arrangements/drums.json")), qPrintable(text));
    }

    /** A pack is a ZIP with a manifest and an arrangement per part in it. */
    void aPackIsAZipSomethingElseCanOpen()
    {
        QTemporaryDir folder;
        QVERIFY(folder.isValid());
        const QString path = folder.path() + QStringLiteral("/piece.feedpak");

        const Score score = twoPartsAndAKit();
        Feedpak::Options options;
        // No audio: this is about the shape of the pack.
        options.stems = false;
        QString error;
        QVERIFY2(Feedpak::write(score, Timeline::playedOrder(score), path, options, &error),
                 qPrintable(error));

        KZip archive(path);
        QVERIFY(archive.open(QIODevice::ReadOnly));
        QVERIFY(archive.directory()->entry(QStringLiteral("manifest.yaml")));
        QVERIFY(archive.directory()->entry(QStringLiteral("arrangements/lead_guitar.json")));
        QVERIFY(archive.directory()->entry(QStringLiteral("arrangements/bass.json")));
    }

    /** Nothing fretted is nothing to write, and it says so. */
    void aScoreWithNoFrettedPartIsRefusedAndExplained()
    {
        Score score;
        Track kit;
        kit.name = QStringLiteral("Drums");
        kit.instrumentType = QStringLiteral("drumKit");
        score.tracks = {kit};
        MasterBar master;
        master.bars = {0};
        score.masterBars.append(master);
        score.bars.insert(0, Bar{{0, -1, -1, -1}});
        score.voices.insert(0, Voice{{}});

        QTemporaryDir folder;
        QString error;
        Feedpak::Options options;
        options.stems = false;
        QVERIFY(!Feedpak::write(score, Timeline::playedOrder(score),
                                folder.path() + QStringLiteral("/x.feedpak"), options, &error));
        QVERIFY(!error.isEmpty());
    }
};

QTEST_GUILESS_MAIN(FeedpakTest)
#include "feedpaktest.moc"
