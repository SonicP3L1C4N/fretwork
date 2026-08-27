// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "sampler.h"

#include "wav.h"

#include <QFileInfo>

#include <algorithm>
#include <cmath>

namespace
{
constexpr int Channels = 16;

/** Decibels as a multiplier, which is what a mix actually does with them. */
float amplitudeOf(double decibels)
{
    return float(std::pow(10.0, decibels / 20.0));
}

/**
 * How loud a note struck at `velocity` is.
 *
 * The square of the fraction, which is SFZ's own default and roughly what a
 * player's hand does: the difference between a whisper and a normal note is
 * far larger than the difference between a normal note and a hard one.
 */
float velocityGain(int velocity)
{
    const double fraction = std::clamp(velocity, 0, 127) / 127.0;
    return float(fraction * fraction);
}

/** Left and right multipliers for a pan of -100 to 100, at constant power. */
void panOf(double pan, float *left, float *right)
{
    const double angle = (std::clamp(pan, -100.0, 100.0) + 100.0) / 200.0 * M_PI_2;
    *left = float(std::cos(angle));
    *right = float(std::sin(angle));
}
}

Sampler::Sampler(const Sfz::Instrument &instrument, const QList<Timeline::Message> &messages,
                 const Timeline::Clock &clock, const Options &options)
    : m_options(options)
{
    if (instrument.isEmpty()) {
        m_error = QStringLiteral("that instrument has no regions in it");
        return;
    }

    // One copy of each recording however many regions point at it. A library
    // that used one file for four velocity layers with different offsets would
    // otherwise be read four times and held four times.
    QHash<QString, std::shared_ptr<Sound>> loaded;
    QStringList missing;

    m_playables.reserve(size_t(instrument.regions.size()));
    for (const Sfz::Region &region : instrument.regions) {
        if (region.sample.isEmpty()) {
            continue;
        }
        auto found = loaded.constFind(region.sample);
        if (found == loaded.constEnd()) {
            const WavReader reader(region.sample);
            if (!reader.isValid()) {
                if (missing.size() < 6) {
                    missing.append(QFileInfo(region.sample).fileName());
                }
                loaded.insert(region.sample, nullptr);
                continue;
            }
            auto sound = std::make_shared<Sound>();
            sound->samples = reader.samples();
            sound->channels = reader.channels();
            sound->rate = reader.sampleRate();
            sound->frames = reader.frames();
            found = loaded.insert(region.sample, sound);
            ++m_loaded;
        }
        if (!found.value()) {
            continue;
        }

        Playable playable;
        playable.region = region;
        playable.sound = found.value();
        const float amplitude = amplitudeOf(region.volumeDb);
        panOf(region.pan, &playable.gainLeft, &playable.gainRight);
        playable.gainLeft *= amplitude;
        playable.gainRight *= amplitude;
        // A recording made at one rate and played out at another has to be
        // stepped through faster or slower before any pitching is considered.
        playable.stepScale = playable.sound->rate / std::max(1, m_options.sampleRate);
        m_playables.push_back(playable);
    }

    if (m_playables.empty()) {
        m_error = missing.isEmpty()
            ? QStringLiteral("none of that instrument's regions had a sample")
            : QStringLiteral("none of its samples could be read: %1")
                  .arg(missing.join(QStringLiteral(", ")));
        return;
    }

    m_voices.assign(size_t(std::max(1, m_options.voices)), Voice{});
    m_bend.assign(Channels, 0.0);
    m_bendRange.assign(Channels, 2.0);

    m_events.reserve(size_t(messages.size()));
    for (const Timeline::Message &message : messages) {
        m_events.push_back({qint64(clock.secondsAt(message.at) * m_options.sampleRate),
                            message});
    }
    std::stable_sort(m_events.begin(), m_events.end(),
                     [](const Event &a, const Event &b) { return a.at < b.at; });
}

Sampler::~Sampler() = default;

bool Sampler::isValid() const
{
    return m_error.isEmpty();
}

QString Sampler::error() const
{
    return m_error;
}

int Sampler::loadedCount() const
{
    return m_loaded;
}

qint64 Sampler::lastEventSample() const
{
    return m_events.empty() ? 0 : m_events.back().at;
}

void Sampler::startNote(int channel, int key, int velocity)
{
    // Which take of the round-robin this is. Counted per note, because that is
    // what a listener notices: the same note twice in a row is what gives a
    // sampled instrument away, and two different notes never sounded alike.
    const int turn = m_sequence.value(key, 0);

    bool started = false;
    for (const Playable &playable : m_playables) {
        const Sfz::Region &region = playable.region;
        if (key < region.lowKey || key > region.highKey) {
            continue;
        }
        if (velocity < region.lowVelocity || velocity > region.highVelocity) {
            continue;
        }
        if (region.sequenceLength > 1
            && region.sequencePosition != (turn % region.sequenceLength) + 1) {
            continue;
        }

        // Anything this region turns off, goes. Two notes on one string cannot
        // sound at once, and a library says so with a group that silences
        // itself.
        if (region.offBy != 0) {
            for (Voice &voice : m_voices) {
                if (voice.active && voice.group == region.offBy) {
                    voice.releasing = true;
                    voice.fadeStep = -1.0f / float(std::max(1, m_options.sampleRate) * 0.02);
                }
            }
        }

        Voice *free = nullptr;
        for (Voice &voice : m_voices) {
            if (!voice.active) {
                free = &voice;
                break;
            }
        }
        if (!free) {
            // Every voice busy: the one furthest through its recording is the
            // one least likely to be missed.
            free = &*std::max_element(m_voices.begin(), m_voices.end(),
                                      [](const Voice &a, const Voice &b) {
                                          return a.position < b.position;
                                      });
        }

        const double semitones =
            double(key - region.keyCentre) + double(region.tuneCents) / 100.0;
        *free = Voice{};
        free->playable = &playable;
        free->position = double(std::max<qint64>(0, region.offset));
        free->step = std::pow(2.0, semitones / 12.0) * playable.stepScale;
        const float loudness = velocityGain(velocity) * float(m_options.gain);
        free->gainLeft = playable.gainLeft * loudness;
        free->gainRight = playable.gainRight * loudness;
        free->key = key;
        free->channel = channel;
        free->group = region.group;
        free->fade = 1.0f;
        free->active = true;
        started = true;
    }

    if (started) {
        m_sequence.insert(key, turn + 1);
    }
}

void Sampler::releaseNote(int channel, int key)
{
    for (Voice &voice : m_voices) {
        if (!voice.active || voice.key != key || voice.channel != channel) {
            continue;
        }
        voice.releasing = true;
        const double seconds = std::max(0.005, voice.playable->region.release);
        voice.fadeStep = -1.0f / float(seconds * m_options.sampleRate);
    }
}

void Sampler::dispatch(const Timeline::Message &message)
{
    const int channel = std::clamp(message.channel, 0, Channels - 1);
    switch (message.kind) {
    case Timeline::MessageKind::NoteOn:
        startNote(channel, message.data1, message.data2);
        break;
    case Timeline::MessageKind::NoteOff:
        releaseNote(channel, message.data1);
        break;
    case Timeline::MessageKind::PitchBend:
        // The same fourteen-bit value the MIDI writer sends, turned back into
        // cents against whatever range the channel was told to use.
        m_bend[size_t(channel)] =
            (message.data1 - 8192) / 8192.0 * m_bendRange[size_t(channel)] * 100.0;
        break;
    case Timeline::MessageKind::BendRange:
        m_bendRange[size_t(channel)] = std::max(1, message.data1);
        break;
    }
}

void Sampler::fill(float *left, float *right, int frames, qint64 at)
{
    std::fill_n(left, frames, 0.0f);
    std::fill_n(right, frames, 0.0f);

    const qint64 until = at + frames;
    while (m_next < int(m_events.size()) && m_events[size_t(m_next)].at < until) {
        dispatch(m_events[size_t(m_next)].message);
        ++m_next;
    }

    for (Voice &voice : m_voices) {
        if (!voice.active) {
            continue;
        }
        const Sfz::Region &region = voice.playable->region;
        const Sound &sound = *voice.playable->sound;
        const qint64 last = region.end >= 0 ? std::min(region.end, sound.frames) : sound.frames;
        const qint64 loopEnd = region.loopEnd >= 0 ? std::min(region.loopEnd, last) : last;

        // A bend is a change of step, which is what it is to a recording: the
        // whole thing plays faster or slower rather than being resynthesised.
        const double bend = m_bend[size_t(voice.channel)];
        const double step = voice.step * std::pow(2.0, bend / 1200.0);

        for (int frame = 0; frame < frames; ++frame) {
            if (voice.position >= double(last)) {
                if (region.loops && loopEnd > region.loopStart) {
                    voice.position = double(region.loopStart)
                        + std::fmod(voice.position - double(loopEnd),
                                    double(loopEnd - region.loopStart));
                } else {
                    voice.active = false;
                    break;
                }
            }

            const qint64 index = qint64(voice.position);
            const double between = voice.position - double(index);
            const qint64 after = std::min(index + 1, last - 1);

            // Linear between two samples. Audibly fine within a few semitones
            // of where a recording was made, which is how libraries are built.
            const auto at2 = [&](qint64 frameIndex, int channel) {
                const size_t offset =
                    size_t(frameIndex) * size_t(sound.channels)
                    + size_t(std::min(channel, sound.channels - 1));
                return offset < sound.samples.size() ? sound.samples[offset] : 0.0f;
            };
            const float leftSample =
                float(at2(index, 0) * (1 - between) + at2(after, 0) * between);
            const float rightSample =
                float(at2(index, 1) * (1 - between) + at2(after, 1) * between);

            left[frame] += leftSample * voice.gainLeft * voice.fade;
            right[frame] += rightSample * voice.gainRight * voice.fade;

            voice.position += step;
            if (voice.releasing) {
                voice.fade += voice.fadeStep;
                if (voice.fade <= 0) {
                    voice.active = false;
                    break;
                }
            }
        }
    }
}

void Sampler::seek(qint64 sample)
{
    for (Voice &voice : m_voices) {
        voice.active = false;
    }
    m_sequence.clear();
    const auto found = std::lower_bound(m_events.begin(), m_events.end(), sample,
                                        [](const Event &event, qint64 value) {
                                            return event.at < value;
                                        });
    m_next = int(found - m_events.begin());
}
