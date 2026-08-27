// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "sampler.h"
#include "sfz.h"
#include "wav.h"

#include <QDir>
#include <QTemporaryDir>
#include <QTest>

#include <cmath>

/**
 * Playing recordings instead of a synthesiser.
 *
 * There is no sample library to test against and there is not going to be one
 * in a repository, so the recordings here are written by the test: a different
 * steady tone in each file, so that which one came out can be measured rather
 * than listened for. That is enough to answer the questions that matter --
 * which take of a round-robin played, which velocity layer, and at what pitch
 * -- and it is not enough to answer whether it sounds like a guitar, which no
 * test was ever going to answer.
 */
class SamplerTest : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_directory;
    static constexpr int Rate = 48000;

    QString path(const QString &name) const
    {
        return m_directory.path() + QLatin1Char('/') + name;
    }

    /** A second of a steady tone, written where the sampler can find it. */
    void writeTone(const QString &name, double hertz, double amplitude = 0.5) const
    {
        WavWriter writer(path(name), Rate);
        QVERIFY(writer.isOpen());
        std::vector<float> left(Rate), right(Rate);
        for (int frame = 0; frame < Rate; ++frame) {
            const auto value =
                float(amplitude * std::sin(2 * M_PI * hertz * frame / Rate));
            left[size_t(frame)] = value;
            right[size_t(frame)] = value;
        }
        QVERIFY(writer.write(left.data(), right.data(), Rate));
        QVERIFY(writer.close());
    }

    /** The strongest frequency in a block, found by trying candidates. */
    static double toneOf(const float *samples, int frames)
    {
        double best = 0;
        double bestPower = 0;
        for (double hertz = 100; hertz <= 2000; hertz += 0.5) {
            double real = 0;
            double imaginary = 0;
            for (int frame = 0; frame < frames; ++frame) {
                const double angle = 2 * M_PI * hertz * frame / Rate;
                real += samples[frame] * std::cos(angle);
                imaginary += samples[frame] * std::sin(angle);
            }
            const double power = real * real + imaginary * imaginary;
            if (power > bestPower) {
                bestPower = power;
                best = hertz;
            }
        }
        return best;
    }

    static Timeline::Message noteOn(const Rational &at, int key, int velocity)
    {
        return {at, Timeline::MessageKind::NoteOn, 0, key, velocity};
    }

    /** A clock at one beat a second, so a quarter is a second. */
    static Score oneBar()
    {
        Score score;
        Track guitar;
        guitar.instrumentType = QStringLiteral("electricGuitar");
        guitar.tuning = {40, 45, 50, 55, 59, 64};
        score.tracks.append(guitar);
        MasterBar master;
        master.bars = {0};
        score.masterBars.append(master);
        score.bars.insert(0, Bar{{-1, -1, -1, -1}});
        score.tempos.append({0, 0, 60});
        return score;
    }

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(m_directory.isValid());
    }

    void playsTheRecordingItWasGiven()
    {
        writeTone(QStringLiteral("a440.wav"), 440);
        const Sfz::Instrument instrument = Sfz::parse(
            QStringLiteral("<region> sample=a440.wav key=69"), m_directory.path());
        QCOMPARE(instrument.regions.size(), 1);

        const Score score = oneBar();
        const Timeline::Clock clock(score, {0});
        Sampler sampler(instrument, {noteOn(Rational(0), 69, 100)}, clock, {});
        QVERIFY2(sampler.isValid(), qPrintable(sampler.error()));
        QCOMPARE(sampler.loadedCount(), 1);

        std::vector<float> left(4096), right(4096);
        sampler.fill(left.data(), right.data(), 4096, 0);

        // Struck at its own key, so it comes out at the pitch it went in.
        QVERIFY2(std::abs(toneOf(left.data(), 4096) - 440) < 6,
                 qPrintable(QString::number(toneOf(left.data(), 4096))));
    }

    void pitchesARecordingToTheNoteItIsAskedFor()
    {
        writeTone(QStringLiteral("centre.wav"), 440);
        const Sfz::Instrument instrument =
            Sfz::parse(QStringLiteral("<region> sample=centre.wav lokey=60 hikey=96 "
                                      "pitch_keycenter=69"),
                       m_directory.path());

        const Score score = oneBar();
        const Timeline::Clock clock(score, {0});
        // An octave up is twice the frequency, which is the whole of what
        // pitching a recording means.
        Sampler sampler(instrument, {noteOn(Rational(0), 81, 100)}, clock, {});
        QVERIFY(sampler.isValid());

        std::vector<float> left(4096), right(4096);
        sampler.fill(left.data(), right.data(), 4096, 0);
        const double heard = toneOf(left.data(), 4096);
        QVERIFY2(std::abs(heard - 880) < 12, qPrintable(QString::number(heard)));
    }

    void takesTheTurnsOfARoundRobin()
    {
        // Four takes of one note, each a different tone so that which one
        // played can be measured. This is the reason the format is worth
        // reading at all: the same note twice must not be the same waveform
        // twice, and an ear notices that immediately without being able to
        // name it.
        const QList<double> tones = {300, 500, 700, 900};
        QString text = QStringLiteral("<group> key=60 seq_length=4\n");
        for (int take = 0; take < 4; ++take) {
            writeTone(QStringLiteral("take%1.wav").arg(take), tones.at(take));
            text += QStringLiteral("<region> sample=take%1.wav seq_position=%2\n")
                        .arg(take)
                        .arg(take + 1);
        }
        const Sfz::Instrument instrument = Sfz::parse(text, m_directory.path());
        QCOMPARE(instrument.regions.size(), 4);

        // Six strikes a second apart, at sixty to the minute.
        QList<Timeline::Message> messages;
        for (int strike = 0; strike < 6; ++strike) {
            messages.append(noteOn(Rational(strike), 60, 100));
            messages.append({Rational(strike * 2 + 1, 2), Timeline::MessageKind::NoteOff,
                             0, 60, 0});
        }

        const Score score = oneBar();
        const Timeline::Clock clock(score, {0});
        Sampler sampler(instrument, messages, clock, {});
        QVERIFY2(sampler.isValid(), qPrintable(sampler.error()));

        QList<double> heard;
        std::vector<float> left(Rate), right(Rate);
        for (int second = 0; second < 6; ++second) {
            sampler.fill(left.data(), right.data(), 8192, qint64(second) * Rate);
            heard.append(toneOf(left.data(), 8192));
        }

        // Round the four takes in turn, and round again: the fifth strike is
        // the first take once more.
        for (int strike = 0; strike < 6; ++strike) {
            const double wanted = tones.at(strike % 4);
            QVERIFY2(std::abs(heard.at(strike) - wanted) < 8,
                     qPrintable(QStringLiteral("strike %1 sounded %2, wanted %3")
                                    .arg(strike + 1)
                                    .arg(heard.at(strike))
                                    .arg(wanted)));
        }
    }

    void choosesTheVelocityLayer()
    {
        writeTone(QStringLiteral("soft.wav"), 300);
        writeTone(QStringLiteral("hard.wav"), 900);
        const Sfz::Instrument instrument =
            Sfz::parse(QStringLiteral("<group> key=64\n"
                                      "<region> sample=soft.wav lovel=1 hivel=63\n"
                                      "<region> sample=hard.wav lovel=64 hivel=127\n"),
                       m_directory.path());

        const Score score = oneBar();
        const Timeline::Clock clock(score, {0});
        std::vector<float> left(4096), right(4096);

        Sampler quiet(instrument, {noteOn(Rational(0), 64, 30)}, clock, {});
        quiet.fill(left.data(), right.data(), 4096, 0);
        QVERIFY2(std::abs(toneOf(left.data(), 4096) - 300) < 8, "a soft note took the hard layer");

        Sampler loud(instrument, {noteOn(Rational(0), 64, 120)}, clock, {});
        loud.fill(left.data(), right.data(), 4096, 0);
        QVERIFY2(std::abs(toneOf(left.data(), 4096) - 900) < 8, "a hard note took the soft layer");
    }

    void aHarderNoteIsALouderOne()
    {
        writeTone(QStringLiteral("one.wav"), 440);
        const Sfz::Instrument instrument = Sfz::parse(
            QStringLiteral("<region> sample=one.wav key=69"), m_directory.path());
        const Score score = oneBar();
        const Timeline::Clock clock(score, {0});

        const auto peakAt = [&](int velocity) {
            Sampler sampler(instrument, {noteOn(Rational(0), 69, velocity)}, clock, {});
            std::vector<float> left(2048), right(2048);
            sampler.fill(left.data(), right.data(), 2048, 0);
            return *std::max_element(left.begin(), left.end());
        };
        QVERIFY(peakAt(120) > peakAt(60));
        QVERIFY(peakAt(60) > peakAt(20));
    }

    void panPutsARegionWhereItWasTold()
    {
        writeTone(QStringLiteral("wide.wav"), 440);
        const Sfz::Instrument instrument = Sfz::parse(
            QStringLiteral("<region> sample=wide.wav key=69 pan=-100"), m_directory.path());
        const Score score = oneBar();
        const Timeline::Clock clock(score, {0});
        Sampler sampler(instrument, {noteOn(Rational(0), 69, 100)}, clock, {});

        std::vector<float> left(2048), right(2048);
        sampler.fill(left.data(), right.data(), 2048, 0);
        const float loud = *std::max_element(left.begin(), left.end());
        const float quiet = *std::max_element(right.begin(), right.end());
        QVERIFY2(loud > 0.05f, "hard left came out silent on the left");
        QVERIFY2(quiet < loud / 20, "hard left came out of both");
    }

    void saysSoWhenTheSamplesAreNotThere()
    {
        const Sfz::Instrument instrument = Sfz::parse(
            QStringLiteral("<region> sample=nothing_here.wav key=60"), m_directory.path());
        const Score score = oneBar();
        const Timeline::Clock clock(score, {0});
        Sampler sampler(instrument, {}, clock, {});
        QVERIFY(!sampler.isValid());
        QVERIFY2(sampler.error().contains(QLatin1String("nothing_here.wav")),
                 qPrintable(sampler.error()));
    }

    void readsBackWhatTheWriterWrote()
    {
        writeTone(QStringLiteral("round.wav"), 440, 0.25);
        const WavReader reader(path(QStringLiteral("round.wav")));
        QVERIFY2(reader.isValid(), qPrintable(reader.error()));
        QCOMPARE(reader.channels(), 2);
        QCOMPARE(reader.sampleRate(), Rate);
        QCOMPARE(reader.frames(), qint64(Rate));
        QVERIFY(std::abs(*std::max_element(reader.samples().begin(), reader.samples().end())
                         - 0.25f)
                < 0.01f);
    }
};

QTEST_GUILESS_MAIN(SamplerTest)
#include "samplertest.moc"
