// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "tracksynth.h"

#include <fluidsynth.h>

#include <algorithm>
#include <cmath>

namespace
{
/** FluidSynth reserves this one for percussion, as MIDI does. */
constexpr int DrumChannel = 9;

/**
 * Stop FluidSynth talking about drivers nobody asked for.
 *
 * Creating a settings object probes every audio backend it was built with, and
 * on a machine without SDL initialised that prints a warning -- once per synth,
 * so a four-track score prints it four times before playing a note. Errors are
 * left alone; it is the running commentary that goes.
 */
void quieten()
{
    static bool once = [] {
        fluid_set_log_function(FLUID_WARN, nullptr, nullptr);
        fluid_set_log_function(FLUID_INFO, nullptr, nullptr);
        fluid_set_log_function(FLUID_DBG, nullptr, nullptr);
        return true;
    }();
    Q_UNUSED(once);
}
}

TrackSynth::TrackSynth(const Track &track, const QList<Timeline::Message> &messages,
                       const Timeline::Clock &clock, const Options &options)
    : m_percussion(track.isPercussion())
{
    quieten();
    m_settings = new_fluid_settings();
    fluid_settings_setnum(m_settings, "synth.sample-rate", options.sampleRate);
    fluid_settings_setnum(m_settings, "synth.gain", options.gain);
    fluid_settings_setint(m_settings, "synth.midi-channels", 16);
    m_synth = new_fluid_synth(m_settings);
    if (!m_synth) {
        return;
    }

    m_font = fluid_synth_sfload(m_synth, qPrintable(options.soundFont), 1);
    if (m_font == FLUID_FAILED) {
        return;
    }

    for (int channel = 0; channel < 16; ++channel) {
        if (m_percussion) {
            // Bank 128 is where a SoundFont keeps its kits.
            fluid_synth_bank_select(m_synth, channel, 128);
            fluid_synth_program_change(m_synth, channel, 0);
        } else {
            fluid_synth_program_select(m_synth, channel, m_font, 0, track.program);
        }
    }

    // Sample positions once, here, so that filling a block is only arithmetic
    // and dispatch -- which is what an audio callback is allowed to be.
    m_events.reserve(int(messages.size()));
    for (const Timeline::Message &message : messages) {
        const double seconds = clock.secondsAt(message.at);
        m_events.append({qint64(std::llround(seconds * options.sampleRate)), message});
    }
    std::stable_sort(m_events.begin(), m_events.end(),
                     [](const Event &a, const Event &b) { return a.at < b.at; });
}

TrackSynth::~TrackSynth()
{
    if (m_synth) {
        delete_fluid_synth(m_synth);
    }
    if (m_settings) {
        delete_fluid_settings(m_settings);
    }
}

bool TrackSynth::isValid() const
{
    return m_synth && m_font != FLUID_FAILED;
}

qint64 TrackSynth::lastEventSample() const
{
    return m_events.isEmpty() ? 0 : m_events.constLast().at;
}

int TrackSynth::channelFor(int trackChannel) const
{
    return m_percussion ? DrumChannel : std::clamp(trackChannel, 0, 15);
}

void TrackSynth::dispatch(const Timeline::Message &message)
{
    const int channel = channelFor(message.channel);
    switch (message.kind) {
    case Timeline::MessageKind::NoteOn:
        fluid_synth_noteon(m_synth, channel, message.data1, message.data2);
        break;
    case Timeline::MessageKind::NoteOff:
        fluid_synth_noteoff(m_synth, channel, message.data1);
        break;
    case Timeline::MessageKind::PitchBend:
        fluid_synth_pitch_bend(m_synth, channel, message.data1);
        break;
    case Timeline::MessageKind::BendRange:
        fluid_synth_pitch_wheel_sens(m_synth, channel, message.data1);
        break;
    }
}

void TrackSynth::fill(float *left, float *right, int frames, qint64 at)
{
    const qint64 until = at + frames;
    while (m_next < m_events.size() && m_events.at(m_next).at < until) {
        dispatch(m_events.at(m_next).message);
        ++m_next;
    }
    fluid_synth_write_float(m_synth, frames, left, 0, 1, right, 0, 1);
}

void TrackSynth::seek(qint64 sample)
{
    for (int channel = 0; channel < 16; ++channel) {
        fluid_synth_all_notes_off(m_synth, channel);
        // Any bend left over from where we were would apply to the first note
        // played where we are going.
        fluid_synth_pitch_bend(m_synth, channel, 8192);
    }

    const auto found = std::lower_bound(m_events.constBegin(), m_events.constEnd(), sample,
                                        [](const Event &event, qint64 value) {
                                            return event.at < value;
                                        });
    m_next = int(found - m_events.constBegin());

    // The bend range is set once at the start and would be skipped past by any
    // seek, leaving every bend a sixth of its size for the rest of the piece.
    for (const Event &event : std::as_const(m_events)) {
        if (event.at > sample) {
            break;
        }
        if (event.message.kind == Timeline::MessageKind::BendRange) {
            dispatch(event.message);
        }
    }
}
