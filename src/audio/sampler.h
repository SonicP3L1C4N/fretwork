// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#pragma once

#include "sfz.h"
#include "synth.h"
#include "timeline.h"

#include <QHash>
#include <QString>

#include <memory>
#include <random>
#include <vector>

/**
 * Plays an SFZ instrument: recordings of a guitar instead of a synthesised one.
 *
 * Shaped exactly like `TrackSynth` -- built from a part's messages and a
 * clock, and asked to fill a block of frames at a sample position -- because
 * that is the one rule the engine is built on, and a sampler that needed a
 * different one would need a second renderer and a second player to drive it.
 * A track is one or the other and nothing downstream has to know which.
 *
 * **Round-robins are the point.** A guitar recorded once per note and replayed
 * is a guitar nobody believes, because the same note twice is the identical
 * waveform twice and no instrument does that. A library with four takes of
 * each note played in turn is the cheapest fix there is, and it is the first
 * thing this does with a file.
 *
 * **Linear interpolation, for now.** A note is played by stepping through its
 * recording at a ratio, and between two samples this reads a straight line.
 * That is audibly fine within a few semitones of where a sample was recorded,
 * which is how libraries are built, and audibly not fine an octave away. When
 * it matters the fix is a windowed sinc and a comment about how much slower it
 * is; until a library is in the room to hear it on, the honest thing is to say
 * which one is here.
 */
class Sampler : public Synth
{
public:
    struct Options {
        int sampleRate = 48000;
        /** How many recordings may sound at once before the oldest is dropped. */
        int voices = 64;
        double gain = 1.0;
    };

    Sampler(const Sfz::Instrument &instrument, const QList<Timeline::Message> &messages,
            const Timeline::Clock &clock, const Options &options);
    ~Sampler() override;

    Sampler(const Sampler &) = delete;
    Sampler &operator=(const Sampler &) = delete;

    bool isValid() const override;
    QString error() const;

    /** How many of the instrument's samples actually loaded. */
    int loadedCount() const;

    /** The same contract as `TrackSynth::fill`: a block of frames, in order. */
    void fill(float *left, float *right, int frames, qint64 at) override;

    /** Silences everything and moves the event cursor, allocating nothing. */
    void seek(qint64 sample) override;

    qint64 lastEventSample() const override;

private:
    struct Sound {
        std::vector<float> samples;     //< interleaved
        int channels = 1;
        double rate = 48000;
        qint64 frames = 0;
    };

    struct Playable {
        Sfz::Region region;
        std::shared_ptr<Sound> sound;
        float gainLeft = 1;
        float gainRight = 1;
        double stepScale = 1;           //< the sample's rate against the output's
    };

    struct Voice {
        const Playable *playable = nullptr;
        double position = 0;
        double step = 1;
        float gainLeft = 0;
        float gainRight = 0;
        int key = -1;
        int channel = 0;
        int group = 0;
        bool releasing = false;
        float fade = 1;
        float fadeStep = 0;
        bool active = false;
        qint64 waiting = 0;         //< frames still to wait before it sounds
    };

    struct Event {
        qint64 at = 0;
        Timeline::Message message;
    };

    void dispatch(const Timeline::Message &message);
    void startNote(int channel, int key, int velocity);
    void releaseNote(int channel, int key);

    Options m_options;
    QString m_error;
    int m_loaded = 0;

    std::vector<Playable> m_playables;
    std::vector<Voice> m_voices;

    /** Which take of a round-robin comes next, counted per note. */
    QHash<int, int> m_sequence;

    /**
     * The draw for the other kind of round-robin, seeded the same every time.
     *
     * Deliberately not seeded from the clock. Rendering a score twice has to
     * give the same file twice -- the format tests say so -- and an instrument
     * that chose differently on each pass would make that false without ever
     * looking like the reason.
     */
    std::mt19937 m_random{20260827};

    /** Which articulation is selected, where the library has keyswitches. */
    int m_switch = -1;
    int m_switchLow = 128;
    int m_switchHigh = -1;

    /** Per channel, in cents, so a bend reaches a recording as a step change. */
    std::vector<double> m_bend;
    std::vector<double> m_bendRange;

    std::vector<Event> m_events;
    int m_next = 0;
};
