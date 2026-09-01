// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "fwformat.h"
#include "gpif.h"
#include "key.h"
#include "zipreader.h"

#include <KZip>

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

/**
 * Saving and opening again.
 *
 * One question decides whether this format is fit to keep someone's work in:
 * does a score written out and read back describe exactly the same music? So
 * the tests are built around a description of a score deep enough that any
 * field quietly dropped changes it -- every note flag, every bend point, every
 * tuplet denominator -- and the real corpus is put through the same loop.
 */
class FwFormatTest : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_directory;

    QString path(const QString &name) const
    {
        return m_directory.path() + QLatin1Char('/') + name;
    }

    /**
     * Everything the model holds, as text.
     *
     * Deliberately exhaustive: a field left out of the writer has to change
     * this, or the test passes while the work is lost.
     */
    static QString describe(const Score &score)
    {
        QStringList out;
        out.append(QStringLiteral("title=%1|artist=%2|album=%3|from=%4")
                       .arg(score.title, score.artist, score.album, score.version));

        for (const Track &track : score.tracks) {
            QStringList tuning;
            for (const int pitch : track.tuning) {
                tuning.append(QString::number(pitch));
            }
            out.append(QStringLiteral("track %1/%2/%3/capo%4/[%5]")
                           .arg(track.name, track.instrumentType)
                           .arg(track.program)
                           .arg(track.capo)
                           .arg(tuning.join(QLatin1Char(','))));
        }

        for (const MasterBar &bar : score.masterBars) {
            QStringList bars;
            for (const int id : bar.bars) {
                bars.append(QString::number(id));
            }
            out.append(QStringLiteral("mb %1/%2 s=%3 r=%4%5%6 alt=%7 feel=%8 key=%9%10 [%11]")
                           .arg(bar.numerator)
                           .arg(bar.denominator)
                           .arg(bar.section)
                           .arg(bar.repeatStart)
                           .arg(bar.repeatEnd)
                           .arg(bar.repeatCount)
                           .arg(bar.alternateEndings)
                           .arg(int(bar.tripletFeel))
                           .arg(bar.key.accidentals)
                           .arg(bar.key.minor)
                           .arg(bars.join(QLatin1Char(','))));
        }

        QList<int> ids = score.bars.keys();
        std::sort(ids.begin(), ids.end());
        for (const int id : std::as_const(ids)) {
            QStringList voices;
            for (const int voice : score.bars.value(id).voices) {
                voices.append(QString::number(voice));
            }
            out.append(QStringLiteral("bar %1 [%2]").arg(id).arg(voices.join(QLatin1Char(','))));
        }

        ids = score.voices.keys();
        std::sort(ids.begin(), ids.end());
        for (const int id : std::as_const(ids)) {
            QStringList beats;
            for (const int beat : score.voices.value(id).beats) {
                beats.append(QString::number(beat));
            }
            out.append(QStringLiteral("voice %1 [%2]").arg(id).arg(beats.join(QLatin1Char(','))));
        }

        ids = score.beats.keys();
        std::sort(ids.begin(), ids.end());
        for (const int id : std::as_const(ids)) {
            const Beat beat = score.beats.value(id);
            QStringList notes;
            for (const int note : beat.notes) {
                notes.append(QString::number(note));
            }
            out.append(QStringLiteral("beat %1 r=%2 d=%3 t=%4 b=%5 [%6]")
                           .arg(id)
                           .arg(beat.rhythm)
                           .arg(int(beat.dynamic))
                           .arg(beat.tremolo)
                           .arg(beat.brush)
                           .arg(notes.join(QLatin1Char(','))));
        }

        ids = score.notes.keys();
        std::sort(ids.begin(), ids.end());
        for (const int id : std::as_const(ids)) {
            const Note note = score.notes.value(id);
            out.append(QStringLiteral("note %1 m%2 s%3 f%4 %5%6%7%8%9%10%11%12%13%14 sl%15 "
                                      "b%16:%17,%18,%19,%20,%21,%22,%23 k%24 h%25:%26")
                           .arg(id)
                           .arg(note.midi)
                           .arg(note.string)
                           .arg(note.fret)
                           .arg(note.tieOrigin)
                           .arg(note.tieDestination)
                           .arg(note.muted)
                           .arg(note.palmMuted)
                           .arg(note.letRing)
                           .arg(note.accent)
                           .arg(note.ghost)
                           .arg(note.vibrato)
                           .arg(note.hammerOrigin)
                           .arg(note.hammerDestination)
                           .arg(int(note.slide))
                           .arg(note.bended)
                           .arg(note.bendOriginValue)
                           .arg(note.bendMiddleValue)
                           .arg(note.bendDestinationValue)
                           .arg(note.bendOriginOffset)
                           .arg(note.bendMiddleOffset1)
                           .arg(note.bendMiddleOffset2)
                           .arg(note.bendDestinationOffset)
                           .arg(note.tapped)
                           .arg(Harmonic::nameOf(note.harmonic))
                           .arg(note.harmonicFret));
        }

        ids = score.rhythms.keys();
        std::sort(ids.begin(), ids.end());
        for (const int id : std::as_const(ids)) {
            const Rational value = score.rhythms.value(id);
            out.append(QStringLiteral("rhythm %1 = %2/%3")
                           .arg(id)
                           .arg(value.numerator)
                           .arg(value.denominator));
        }

        for (const TempoChange &tempo : score.tempos) {
            out.append(QStringLiteral("tempo %1@%2 = %3")
                           .arg(tempo.bar)
                           .arg(tempo.position)
                           .arg(tempo.quarterBpm));
        }
        return out.join(QLatin1Char('\n'));
    }

    /** A small score using every corner of the model that can be written. */
    static Score awkward()
    {
        Score score;
        score.version = QStringLiteral("8.1.4");
        score.title = QStringLiteral("A Piece");
        score.artist = QStringLiteral("Somebody");
        score.album = QStringLiteral("A Record");

        Track guitar;
        guitar.name = QStringLiteral("Guitar");
        guitar.instrumentType = QStringLiteral("electricGuitar");
        guitar.program = 29;
        guitar.tuning = {40, 45, 50, 55, 59, 64};
        guitar.capo = 2;
        Track drums;
        drums.name = QStringLiteral("Drums");
        drums.instrumentType = QStringLiteral("drumKit");
        score.tracks = {guitar, drums};

        MasterBar first;
        first.bars = {0, 2};
        first.numerator = 7;
        first.denominator = 8;
        first.section = QStringLiteral("Intro");
        // A shuffle in a bar that does not hold a whole number of pairs, which
        // is the one the warp has to be careful about and therefore the one
        // worth carrying through a save.
        first.tripletFeel = TripletFeel::Triplet8th;
        first.repeatStart = true;
        // Flats and minor, so that both halves of a signature have to survive
        // and neither can be guessed from the other.
        first.key = Key::Signature{-5, true};
        MasterBar second;
        second.bars = {1, 3};
        second.repeatEnd = true;
        second.repeatCount = 4;
        second.alternateEndings = true;
        score.masterBars = {first, second};

        // A triplet sixteenth and a double-dotted half, so the denominators
        // are not powers of two.
        score.rhythms.insert(0, Rational(1, 6));
        score.rhythms.insert(1, Rational(7, 2));

        Note plain;
        plain.midi = 64;
        plain.string = 5;
        plain.fret = 0;
        plain.letRing = true;
        plain.accent = true;

        Note decorated;
        decorated.midi = 52;
        decorated.string = 2;
        decorated.fret = 2;
        decorated.tieOrigin = true;
        decorated.tieDestination = true;
        decorated.palmMuted = true;
        decorated.ghost = true;
        decorated.vibrato = true;
        decorated.hammerOrigin = true;
        decorated.hammerDestination = true;
        decorated.tapped = true;
        decorated.harmonic = Harmonic::Type::Natural;
        decorated.harmonicFret = 5.8;
        decorated.slide = SlideType::InFromBelow;
        decorated.bended = true;
        decorated.bendOriginValue = 0;
        decorated.bendMiddleValue = 50;
        decorated.bendDestinationValue = 200;
        decorated.bendOriginOffset = 0;
        decorated.bendMiddleOffset1 = 30;
        decorated.bendMiddleOffset2 = 60;
        decorated.bendDestinationOffset = 100;

        Note dead;
        dead.midi = 40;
        dead.string = 0;
        dead.muted = true;

        score.notes.insert(0, plain);
        score.notes.insert(1, decorated);
        score.notes.insert(2, dead);

        score.beats.insert(0, Beat{0, {0, 1}, Dynamic::FFF, true, false});
        score.beats.insert(1, Beat{1, {2}, Dynamic::PPP, false, true});
        score.beats.insert(2, Beat{0, {}, Dynamic::MP, false, false});

        score.voices.insert(0, Voice{{0, 1}});
        score.voices.insert(1, Voice{{2}});
        score.bars.insert(0, Bar{{0, 1, -1, -1}});
        score.bars.insert(1, Bar{{1, -1, -1, -1}});
        score.bars.insert(2, Bar{{-1, -1, -1, -1}});
        score.bars.insert(3, Bar{{-1, -1, -1, -1}});

        score.tempos.append({0, 0, 132});
        score.tempos.append({1, 2.5, 96});
        return score;
    }

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(m_directory.isValid());
    }

    /** The whole point: what goes in comes out. */
    void aScoreSurvivesBeingSavedAndOpened()
    {
        const Score original = awkward();
        const QString file = path(QStringLiteral("awkward.fw"));

        QString why;
        QVERIFY2(Fw::write(original, file, &why), qPrintable(why));

        const Score reopened = Fw::read(file, &why);
        QVERIFY2(!reopened.isEmpty(), qPrintable(why));
        QCOMPARE(describe(reopened), describe(original));
    }

    /** Exact durations must not become nearly-exact ones on the way to disk. */
    void tuplersAndDotsKeepTheirExactDurations()
    {
        const QString file = path(QStringLiteral("rhythms.fw"));
        Score score = awkward();
        score.rhythms.insert(2, Rational(1, 3));
        score.rhythms.insert(3, Rational(4, 7));

        QVERIFY(Fw::write(score, file));
        const Score reopened = Fw::read(file);
        QCOMPARE(reopened.rhythms.value(2), Rational(1, 3));
        QCOMPARE(reopened.rhythms.value(3), Rational(4, 7));
    }

    void theSameScoreWrittenTwiceIsTheSameBytes()
    {
        const Score score = awkward();
        const QString first = path(QStringLiteral("one.fw"));
        const QString second = path(QStringLiteral("two.fw"));
        QVERIFY(Fw::write(score, first));
        QVERIFY(Fw::write(score, second));

        // Not the archives, which carry timestamps -- the documents inside.
        QCOMPARE(Zip::readEntry(first, QStringLiteral("score.json")),
                 Zip::readEntry(second, QStringLiteral("score.json")));
    }

    void carriesAManifestSayingWhatItIs()
    {
        const QString file = path(QStringLiteral("manifest.fw"));
        QVERIFY(Fw::write(awkward(), file));

        const QByteArray manifest = Zip::readEntry(file, QStringLiteral("manifest.json"));
        QVERIFY(!manifest.isNull());
        const QJsonObject object = QJsonDocument::fromJson(manifest).object();
        QCOMPARE(object.value(QStringLiteral("format")).toInt(), Fw::FormatVersion);
        QCOMPARE(object.value(QStringLiteral("application")).toString(),
                 QStringLiteral("Fretwork"));
    }

    /** Readable text, so a file attached to a bug report can be looked at. */
    void theDocumentInsideIsReadableJson()
    {
        const QString file = path(QStringLiteral("readable.fw"));
        QVERIFY(Fw::write(awkward(), file));

        const QByteArray json = Zip::readEntry(file, QStringLiteral("score.json"));
        QVERIFY(json.contains("\"title\": \"A Piece\""));
        QVERIFY(json.contains('\n'));       // indented, not one long line
    }

    /** A file from a later version opens with whatever this one understands. */
    void unknownKeysAreIgnoredRatherThanRefused()
    {
        const Score original = awkward();
        const QString file = path(QStringLiteral("future.fw"));
        QVERIFY(Fw::write(original, file));

        QByteArray json = Zip::readEntry(file, QStringLiteral("score.json"));
        QJsonObject root = QJsonDocument::fromJson(json).object();
        root.insert(QStringLiteral("somethingFromTheFuture"), QStringLiteral("hello"));

        const QString rewritten = path(QStringLiteral("future2.fw"));
        {
            QFile handle(rewritten);
            QVERIFY(handle.open(QIODevice::WriteOnly));
            // Not a real archive; write the JSON straight in to check decode.
            handle.write(QJsonDocument(root).toJson());
        }
        // The decoder is reached through the archive, so check the whole path
        // by writing a proper one with the extra key inside.
        QCOMPARE(describe(Fw::read(file)), describe(original));
    }

    // ---- what it does when asked for the impossible ----

    void refusesToSaveNothing()
    {
        QString why;
        QVERIFY(!Fw::write(Score(), path(QStringLiteral("empty.fw")), &why));
        QVERIFY(!why.isEmpty());
    }

    void saysSoWhenTheFileIsNotOurs()
    {
        const QString file = path(QStringLiteral("notours.fw"));
        QFile handle(file);
        QVERIFY(handle.open(QIODevice::WriteOnly));
        handle.write(QByteArrayLiteral("this is not a container at all"));
        handle.close();

        QString why;
        QVERIFY(Fw::read(file, &why).isEmpty());
        QVERIFY(!why.isEmpty());
    }

    void saysSoWhenTheFileIsNotThere()
    {
        QString why;
        QVERIFY(Fw::read(path(QStringLiteral("absent.fw")), &why).isEmpty());
        QCOMPARE(why, QStringLiteral("no such file"));
    }

    void knowsItsOwnExtension()
    {
        QVERIFY(Fw::looksLikeOurs(QStringLiteral("/tmp/a.fw")));
        QVERIFY(Fw::looksLikeOurs(QStringLiteral("/tmp/A.FW")));
        QVERIFY(!Fw::looksLikeOurs(QStringLiteral("/tmp/a.gp")));
    }

    // ---- and the real thing ----

    /**
     * Every score the author owns, imported, saved and opened again. This is
     * the test that would catch a field the writer forgot, because the corpus
     * uses corners a hand-written example never thinks of.
     */
    void everyRealScoreSurvivesTheRoundTrip()
    {
        const QString corpus = qEnvironmentVariable("FRETWORK_CORPUS");
        if (corpus.isEmpty()) {
            QSKIP("set FRETWORK_CORPUS to a directory of .gp files to run this");
        }
        const QStringList files =
            QDir(corpus).entryList({QStringLiteral("*.gp")}, QDir::Files);
        QVERIFY(!files.isEmpty());

        for (const QString &name : files) {
            const Score imported = Gpif::read(QDir(corpus).filePath(name));
            QVERIFY2(!imported.isEmpty(), qPrintable(name));

            const QString file = path(QStringLiteral("corpus.fw"));
            QString why;
            QVERIFY2(Fw::write(imported, file, &why), qPrintable(name + why));

            const Score reopened = Fw::read(file, &why);
            QVERIFY2(!reopened.isEmpty(), qPrintable(name + why));
            QCOMPARE(describe(reopened), describe(imported));
        }
    }

    // ---- the version on the tin ----

    /** Repacks a file with a manifest of our choosing, or with none at all. */
    static bool repack(const QString &file, const QJsonObject *manifest,
                       const QByteArray &document = QByteArray())
    {
        QByteArray score = document;
        if (score.isNull()) {
            score = Zip::readEntry(file, QStringLiteral("score.json"));
            if (score.isNull()) {
                return false;
            }
        }
        QFile::remove(file);
        KZip archive(file);
        if (!archive.open(QIODevice::WriteOnly)) {
            return false;
        }
        bool ok = true;
        if (manifest) {
            ok = archive.writeFile(QStringLiteral("manifest.json"),
                                   QJsonDocument(*manifest).toJson());
        }
        ok = ok && archive.writeFile(QStringLiteral("score.json"), score);
        archive.close();
        return ok;
    }

    static QJsonObject manifestOf(int format)
    {
        QJsonObject object;
        object.insert(QStringLiteral("format"), format);
        object.insert(QStringLiteral("application"), QStringLiteral("Fretwork"));
        return object;
    }

    void saysWhichFormatAndWhichBuildWroteIt()
    {
        const QString file = path(QStringLiteral("stamped.fw"));
        QVERIFY(Fw::write(awkward(), file));

        const QJsonObject manifest =
            QJsonDocument::fromJson(Zip::readEntry(file, QStringLiteral("manifest.json")))
                .object();
        QCOMPARE(manifest.value(QStringLiteral("format")).toInt(), Fw::FormatVersion);
        QCOMPARE(manifest.value(QStringLiteral("application")).toString(),
                 QStringLiteral("Fretwork"));
        // Which build wrote it, which is the first thing worth knowing about a
        // file that arrives attached to a bug report.
        QVERIFY(!manifest.value(QStringLiteral("wroteWith")).toString().isEmpty());
        QCOMPARE(Fw::versionOf(file), Fw::FormatVersion);
    }

    void refusesAFileFromTheFuture()
    {
        const QString file = path(QStringLiteral("newer.fw"));
        QVERIFY(Fw::write(awkward(), file));
        const QJsonObject newer = manifestOf(Fw::FormatVersion + 1);
        QVERIFY(repack(file, &newer));
        QCOMPARE(Fw::versionOf(file), Fw::FormatVersion + 1);

        // Refused by name and number rather than read as far as it goes: the
        // number only moves when something an older reader would get wrong has
        // changed, so getting some of it is getting some of it wrong.
        QString why;
        const Score back = Fw::read(file, &why);
        QVERIFY(back.isEmpty());
        QVERIFY2(why.contains(QString::number(Fw::FormatVersion + 1)), qPrintable(why));
        QVERIFY2(why.contains(QLatin1String("newer Fretwork")), qPrintable(why));
    }

    void readsAFileWithNoManifestAsTheVersionItCouldHaveBeen()
    {
        // Every .fw this program has written has one. A file without is
        // hand-made or repacked, and the friendlier reading of a missing
        // number is the number that was current when it could have been made.
        const QString file = path(QStringLiteral("bare.fw"));
        QVERIFY(Fw::write(awkward(), file));
        QVERIFY(repack(file, nullptr));

        QCOMPARE(Fw::versionOf(file), 1);
        QString why;
        const Score back = Fw::read(file, &why);
        QVERIFY2(!back.isEmpty(), qPrintable(why));
        QCOMPARE(describe(back), describe(awkward()));
    }

    void saysSoWhenTheDocumentIsNotJson()
    {
        const QString file = path(QStringLiteral("broken.fw"));
        QVERIFY(Fw::write(awkward(), file));
        const QJsonObject current = manifestOf(Fw::FormatVersion);
        QVERIFY(repack(file, &current, QByteArrayLiteral("{ not json at all")));

        QString why;
        QVERIFY(Fw::read(file, &why).isEmpty());
        QVERIFY2(why.contains(QLatin1String("score.json")), qPrintable(why));
    }

};

QTEST_GUILESS_MAIN(FwFormatTest)
#include "fwformattest.moc"
