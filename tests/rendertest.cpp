// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "lv2chain.h"
#include "renderer.h"
#include "timeline.h"
#include "wav.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>
#include <QtEndian>

#include <cmath>
#include <vector>

/**
 * The audio side: the WAV writer and reader on their own, and the renderer
 * against a real SoundFont where the machine has one.
 *
 * The renderer's tests are skipped rather than failed without a SoundFont,
 * because a build machine without fluid-soundfont-gm installed is a missing
 * dependency and not a broken program. The writer and the reader are tested
 * unconditionally -- they need nothing.
 *
 * The reader is tested against files built byte by byte rather than against
 * files the writer produced. The writer only ever emits 16-bit stereo PCM, so
 * a round trip through it proves one of the six shapes the reader claims to
 * accept and none of the ones a sample library actually arrives in.
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

    // ---- building WAV files by hand, for the reader ----

    static void putU16(QByteArray &out, quint16 value)
    {
        char bytes[2];
        qToLittleEndian(value, bytes);
        out.append(bytes, 2);
    }

    static void putU32(QByteArray &out, quint32 value)
    {
        char bytes[4];
        qToLittleEndian(value, bytes);
        out.append(bytes, 4);
    }

    /**
     * A WAV with the header a real one has, and nothing helpful added.
     *
     * `format` is the tag in the fmt chunk: 1 for PCM, 3 for float, 0xFFFE for
     * extensible. For extensible, `subFormat` is the tag buried in the
     * SubFormat GUID, which is where the format actually lives.
     */
    static QByteArray wavBytes(int format, int bits, int channels, int rate,
                               const QByteArray &data, int subFormat = 1,
                               const QByteArray &extraChunk = {})
    {
        QByteArray fmt;
        putU16(fmt, quint16(format));
        putU16(fmt, quint16(channels));
        putU32(fmt, quint32(rate));
        putU32(fmt, quint32(rate * channels * bits / 8));
        putU16(fmt, quint16(channels * bits / 8));
        putU16(fmt, quint16(bits));
        if (format == 0xFFFE) {
            putU16(fmt, 22);                    // cbSize
            putU16(fmt, quint16(bits));         // wValidBitsPerSample
            putU32(fmt, 3);                     // dwChannelMask
            putU16(fmt, quint16(subFormat));    // the GUID's first two bytes
            putU16(fmt, 0);
            // The rest of the GUID every extensible WAV carries unchanged.
            const unsigned char tail[14] = {0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x80,
                                            0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71};
            fmt.append(reinterpret_cast<const char *>(tail), 14);
        }

        QByteArray body;
        body.append("WAVE", 4);
        body.append(extraChunk);
        body.append("fmt ", 4);
        putU32(body, quint32(fmt.size()));
        body.append(fmt);
        body.append("data", 4);
        putU32(body, quint32(data.size()));
        body.append(data);

        QByteArray out;
        out.append("RIFF", 4);
        putU32(out, quint32(body.size()));
        out.append(body);
        return out;
    }

    /** Writes `bytes` to a file in the temporary directory and names it back. */
    QString wavFile(const QString &name, const QByteArray &bytes) const
    {
        const QString where = path(name);
        QFile file(where);
        [&] { QVERIFY(file.open(QIODevice::WriteOnly)); }();
        [&] { QCOMPARE(file.write(bytes), qint64(bytes.size())); }();
        file.close();
        return where;
    }

    /** One 24-bit sample at half scale, little-endian. */
    static QByteArray sample24(qint32 value)
    {
        QByteArray out;
        out.append(char(value & 0xFF));
        out.append(char((value >> 8) & 0xFF));
        out.append(char((value >> 16) & 0xFF));
        return out;
    }

    static float loudest(const std::vector<float> &samples)
    {
        float peak = 0;
        for (const float sample : samples) {
            peak = std::max(peak, std::abs(sample));
        }
        return peak;
    }

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

    // ---- the WAV reader, against files a sample library would hold ----

    /**
     * Every depth and every header shape a sample library arrives in, each
     * read to the right amplitude.
     *
     * Half scale in every case, so a wrong divisor is a wrong number rather
     * than silence, and one row per format so a failure names the format
     * rather than stopping at the first one that breaks.
     *
     * The extensible rows are the ones with teeth. An extensible header names
     * its format only in the SubFormat GUID: the bit depth cannot stand in for
     * it, because 32-bit integer and 32-bit float are both 32 bits and reading
     * one as the other does not fail -- it returns numbers. A sample four
     * times too loud, or a denormal that reads as silence, is the kind of
     * wrong that gets blamed on the sampler. Extensible 24-bit matters on its
     * own account: it is what a professional sample library is usually in.
     */
    void readsEveryShapeASampleArrivesIn_data()
    {
        QTest::addColumn<int>("format");
        QTest::addColumn<int>("bits");
        QTest::addColumn<int>("subFormat");
        QTest::addColumn<QByteArray>("data");

        QByteArray sixteen;
        putU16(sixteen, quint16(qint16(16384)));            // 0.5 of 32768
        QByteArray thirtyTwo;
        putU32(thirtyTwo, 0x40000000u);                     // 0.5 of 2147483648
        QByteArray asFloat;
        const float half = 0.5f;
        asFloat.append(reinterpret_cast<const char *>(&half), 4);
        const QByteArray twentyFour = sample24(0x400000);   // 0.5 of 8388608

        QTest::newRow("16-bit PCM") << 1 << 16 << 1 << sixteen;
        QTest::newRow("24-bit PCM") << 1 << 24 << 1 << twentyFour;
        QTest::newRow("32-bit PCM") << 1 << 32 << 1 << thirtyTwo;
        QTest::newRow("32-bit float") << 3 << 32 << 1 << asFloat;
        QTest::newRow("extensible 16-bit PCM") << 0xFFFE << 16 << 1 << sixteen;
        QTest::newRow("extensible 24-bit PCM") << 0xFFFE << 24 << 1 << twentyFour;
        QTest::newRow("extensible 32-bit PCM") << 0xFFFE << 32 << 1 << thirtyTwo;
        QTest::newRow("extensible 32-bit float") << 0xFFFE << 32 << 3 << asFloat;
    }

    void readsEveryShapeASampleArrivesIn()
    {
        QFETCH(int, format);
        QFETCH(int, bits);
        QFETCH(int, subFormat);
        QFETCH(QByteArray, data);

        const QString file =
            wavFile(QStringLiteral("shape-%1.wav").arg(QLatin1String(QTest::currentDataTag())),
                    wavBytes(format, bits, 1, 44100, data, subFormat));
        const WavReader reader(file);
        QVERIFY2(reader.isValid(), qPrintable(reader.error()));
        QCOMPARE(reader.channels(), 1);
        QCOMPARE(reader.sampleRate(), 44100);
        QCOMPARE(reader.frames(), qint64(1));

        const float peak = loudest(reader.samples());
        QVERIFY2(std::abs(peak - 0.5f) < 0.001f,
                 qPrintable(QStringLiteral("read at %1, not 0.5").arg(double(peak))));
    }

    /** Stereo stays stereo, and the frame count is samples over channels. */
    void countsFramesPerChannelNotPerSample()
    {
        QByteArray data;
        for (int frame = 0; frame < 4; ++frame) {
            putU16(data, quint16(qint16(1000)));    // left
            putU16(data, quint16(qint16(-1000)));   // right
        }
        const WavReader reader(
            wavFile(QStringLiteral("stereo.wav"), wavBytes(1, 16, 2, 48000, data)));
        QVERIFY2(reader.isValid(), qPrintable(reader.error()));
        QCOMPARE(reader.channels(), 2);
        QCOMPARE(reader.frames(), qint64(4));
        QCOMPARE(reader.samples().size(), size_t(8));
    }

    /**
     * A chunk the reader has never heard of, before the one it needs.
     *
     * Editors put LIST and fact chunks wherever they like, and one of them is
     * an odd number of bytes long -- which is padded to even, and a reader
     * that forgets the padding walks into the middle of the next chunk and
     * finds nothing.
     */
    void walksPastChunksItDoesNotKnow()
    {
        QByteArray odd;
        odd.append("LIST", 4);
        putU32(odd, 5);
        odd.append("INFO!", 5);
        odd.append('\0');                           // the pad byte

        QByteArray data;
        putU16(data, quint16(qint16(16384)));
        const WavReader reader(wavFile(QStringLiteral("chunky.wav"),
                                       wavBytes(1, 16, 1, 44100, data, 1, odd)));
        QVERIFY2(reader.isValid(), qPrintable(reader.error()));
        QCOMPARE(reader.frames(), qint64(1));
        QVERIFY(std::abs(loudest(reader.samples()) - 0.5f) < 0.001f);
    }

    /**
     * A data chunk that claims more than the file holds.
     *
     * Truncated downloads are ordinary, and the header still says how long the
     * file was meant to be. Reading to the length in the header rather than to
     * the length on disk is a read past the end of the buffer.
     */
    void readsNoFurtherThanTheFileGoes()
    {
        QByteArray data;
        for (int frame = 0; frame < 8; ++frame) {
            putU16(data, quint16(qint16(1000)));
        }
        QByteArray bytes = wavBytes(1, 16, 1, 44100, data);
        bytes.chop(10);                             // five frames short

        const WavReader reader(wavFile(QStringLiteral("cut.wav"), bytes));
        QVERIFY2(reader.isValid(), qPrintable(reader.error()));
        QCOMPARE(reader.frames(), qint64(3));
    }

    /** Refused by name, so a missing sample is not a silent one. */
    void refusesFormatsItCannotReadByName()
    {
        QByteArray data(64, '\0');

        const struct {
            const char *what;
            int format;
            int bits;
            int channels;
        } cases[] = {
            {"ADPCM", 2, 4, 1},
            {"8-bit PCM", 1, 8, 1},
            {"five channels", 1, 16, 5},
        };

        for (const auto &one : cases) {
            const WavReader reader(
                wavFile(QStringLiteral("bad-%1.wav").arg(QLatin1String(one.what)),
                        wavBytes(one.format, one.bits, one.channels, 44100, data)));
            QVERIFY2(!reader.isValid(), one.what);
            QVERIFY2(!reader.error().isEmpty(), one.what);
        }
    }

    /**
     * An extensible header whose GUID cannot be read is refused, not guessed
     * at. Guessing is what produced the wrong amplitude this suite exists to
     * catch.
     */
    void refusesAnExtensibleHeaderItCannotResolve()
    {
        QByteArray data(64, '\0');
        QByteArray bytes = wavBytes(0xFFFE, 32, 1, 44100, data, 1);
        // Cut the fmt chunk back to the plain sixteen bytes, leaving the tag
        // saying "extensible" with no GUID behind it.
        const int at = bytes.indexOf("fmt ") + 4;
        QByteArray shortened = bytes.left(at);
        putU32(shortened, 16);
        shortened.append(bytes.mid(at + 4, 16));
        shortened.append(bytes.mid(bytes.indexOf("data")));

        const WavReader reader(wavFile(QStringLiteral("halfext.wav"), shortened));
        QVERIFY(!reader.isValid());
        QVERIFY2(!reader.error().isEmpty(), qPrintable(reader.error()));
    }

    void saysWhichFileItCouldNotOpen()
    {
        const WavReader reader(path(QStringLiteral("no-such-sample.wav")));
        QVERIFY(!reader.isValid());
        QVERIFY2(reader.error().contains(QStringLiteral("no-such-sample.wav")),
                 qPrintable(reader.error()));
    }

    void refusesSomethingThatIsNotAWavAtAll()
    {
        const WavReader reader(
            wavFile(QStringLiteral("prose.wav"),
                    QByteArrayLiteral("This is not a WAV file, it is a sentence.")));
        QVERIFY(!reader.isValid());
        QVERIFY(!reader.error().isEmpty());
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

    /**
     * The click is written as a stem and kept out of the mix.
     *
     * Both halves matter and the second is the one that can break quietly. A
     * click baked into the mix cannot be taken out again by whoever opens
     * these files later, and it would sound like a deliberate part rather than
     * like a mistake -- so the test is that the mix is still exactly the two
     * instruments, with a click file sitting beside it.
     */
    void writesTheClickAsAStemAndKeepsItOutOfTheMix()
    {
        if (Render::findSoundFont().isEmpty()) {
            QSKIP("no SoundFont on this machine; install fluid-soundfont-gm");
        }

        const Score score = twoTracks();
        const QString folder = path(QStringLiteral("clicked"));
        Render::Options options;
        options.tailSeconds = 0.5;
        options.click = true;

        QString why;
        QList<Render::Written> written;
        QVERIFY2(Render::stems(score, Timeline::playedOrder(score), folder, options,
                               &why, &written),
                 qPrintable(why));

        QVERIFY(QFileInfo::exists(QDir(folder).filePath(QStringLiteral("click.wav"))));
        const Wav click = readWav(QDir(folder).filePath(QStringLiteral("click.wav")));
        QVERIFY2(click.frames > 0, "the click stem is empty");

        const Wav guitar = readWav(QDir(folder).filePath(QStringLiteral("00-Guitar.wav")));
        const Wav drums = readWav(QDir(folder).filePath(QStringLiteral("01-Drums.wav")));
        const Wav mix = readWav(QDir(folder).filePath(QStringLiteral("mix.wav")));

        int worst = 0;
        for (int index = 0; index < mix.samples.size(); index += 37) {
            const int summed = std::clamp(guitar.samples.at(index) + drums.samples.at(index),
                                          -32767, 32767);
            worst = std::max(worst, std::abs(summed - mix.samples.at(index)));
        }
        QVERIFY2(worst <= 4,
                 qPrintable(QStringLiteral("the click reached the mix: off by %1")
                                .arg(worst)));
    }

    /**
     * The tail is what is asked for, and it is there because a file that stops
     * on the last note-off ends with a click of its own.
     */
    void keepsRenderingForAsLongAsTheTailAsksFor()
    {
        if (Render::findSoundFont().isEmpty()) {
            QSKIP("no SoundFont on this machine; install fluid-soundfont-gm");
        }

        const Score score = twoTracks();
        Render::Options brief;
        brief.tailSeconds = 0.5;
        Render::Options longer = brief;
        longer.tailSeconds = 2.0;

        QList<Render::Written> shortRun;
        QList<Render::Written> longRun;
        QVERIFY(Render::stems(score, Timeline::playedOrder(score),
                              path(QStringLiteral("tail-short")), brief, nullptr,
                              &shortRun));
        QVERIFY(Render::stems(score, Timeline::playedOrder(score),
                              path(QStringLiteral("tail-long")), longer, nullptr,
                              &longRun));

        QVERIFY(!shortRun.isEmpty() && !longRun.isEmpty());
        const double difference = longRun.constFirst().seconds - shortRun.constFirst().seconds;
        QVERIFY2(std::abs(difference - 1.5) < 0.05,
                 qPrintable(QStringLiteral("the extra tail was %1 seconds, not 1.5")
                                .arg(difference)));
    }

    /**
     * A named SoundFont that is not there. Not the same case as none being
     * installed at all: naming one skips the search entirely, so this is the
     * "could not load" branch and the message has to name the file, since the
     * path the user typed is the thing they got wrong.
     */
    void saysWhichSoundFontItCouldNotLoad()
    {
        Render::Options options;
        options.soundFont = path(QStringLiteral("absent.sf2"));

        const Score score = twoTracks();
        QString why;
        QVERIFY(!Render::stems(score, Timeline::playedOrder(score),
                               path(QStringLiteral("nowhere")), options, &why));
        QVERIFY2(why.contains(QStringLiteral("absent.sf2")), qPrintable(why));
    }

    /**
     * The search only ever offers a file that is there.
     *
     * The branch where it finds nothing -- the one a stranger with no
     * fluid-soundfont-gm actually meets -- cannot be reached on a machine that
     * has a SoundFont installed, because the candidates are fixed paths. This
     * asserts the half that is reachable either way.
     */
    void findsOnlyASoundFontThatExists()
    {
        const QString found = Render::findSoundFont();
        if (found.isEmpty()) {
            QSKIP("no SoundFont on this machine: the search has nothing to offer");
        }
        QVERIFY2(QFileInfo::exists(found), qPrintable(found));
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
