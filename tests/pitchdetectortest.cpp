// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "pitchdetector.h"
#include "tuner.h"

#include <QTest>

#include <cmath>
#include <random>
#include <vector>

/**
 * Hearing a note without a guitar in the room.
 *
 * Every signal here is built in code, for the same reason the rest of the
 * suite builds its scores in code: a test that needed a recording would be a
 * test that only ran on the machine the recording was made on. What cannot be
 * built this way -- whether a real pickup through a real interface is clean
 * enough -- is not something a unit test could have told us anyway.
 *
 * The case that matters is `hearsTheNoteAndNotItsLoudestPartial`. A plucked
 * low string's second harmonic is routinely louder than its fundamental, and a
 * detector that picked the loudest thing present would report the octave above
 * and send somebody to tune a string that was already right.
 */
class PitchDetectorTest : public QObject
{
    Q_OBJECT

private:
    static constexpr double Rate = 48000;
    static constexpr double Pi = 3.14159265358979323846;

    /** A sine, which is the easiest thing in the world to hear. */
    static std::vector<float> sine(double hertz, int frames, double amplitude = 0.3)
    {
        std::vector<float> samples(static_cast<size_t>(frames), 0.0f);
        for (int index = 0; index < frames; ++index) {
            samples[size_t(index)] =
                float(amplitude * std::sin(2 * Pi * hertz * index / Rate));
        }
        return samples;
    }

    /** A string: a fundamental and the partials above it, at chosen weights. */
    static std::vector<float> harmonics(double hertz, const std::vector<double> &weights,
                                        int frames)
    {
        std::vector<float> samples(static_cast<size_t>(frames), 0.0f);
        for (size_t partial = 0; partial < weights.size(); ++partial) {
            const double frequency = hertz * double(partial + 1);
            for (int index = 0; index < frames; ++index) {
                samples[size_t(index)] += float(weights[partial]
                                                * std::sin(2 * Pi * frequency
                                                           * index / Rate));
            }
        }
        return samples;
    }

    static int window()
    {
        return Pitch::windowFor(Pitch::Settings{});
    }

private Q_SLOTS:
    void hearsEveryOpenStringOfAGuitar()
    {
        Pitch::Detector detector;
        for (const Tuner::StringTarget &target : Tuner::standardGuitar()) {
            const std::vector<float> samples = sine(target.hertz, window());
            const Pitch::Detection detection = detector.detect(samples.data(),
                                                               int(samples.size()));
            QVERIFY2(detection.voiced, qPrintable(QStringLiteral("%1 was not heard at all")
                                                      .arg(target.name)));
            const double error = std::abs(Tuner::cents(detection.hertz, target.hertz));
            QVERIFY2(error < 2.0,
                     qPrintable(QStringLiteral("%1: heard %2 Hz for %3 Hz, %4 cents out")
                                    .arg(target.name)
                                    .arg(detection.hertz, 0, 'f', 2)
                                    .arg(target.hertz, 0, 'f', 2)
                                    .arg(error, 0, 'f', 1)));
        }
    }

    void hearsTheEndsOfTheRangeItClaims()
    {
        Pitch::Detector detector;
        const Pitch::Settings settings;
        // A five-string bass's low B, and the top fret of a high E.
        for (const double hertz : {30.87, 1318.5}) {
            const std::vector<float> samples = sine(hertz, window());
            const Pitch::Detection detection = detector.detect(samples.data(),
                                                               int(samples.size()));
            QVERIFY2(detection.voiced,
                     qPrintable(QStringLiteral("%1 Hz is inside the stated range of %2 to %3 "
                                               "and was not heard")
                                    .arg(hertz)
                                    .arg(settings.lowestHertz)
                                    .arg(settings.highestHertz)));
            QVERIFY(std::abs(Tuner::cents(detection.hertz, hertz)) < 5.0);
        }
    }

    void hearsTheNoteAndNotItsLoudestPartial()
    {
        // A low E with a fundamental a third the height of its second
        // harmonic, which is an ordinary bridge pickup rather than a pathology.
        const double fundamental = 82.41;
        const std::vector<float> samples =
            harmonics(fundamental, {0.10, 0.30, 0.18, 0.09, 0.04}, window());

        Pitch::Detector detector;
        const Pitch::Detection detection = detector.detect(samples.data(), int(samples.size()));
        QVERIFY(detection.voiced);
        QVERIFY2(std::abs(Tuner::cents(detection.hertz, fundamental)) < 5.0,
                 qPrintable(QStringLiteral("heard %1 Hz, which is %2 cents from the "
                                           "fundamental at %3 Hz")
                                .arg(detection.hertz, 0, 'f', 2)
                                .arg(Tuner::cents(detection.hertz, fundamental), 0, 'f', 0)
                                .arg(fundamental)));
    }

    void measuresHowFarOutAStringIs()
    {
        // A string thirteen cents flat is the whole job: not which note, but
        // how far, and in which direction.
        const double target = Tuner::hertzForMidi(40);           // E2
        const double flat = target * std::pow(2.0, -13.0 / 1200.0);
        const std::vector<float> samples = harmonics(flat, {0.25, 0.15, 0.08}, window());

        Pitch::Detector detector;
        const Pitch::Detection detection = detector.detect(samples.data(), int(samples.size()));
        QVERIFY(detection.voiced);
        const double measured = Tuner::cents(detection.hertz, target);
        QVERIFY2(std::abs(measured + 13.0) < 2.0,
                 qPrintable(QStringLiteral("measured %1 cents, expected -13")
                                .arg(measured, 0, 'f', 1)));
    }

    void followsAStringBeingTunedUp()
    {
        // What the tuner actually does: the same detector called again and
        // again on a window that slides along, while somebody turns a peg. A
        // detector that is right about one window and jumps an octave on the
        // next is no use for this, and no single-window test would catch it.
        const double target = Tuner::hertzForMidi(45);           // A2
        const int length = int(Rate * 1.5);
        std::vector<float> samples(static_cast<size_t>(length), 0.0f);

        double phase = 0;
        for (int index = 0; index < length; ++index) {
            const double along = double(index) / length;
            const double hertz = target * std::pow(2.0, (-30.0 + 30.0 * along) / 1200.0);
            phase += 2 * Pi * hertz / Rate;
            samples[size_t(index)] = float(0.25 * std::sin(phase) + 0.12 * std::sin(2 * phase));
        }

        Pitch::Detector detector;
        double previous = -1000;
        int readings = 0;
        double last = 0;
        for (int start = 0; start + window() <= length; start += int(Rate * 0.066)) {
            const Pitch::Detection detection = detector.detect(&samples[size_t(start)], window());
            QVERIFY(detection.voiced);
            last = Tuner::cents(detection.hertz, target);
            // Never backwards: a needle that wobbles the wrong way while a peg
            // is being turned one way is a needle nobody can use.
            QVERIFY2(last > previous - 1.0,
                     qPrintable(QStringLiteral("went from %1 to %2 cents")
                                    .arg(previous, 0, 'f', 1)
                                    .arg(last, 0, 'f', 1)));
            previous = last;
            ++readings;
        }
        QVERIFY(readings > 10);
        // It ends where the string ends up: in tune, within a couple of cents.
        QVERIFY2(std::abs(last) < 3.0, qPrintable(QString::number(last)));
    }

    void saysNothingAboutSilence()
    {
        const std::vector<float> samples(size_t(window()), 0.0f);
        Pitch::Detector detector;
        const Pitch::Detection detection = detector.detect(samples.data(), int(samples.size()));
        QVERIFY(!detection.voiced);
        QCOMPARE(detection.hertz, 0.0);
    }

    void saysNothingAboutARoom()
    {
        // Noise is what a tuner hears between notes, and reporting a pitch for
        // it is what makes a tuner look broken.
        std::mt19937 generator(1729);
        std::uniform_real_distribution<float> spread(-0.05f, 0.05f);
        std::vector<float> samples(static_cast<size_t>(window()), 0.0f);
        for (float &sample : samples) {
            sample = spread(generator);
        }

        Pitch::Detector detector;
        const Pitch::Detection detection = detector.detect(samples.data(), int(samples.size()));
        QVERIFY2(!detection.voiced,
                 qPrintable(QStringLiteral("noise was reported as %1 Hz with clarity %2")
                                .arg(detection.hertz, 0, 'f', 1)
                                .arg(detection.clarity, 0, 'f', 2)));
    }

    void wantsAWindowLongEnoughForTheLowestNote()
    {
        const Pitch::Settings settings;
        // Two periods of the lowest note, or the difference function has
        // nothing to compare the second one against. Not exactly two: the lag
        // is a whole number of samples and rounding it down loses a fraction
        // of one, which is a sample and a half at the bottom of the range.
        const double periods = window() / (settings.sampleRate / settings.lowestHertz);
        QVERIFY2(periods > 1.99, qPrintable(QString::number(periods)));

        // Too short a window is answered with silence rather than a guess.
        Pitch::Detector detector;
        const std::vector<float> samples = sine(196.0, 64);
        QVERIFY(!detector.detect(samples.data(), int(samples.size())).voiced);
    }
};

QTEST_GUILESS_MAIN(PitchDetectorTest)
#include "pitchdetectortest.moc"
