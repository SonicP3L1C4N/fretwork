// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "renderer.h"
#include "timeline.h"
#include "wav.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>

#include <fluidsynth.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

namespace
{
/** FluidSynth reserves this one for percussion, as MIDI does. */
constexpr int DrumChannel = 9;

/**
 * One track's synthesiser.
 *
 * Owns a FluidSynth of its own -- which is the expensive-sounding part of the
 * design and the reason it works. Sixteen channels each, so a guitar spends
 * six on its strings and nothing collides; a mixer that wants to solo a track
 * or put an amplifier simulation on it already has the audio separated.
 */
class TrackSynth
{
public:
    TrackSynth(const Track &track, const QList<Timeline::Message> &messages,
          const Timeline::Clock &clock, const Render::Options &options)
        : m_percussion(track.isPercussion())
    {
        m_settings = new_fluid_settings();
        fluid_settings_setnum(m_settings, "synth.sample-rate", options.sampleRate);
        fluid_settings_setnum(m_settings, "synth.gain", options.gain);
        fluid_settings_setint(m_settings, "synth.midi-channels", 16);
        // Nothing here is played live, so there is no thread to be polite to.
        fluid_settings_setint(m_settings, "synth.threadsafe-api", 0);
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

        // Sample positions once, here, so that filling a block is only
        // arithmetic and dispatch.
        m_events.reserve(messages.size());
        for (const Timeline::Message &message : messages) {
            const double seconds = clock.secondsAt(message.at);
            m_events.append({qint64(std::llround(seconds * options.sampleRate)), message});
        }
        std::stable_sort(m_events.begin(), m_events.end(),
                         [](const Event &a, const Event &b) { return a.at < b.at; });
    }

    ~TrackSynth()
    {
        if (m_synth) {
            delete_fluid_synth(m_synth);
        }
        if (m_settings) {
            delete_fluid_settings(m_settings);
        }
    }

    TrackSynth(const TrackSynth &) = delete;
    TrackSynth &operator=(const TrackSynth &) = delete;

    bool isValid() const
    {
        return m_synth && m_font != FLUID_FAILED;
    }

    /**
     * Fill `frames` of audio starting at sample `at`.
     *
     * The whole engine, and deliberately the whole of it: no clock, no
     * transport, no idea whether it is being driven by an audio device or by a
     * loop going as fast as it can. Positions must arrive in order, which both
     * of those do.
     */
    void fill(float *left, float *right, int frames, qint64 at)
    {
        const qint64 until = at + frames;
        while (m_next < m_events.size() && m_events.at(m_next).at < until) {
            dispatch(m_events.at(m_next).message);
            ++m_next;
        }
        fluid_synth_write_float(m_synth, frames, left, 0, 1, right, 0, 1);
    }

    qint64 lastEventSample() const
    {
        return m_events.isEmpty() ? 0 : m_events.last().at;
    }

private:
    struct Event {
        qint64 at = 0;
        Timeline::Message message;
    };

    int channelFor(int trackChannel) const
    {
        return m_percussion ? DrumChannel : std::clamp(trackChannel, 0, 15);
    }

    void dispatch(const Timeline::Message &message)
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

    fluid_settings_t *m_settings = nullptr;
    fluid_synth_t *m_synth = nullptr;
    int m_font = FLUID_FAILED;
    bool m_percussion = false;

    QList<Event> m_events;
    int m_next = 0;
};

QString safeName(const QString &name)
{
    QString out = name;
    out.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9._-]+")), QStringLiteral("_"));
    return out.isEmpty() ? QStringLiteral("track") : out;
}
}

QString Render::findSoundFont()
{
    // The usual places a distribution puts a General MIDI bank. Ordered by how
    // good they sound rather than alphabetically.
    static const QStringList candidates = {
        QStringLiteral("/usr/share/sounds/sf2/FluidR3_GM.sf2"),
        QStringLiteral("/usr/share/sounds/sf2/default-GM.sf2"),
        QStringLiteral("/usr/share/soundfonts/FluidR3_GM.sf2"),
        QStringLiteral("/usr/share/soundfonts/default.sf2"),
    };
    for (const QString &candidate : candidates) {
        if (QFileInfo::exists(candidate)) {
            return candidate;
        }
    }
    return {};
}

bool Render::stems(const Score &score, const QList<int> &order, const QString &directory,
                   const Options &given, QString *error, QList<Written> *written)
{
    Options options = given;
    if (options.soundFont.isEmpty()) {
        options.soundFont = findSoundFont();
    }
    if (options.soundFont.isEmpty()) {
        if (error) {
            *error = QStringLiteral("no SoundFont found: install fluid-soundfont-gm, "
                                    "or name one with --soundfont");
        }
        return false;
    }
    if (score.isEmpty()) {
        if (error) {
            *error = QStringLiteral("there is nothing to render");
        }
        return false;
    }

    QDir folder(directory);
    if (!folder.mkpath(QStringLiteral("."))) {
        if (error) {
            *error = QStringLiteral("cannot make %1").arg(directory);
        }
        return false;
    }

    const Timeline::Clock clock(score, order);

    std::vector<std::unique_ptr<TrackSynth>> voices;
    std::vector<std::unique_ptr<WavWriter>> writers;
    QStringList paths;

    qint64 lastEvent = 0;
    for (int index = 0; index < score.tracks.size(); ++index) {
        const Track &track = score.tracks.at(index);
        auto voice = std::make_unique<TrackSynth>(
            track, Timeline::messagesFor(score, index, order), clock, options);
        if (!voice->isValid()) {
            if (error) {
                *error = QStringLiteral("could not load %1").arg(options.soundFont);
            }
            return false;
        }
        lastEvent = std::max(lastEvent, voice->lastEventSample());

        const QString path = folder.filePath(QStringLiteral("%1-%2.wav")
                                                 .arg(index, 2, 10, QLatin1Char('0'))
                                                 .arg(safeName(track.name)));
        auto writer = std::make_unique<WavWriter>(path, options.sampleRate);
        if (!writer->isOpen()) {
            if (error) {
                *error = QStringLiteral("%1: %2").arg(path, writer->error());
            }
            return false;
        }
        paths.append(path);
        voices.push_back(std::move(voice));
        writers.push_back(std::move(writer));
    }

    const QString mixPath = folder.filePath(QStringLiteral("mix.wav"));
    WavWriter mix(mixPath, options.sampleRate);
    if (!mix.isOpen()) {
        if (error) {
            *error = QStringLiteral("%1: %2").arg(mixPath, mix.error());
        }
        return false;
    }

    // The tail matters: a score does not stop when its last note-off is sent.
    const qint64 total =
        lastEvent + qint64(options.tailSeconds * options.sampleRate);
    const int block = std::max(32, options.blockFrames);

    // Two arguments rather than one: `std::vector<float> left(size_t(block))`
    // declares a function, which is the most vexing parse and compiles until
    // something is asked of it.
    std::vector<float> left(size_t(block), 0.0f);
    std::vector<float> right(size_t(block), 0.0f);
    std::vector<float> mixLeft(size_t(block), 0.0f);
    std::vector<float> mixRight(size_t(block), 0.0f);

    for (qint64 at = 0; at < total; at += block) {
        const int frames = int(std::min<qint64>(block, total - at));
        std::fill_n(mixLeft.begin(), frames, 0.0f);
        std::fill_n(mixRight.begin(), frames, 0.0f);

        for (size_t index = 0; index < voices.size(); ++index) {
            voices[index]->fill(left.data(), right.data(), frames, at);
            if (!writers[index]->write(left.data(), right.data(), frames)) {
                if (error) {
                    *error = writers[index]->error();
                }
                return false;
            }
            for (int frame = 0; frame < frames; ++frame) {
                mixLeft[size_t(frame)] += left[size_t(frame)];
                mixRight[size_t(frame)] += right[size_t(frame)];
            }
        }

        if (!mix.write(mixLeft.data(), mixRight.data(), frames)) {
            if (error) {
                *error = mix.error();
            }
            return false;
        }
    }

    const double seconds = double(total) / options.sampleRate;
    for (size_t index = 0; index < writers.size(); ++index) {
        const float peak = writers[index]->peak();
        if (!writers[index]->close()) {
            if (error) {
                *error = writers[index]->error();
            }
            return false;
        }
        if (written) {
            written->append({paths.at(int(index)), peak, seconds});
        }
    }
    const float mixPeak = mix.peak();
    if (!mix.close()) {
        if (error) {
            *error = mix.error();
        }
        return false;
    }
    if (written) {
        written->append({mixPath, mixPeak, seconds});
    }
    return true;
}
