// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#pragma once

#include <vector>

/**
 * How high the note is that somebody just played.
 *
 * This is YIN -- de Cheveigné and Kawahara, 2002 -- and it is here rather than
 * a Fourier transform because the question is which *note* was played, not
 * which frequencies are present. A plucked string's loudest partial is very
 * often not its fundamental: the low E of a bass through a pickup can have a
 * second harmonic twice the height of the note anybody would say they heard,
 * and a spectrum peak-picker reports the octave above and is confidently
 * wrong. YIN works in the time domain on periodicity, which is the thing a
 * listener is actually agreeing about.
 *
 * **Monophonic only, and it says so.** One note at a time is a solved problem;
 * a strummed chord is not, and this will report something plausible and
 * useless for one. That limit is the reason the tuner asks for one string at a
 * time rather than pretending to hear six.
 *
 * The cost is a difference function over every lag in range: about five
 * million multiply-adds for a window long enough to hear a low B, which is a
 * few milliseconds and perfectly affordable ten times a second on a worker
 * thread. It would not be affordable in an audio callback, which is why it is
 * not called from one.
 */
namespace Pitch
{
struct Settings {
    double sampleRate = 48000;

    /**
     * The range to look in, which is a guitar's and not a piano's.
     *
     * The bottom is a five-string bass's low B (B0, 30.87 Hz) with room under
     * it for a slack string, and the top is above the 24th fret of a high E
     * (E6, 1318.5 Hz). Narrowing the range is not a nicety: every lag outside
     * it is both work not done and a wrong answer not available.
     */
    double lowestHertz = 28.0;
    double highestHertz = 1400.0;

    /** YIN's absolute threshold. Below this a lag is periodic enough to believe. */
    double threshold = 0.15;

    /** Quieter than this is a room rather than a note. */
    double minimumLevel = 0.004;
};

struct Detection {
    /** True when there was a note to hear at all. Everything else is advisory. */
    bool voiced = false;

    double hertz = 0;
    /** 0 to 1, where 1 is a perfect period. Below about 0.8 is a guess. */
    double clarity = 0;
    /** RMS of the window, so a caller can say "play something" rather than "I don't know". */
    double level = 0;
};

/**
 * How many frames one detection needs.
 *
 * Two periods of the lowest note plus the lag itself: a difference function
 * cannot compare a window with a copy of itself shifted further than the
 * window is long. At 48 kHz and a low B that is about a tenth of a second,
 * which is also roughly how long a person waits before believing a tuner.
 */
int windowFor(const Settings &settings);

/**
 * Holds its own working buffers, because it is called on a timer and an
 * allocation per detection is an allocation per tick for no reason.
 */
class Detector
{
public:
    explicit Detector(const Settings &settings = {});

    const Settings &settings() const { return m_settings; }

    /** The window is read and not kept; nothing here holds a pointer to it. */
    Detection detect(const float *samples, int count);

private:
    Settings m_settings;
    std::vector<double> m_difference;
    std::vector<double> m_normalised;
};
}
