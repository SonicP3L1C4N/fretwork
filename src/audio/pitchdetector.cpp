// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "pitchdetector.h"

#include <algorithm>
#include <cmath>

namespace
{
/**
 * The lag a frequency corresponds to, rounded outwards.
 *
 * Rounded outwards on purpose: a range that excluded the very lag the lowest
 * note needs would report the octave above it instead, which is the failure
 * this detector exists to avoid.
 */
int lagFor(double hertz, double sampleRate)
{
    return std::max(2, int(std::floor(sampleRate / std::max(hertz, 1.0))));
}
}

int Pitch::windowFor(const Settings &settings)
{
    // Twice the longest lag: half the window is the piece being compared, and
    // half is the room it has to be shifted through.
    return 2 * lagFor(settings.lowestHertz, settings.sampleRate);
}

Pitch::Detector::Detector(const Settings &settings)
    : m_settings(settings)
{
    const int lags = lagFor(m_settings.lowestHertz, m_settings.sampleRate) + 1;
    m_difference.assign(size_t(lags), 0.0);
    m_normalised.assign(size_t(lags), 0.0);
}

Pitch::Detection Pitch::Detector::detect(const float *samples, int count)
{
    Detection detection;
    if (!samples || count < 4) {
        return detection;
    }

    double sumOfSquares = 0;
    for (int index = 0; index < count; ++index) {
        sumOfSquares += double(samples[index]) * double(samples[index]);
    }
    detection.level = std::sqrt(sumOfSquares / count);

    // Silence is not a wrong answer, it is no answer, and a tuner that spent
    // five million operations arriving at one for an empty room would be
    // running that loop all day for nothing.
    if (detection.level < m_settings.minimumLevel) {
        return detection;
    }

    const int longest = std::min(lagFor(m_settings.lowestHertz, m_settings.sampleRate), count / 2);
    const int shortest = std::min(lagFor(m_settings.highestHertz, m_settings.sampleRate), longest - 1);
    if (shortest < 2 || longest <= shortest) {
        return detection;
    }

    // The window compared is what is left after the longest shift, which is
    // why `windowFor` asks for twice the longest lag rather than one lag.
    const int window = count - longest;
    if (window < longest) {
        return detection;
    }

    if (int(m_difference.size()) <= longest) {
        m_difference.assign(size_t(longest) + 1, 0.0);
        m_normalised.assign(size_t(longest) + 1, 0.0);
    }

    // The difference function: how unlike itself the signal is at each shift.
    m_difference[0] = 0;
    for (int lag = 1; lag <= longest; ++lag) {
        double total = 0;
        for (int index = 0; index < window; ++index) {
            const double delta = double(samples[index]) - double(samples[index + lag]);
            total += delta * delta;
        }
        m_difference[size_t(lag)] = total;
    }

    // The cumulative mean normalisation, which is YIN's own contribution: it
    // divides each lag by the average of every shorter one, so that a lag is
    // judged against the ones that would have been chosen instead. Without it
    // the difference function has a zero at lag 0 and a lower one at every
    // multiple of the period, and the arithmetic prefers the octave.
    m_normalised[0] = 1.0;
    double running = 0;
    for (int lag = 1; lag <= longest; ++lag) {
        running += m_difference[size_t(lag)];
        m_normalised[size_t(lag)] =
            running > 0 ? m_difference[size_t(lag)] * lag / running : 1.0;
    }

    // The first lag good enough, not the best one. A period doubling is always
    // at least as good as the period, so taking the minimum would report an
    // octave down as often as not; taking the first dip under the threshold
    // and then walking to the bottom of it is what makes the answer the note
    // rather than a multiple of it.
    int chosen = -1;
    for (int lag = shortest; lag <= longest; ++lag) {
        if (m_normalised[size_t(lag)] < m_settings.threshold) {
            while (lag + 1 <= longest && m_normalised[size_t(lag + 1)] < m_normalised[size_t(lag)]) {
                ++lag;
            }
            chosen = lag;
            break;
        }
    }

    if (chosen < 0) {
        // Nothing was periodic enough. The best lag is still reported, marked
        // unvoiced, because "I heard something at about 90 Hz and do not
        // believe it" is more use to a caller than a zero.
        chosen = int(std::min_element(m_normalised.begin() + shortest,
                                      m_normalised.begin() + longest + 1)
                     - m_normalised.begin());
    }

    // Parabolic interpolation through the three points around the minimum. A
    // lag is a whole number of samples and a period is not: at 48 kHz the
    // difference between lag 147 and lag 148 near the top of the neck is
    // several cents, which is the whole quantity a tuner is measuring.
    double refined = chosen;
    if (chosen > 0 && chosen < longest) {
        const double before = m_normalised[size_t(chosen - 1)];
        const double at = m_normalised[size_t(chosen)];
        const double after = m_normalised[size_t(chosen + 1)];
        const double divisor = 2 * (2 * at - before - after);
        if (std::abs(divisor) > 1e-12) {
            refined = chosen + (after - before) / divisor;
        }
    }

    detection.hertz = refined > 0 ? m_settings.sampleRate / refined : 0;
    detection.clarity = std::clamp(1.0 - m_normalised[size_t(chosen)], 0.0, 1.0);
    detection.voiced = m_normalised[size_t(chosen)] < m_settings.threshold
        && detection.hertz >= m_settings.lowestHertz
        && detection.hertz <= m_settings.highestHertz;
    return detection;
}
