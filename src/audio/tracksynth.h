// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#pragma once

#include "score.h"
#include "timeline.h"

#include <QList>
#include <QString>

// Declared exactly as fluidsynth/types.h declares them, so that this header
// costs nothing to include and still cannot disagree with the library.
typedef struct _fluid_hashtable_t fluid_settings_t;
typedef struct _fluid_synth_t fluid_synth_t;

/**
 * One track's synthesiser: a FluidSynth of its own, and the events it plays.
 *
 * Shared by the offline renderer and the live player, which is the point of it
 * being here rather than inside either. Both drive it the same way -- fill a
 * block of frames at a sample position -- so a bug in one is a bug in both,
 * and neither can quietly grow a second interpretation of the score.
 *
 * Sixteen channels each, so a guitar spends six on its strings and nothing
 * collides. That is what a synth per track buys, and it is why the sixteen
 * channels of a MIDI file are not a limit here.
 */
class TrackSynth
{
public:
    struct Options {
        QString soundFont;
        int sampleRate = 48000;
        double gain = 0.4;
    };

    /** `events` must be sorted by sample position. */
    TrackSynth(const Track &track, const QList<Timeline::Message> &messages,
               const Timeline::Clock &clock, const Options &options);
    ~TrackSynth();

    TrackSynth(const TrackSynth &) = delete;
    TrackSynth &operator=(const TrackSynth &) = delete;

    bool isValid() const;

    /**
     * Fill `frames` of audio starting at sample `at`.
     *
     * The whole engine, deliberately: no clock, no transport, no idea whether
     * an audio device or a flat-out loop is asking. Positions must arrive in
     * order; seek() is how they stop doing so.
     */
    void fill(float *left, float *right, int frames, qint64 at);

    /**
     * Move to `sample`, silencing whatever was ringing.
     *
     * Safe to call from an audio callback: it silences and repositions a
     * cursor, and allocates nothing. Notes that would have been sounding are
     * not restarted, which is what every sequencer does on a seek.
     */
    void seek(qint64 sample);

    /** The sample the last event falls on, which is where a render may stop. */
    qint64 lastEventSample() const;

private:
    struct Event {
        qint64 at = 0;
        Timeline::Message message;
    };

    int channelFor(int trackChannel) const;
    void dispatch(const Timeline::Message &message);

    fluid_settings_t *m_settings = nullptr;
    fluid_synth_t *m_synth = nullptr;
    int m_font = -1;
    bool m_percussion = false;

    QList<Event> m_events;
    int m_next = 0;
};
