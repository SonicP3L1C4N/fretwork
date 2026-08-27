// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "lv2chain.h"
#include "renderer.h"
#include "timeline.h"
#include "wav.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>
#include <QtEndian>

/**
 * The audio side: the WAV writer on its own, and the renderer against a real
 * SoundFont where the machine has one.
 *
 * The renderer's tests are skipped rather than failed without a SoundFont,
 * because a build machine without fluid-soundfont-gm installed is a missing
 * dependency and not a broken program. The WAV writer is tested unconditionally
 * -- it needs nothing.
 */
class RenderTest : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_directory;

    QString path(const QString &name) const
    {
        return m_directory.path() + QLatin1Char('/') + name;
    }

    struct Wav {
        int channels = 0;
        int bits = 0;
        int rate = 0;
        qint64 frames = 0;
        QList<qint16> samples;
    };

    /** Reads back a WAV strictly enough to catch a wrong length in the header. */
    static Wav readWav(const QString &file)
    {
        Wav wav;
        QFile handle(file);
        [&] { QVERIFY(handle.open(QIODevice::ReadOnly)); }();
        const QByteArray data = handle.readAll();

        [&] { QCOMPARE(data.left(4), QByteArrayLiteral("RIFF")); }();
        [&] { QCOMPARE(data.mid(8, 4), QByteArrayLiteral("WAVE")); }();
        // The RIFF length counts everything after those first eight bytes.
        [&] {
            QCOMPARE(qFromLittleEndian<quint32>(data.constData() + 4),
                     quint32(data.size() - 8));
        }();

        wav.channels = qFromLittleEndian<quint16>(data.constData() + 22);
        wav.rate = int(qFromLittleEndian<quint32>(data.constData() + 24));
        wav.bits = qFromLittleEndian<quint16>(data.constData() + 34);

        [&] { QCOMPARE(data.mid(36, 4), QByteArrayLiteral("data")); }();
        const quint32 bytes = qFromLittleEndian<quint32>(data.constData() + 40);
        [&] { QCOMPARE(bytes, quint32(data.size() - 44)); }();

        wav.frames = bytes / (wav.channels * wav.bits / 8);
        for (quint32 at = 0; at + 1 < bytes; at += 2) {
            wav.samples.append(qFromLittleEndian<qint16>(data.constData() + 44 + at));
        }
        return wav;
    }

    /** A guitar and a drum kit, two bars, so a render has something to do. */
    static QByteArray contentsOf(const QString &path)
    {
        QFile file(path);
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    }

    static Score twoTracks()
    {
        Score score;
        Track guitar;
        guitar.name = QStringLiteral("Guitar");
        guitar.instrumentType = QStringLiteral("electricGuitar");
        guitar.program = 29;
        for (int string = 0; string < 6; ++string) {
            guitar.tuning.append(40 + string * 5);
        }
        Track drums;
        drums.name = QStringLiteral("Drums");
        drums.instrumentType = QStringLiteral("drumKit");
        score.tracks = {guitar, drums};

        for (int bar = 0; bar < 2; ++bar) {
            MasterBar master;
            master.bars = {bar * 2, bar * 2 + 1};
            score.masterBars.append(master);
        }
        score.rhythms.insert(0, Rational(1));
        score.tempos.append({0, 0, 120});

        int id = 0;
        for (int bar = 0; bar < 2; ++bar) {
            for (int track = 0; track < 2; ++track) {
                QList<int> beats;
                for (int beat = 0; beat < 4; ++beat) {
                    Note note;
                    note.midi = track == 0 ? 64 - beat : 36 + (beat % 2) * 2;
                    note.string = track == 0 ? 5 - beat : -1;
                    score.notes.insert(id, note);
                    score.beats.insert(id, Beat{0, {id}, Dynamic::F, false, false});
                    beats.append(id);
                    ++id;
                }
                const int voice = bar * 2 + track;
                score.voices.insert(voice, Voice{beats});
                score.bars.insert(voice, Bar{{voice, -1, -1, -1}});
            }
        }
        return score;
    }

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(m_directory.isValid());
    }

    // ---- the WAV writer, which needs nothing ----

    void writesAHeaderThatMatchesItsContents()
    {
        const QString file = path(QStringLiteral("tone.wav"));
        {
            WavWriter writer(file, 48000);
            QVERIFY(writer.isOpen());
            QList<float> left(480, 0.5f);
            QList<float> right(480, -0.5f);
            QVERIFY(writer.write(left.constData(), right.constData(), 480));
            QVERIFY(writer.close());
        }

        const Wav wav = readWav(file);
        QCOMPARE(wav.channels, 2);
        QCOMPARE(wav.bits, 16);
        QCOMPARE(wav.rate, 48000);
        QCOMPARE(wav.frames, 480);
        QCOMPARE(wav.samples.at(0), qint16(16384));   // 0.5 of full scale
        QCOMPARE(wav.samples.at(1), qint16(-16384));
    }

    /** The lengths are written blank and patched, so closing must not be optional. */
    void theDestructorClosesTheFile()
    {
        const QString file = path(QStringLiteral("implicit.wav"));
        {
            WavWriter writer(file, 44100);
            QList<float> silence(256, 0.0f);
            QVERIFY(writer.write(silence.constData(), silence.constData(), 256));
        }
        const Wav wav = readWav(file);
        QCOMPARE(wav.frames, 256);
        QCOMPARE(wav.rate, 44100);
    }

    /** Loud and wrong beats quiet and inside out. */
    void tooLoudIsClippedRatherThanWrapped()
    {
        const QString file = path(QStringLiteral("loud.wav"));
        {
            WavWriter writer(file, 48000);
            const QList<float> over = {4.0f, -4.0f};
            QVERIFY(writer.write(over.constData(), over.constData(), 2));
            QVERIFY(writer.peak() > 1.0f);
        }
        const Wav wav = readWav(file);
        for (const qint16 sample : wav.samples) {
            QVERIFY(sample == 32767 || sample == -32767);
        }
    }

    void reportsTheLoudestSampleItSaw()
    {
        WavWriter writer(path(QStringLiteral("peak.wav")), 48000);
        const QList<float> quiet(8, 0.25f);
        QVERIFY(writer.write(quiet.constData(), quiet.constData(), 8));
        QCOMPARE(writer.peak(), 0.25f);
    }

    void refusesADirectoryItCannotWriteTo()
    {
        WavWriter writer(path(QStringLiteral("no/such/place/x.wav")), 48000);
        QVERIFY(!writer.isOpen());
        QVERIFY(!writer.error().isEmpty());
    }

    // ---- the renderer ----

    void rendersATrackPerFileAndAMix()
    {
        if (Render::findSoundFont().isEmpty()) {
            QSKIP("no SoundFont on this machine; install fluid-soundfont-gm");
        }

        const Score score = twoTracks();
        const QString folder = path(QStringLiteral("stems"));
        Render::Options options;
        options.tailSeconds = 0.5;

        QString why;
        QList<Render::Written> written;
        QVERIFY2(Render::stems(score, Timeline::playedOrder(score), folder, options,
                               &why, &written),
                 qPrintable(why));

        QCOMPARE(written.size(), 3);        // two tracks and their mix
        QVERIFY(written.at(0).path.endsWith(QStringLiteral("00-Guitar.wav")));
        QVERIFY(written.at(1).path.endsWith(QStringLiteral("01-Drums.wav")));
        QVERIFY(written.at(2).path.endsWith(QStringLiteral("mix.wav")));

        for (const Render::Written &file : written) {
            QVERIFY2(file.peak > 0.001f,
                     qPrintable(file.path + QStringLiteral(" is silent")));
            QVERIFY(file.seconds > 3.5);    // eight quarters at 120, plus the tail
        }
    }

    /**
     * A dry stem is the part before its amplifier, and only where there is one.
     *
     * The claim being tested is identity, not merely difference: the dry file
     * must be what the renderer would have written with no chain at all, or it
     * is not a dry stem, it is a quieter wet one. A part with no chain gets no
     * dry file, because that would be the same samples under two names.
     */
    void writesADryStemBesideAnEffectedOne()
    {
        if (Render::findSoundFont().isEmpty()) {
            QSKIP("no SoundFont on this machine; install fluid-soundfont-gm");
        }
        const QList<Lv2::Description> plugins = Lv2::installed();
        QString uri;
        for (const Lv2::Description &plugin : plugins) {
            if (plugin.name.contains(QStringLiteral("mplifier"))) {
                uri = plugin.uri;
                break;
            }
        }
        if (uri.isEmpty()) {
            QSKIP("no amplifier plugin on this machine");
        }

        const Score score = twoTracks();
        const QList<int> order = Timeline::playedOrder(score);
        Render::Options options;
        options.tailSeconds = 0.5;

        // Once with no chain at all, to have something to be identical to.
        QString why;
        QVERIFY2(Render::stems(score, order, path(QStringLiteral("plain")), options, &why),
                 qPrintable(why));

        options.effects.insert(0, {uri});
        options.dryStems = true;
        QList<Render::Written> written;
        QVERIFY2(Render::stems(score, order, path(QStringLiteral("wetdry")), options, &why,
                               &written),
                 qPrintable(why));

        // Two tracks, one dry copy, and the mix -- and the wet stems keep the
        // names they always had, so the list does not shift under anybody.
        QCOMPARE(written.size(), 4);
        QVERIFY(written.at(0).path.endsWith(QStringLiteral("00-Guitar.wav")));
        QVERIFY(written.at(1).path.endsWith(QStringLiteral("01-Drums.wav")));
        QVERIFY(written.at(2).path.endsWith(QStringLiteral("00-Guitar-dry.wav")));
        QVERIFY(written.at(3).path.endsWith(QStringLiteral("mix.wav")));
        // The part without a chain has no dry file of its own.
        QVERIFY(!QFile::exists(path(QStringLiteral("wetdry"))
                               + QStringLiteral("/01-Drums-dry.wav")));

        const QByteArray dry = contentsOf(path(QStringLiteral("wetdry"))
                                          + QStringLiteral("/00-Guitar-dry.wav"));
        const QByteArray plain = contentsOf(path(QStringLiteral("plain"))
                                            + QStringLiteral("/00-Guitar.wav"));
        const QByteArray wet = contentsOf(path(QStringLiteral("wetdry"))
                                          + QStringLiteral("/00-Guitar.wav"));
        QVERIFY(!dry.isEmpty());
        QCOMPARE(dry, plain);       //< the same performance, before the amplifier
        QVERIFY(dry != wet);        //< and the amplifier did something
    }

    /**
     * The mix must be the sum of the stems, or the stems are not what is being
     * heard and the whole arrangement is decorative.
     */
    void theMixIsTheSumOfTheStems()
    {
        if (Render::findSoundFont().isEmpty()) {
            QSKIP("no SoundFont on this machine; install fluid-soundfont-gm");
        }

        const Score score = twoTracks();
        const QString folder = path(QStringLiteral("summed"));
        Render::Options options;
        options.tailSeconds = 0.5;
        QVERIFY(Render::stems(score, Timeline::playedOrder(score), folder, options));

        const Wav guitar = readWav(QDir(folder).filePath(QStringLiteral("00-Guitar.wav")));
        const Wav drums = readWav(QDir(folder).filePath(QStringLiteral("01-Drums.wav")));
        const Wav mix = readWav(QDir(folder).filePath(QStringLiteral("mix.wav")));

        QCOMPARE(guitar.frames, mix.frames);
        QCOMPARE(drums.frames, mix.frames);

        int worst = 0;
        for (int index = 0; index < mix.samples.size(); index += 37) {
            const int summed = std::clamp(guitar.samples.at(index) + drums.samples.at(index),
                                          -32767, 32767);
            worst = std::max(worst, std::abs(summed - mix.samples.at(index)));
        }
        // Each stem rounds to a whole sample on its own and the mix rounds once,
        // so they may differ by a step or two and no more.
        QVERIFY2(worst <= 4, qPrintable(QStringLiteral("stems differ from the mix by %1")
                                            .arg(worst)));
    }

    void saysSoWhenThereIsNoSoundFont()
    {
        Render::Options options;
        options.soundFont = path(QStringLiteral("absent.sf2"));

        const Score score = twoTracks();
        QString why;
        QVERIFY(!Render::stems(score, Timeline::playedOrder(score),
                               path(QStringLiteral("nowhere")), options, &why));
        QVERIFY(!why.isEmpty());
    }

    void refusesAnEmptyScore()
    {
        QString why;
        QVERIFY(!Render::stems(Score(), {}, path(QStringLiteral("empty")),
                               Render::Options(), &why));
        QVERIFY(!why.isEmpty());
    }
};

QTEST_GUILESS_MAIN(RenderTest)
#include "rendertest.moc"
