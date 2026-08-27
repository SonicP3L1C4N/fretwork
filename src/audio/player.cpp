// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "player.h"
#include "renderer.h"
#include "timeline.h"

#include <fluidsynth.h>

#ifdef FRETWORK_HAVE_PIPEWIRE
#include <pipewire/pipewire.h>
#endif

#include <algorithm>
#include <cmath>

namespace
{
/**
 * Room for a block far larger than any driver asks for.
 *
 * The callback must not allocate, so the scratch buffers are sized once. If a
 * driver ever asks for more than this, the mix is done in several passes
 * rather than resized -- slower, and still silent about it, which is the right
 * order of priorities inside an audio callback.
 */
constexpr int MaximumBlock = 8192;

/** How long to keep rendering past the last event, so the sound can decay. */
constexpr double TailSeconds = 3.0;

/**
 * PipeWire will not start until its own library has been initialised, and
 * FluidSynth does not do it for you.
 *
 * Without this the driver cannot load a single SPA plugin and fails with a
 * message about SPA_PLUGIN_DIR that leads nowhere useful. Calling it costs
 * nothing on a machine not running PipeWire, and once is enough.
 */
void initialisePipeWire()
{
#ifdef FRETWORK_HAVE_PIPEWIRE
    static bool once = [] {
        pw_init(nullptr, nullptr);
        return true;
    }();
    Q_UNUSED(once);
#endif
}

/**
 * Which output to try, in order.
 *
 * Native PipeWire first because that is what this runs on; its PulseAudio shim
 * next, which works and is one translation layer more; ALSA last, which works
 * on anything. A machine with none of them gets an error naming all three
 * rather than the first.
 */
QStringList driversToTry(const QString &requested)
{
    if (!requested.isEmpty()) {
        return {requested};
    }
    return {QStringLiteral("pipewire"), QStringLiteral("pulseaudio"),
            QStringLiteral("alsa"), QStringLiteral("jack")};
}
}

Player::Player(const Score &score, const QList<int> &order, const Options &options)
    : m_options(options)
{
    if (m_options.soundFont.isEmpty()) {
        m_options.soundFont = Render::findSoundFont();
    }
    if (m_options.soundFont.isEmpty()) {
        m_error = QStringLiteral("no SoundFont found: install fluid-soundfont-gm, "
                                 "or name one with --soundfont");
        return;
    }
    if (score.isEmpty()) {
        m_error = QStringLiteral("there is nothing to play");
        return;
    }

    const Timeline::Clock clock(score, order);

    TrackSynth::Options synthOptions;
    synthOptions.soundFont = m_options.soundFont;
    synthOptions.sampleRate = m_options.sampleRate;
    synthOptions.gain = m_options.gain;

    qint64 lastEvent = 0;
    for (int index = 0; index < score.tracks.size(); ++index) {
        auto channel = std::make_unique<Channel>();
        channel->synth = std::make_unique<TrackSynth>(
            score.tracks.at(index), Timeline::messagesFor(score, index, order), clock,
            synthOptions);
        if (!channel->synth->isValid()) {
            m_error = QStringLiteral("could not load %1").arg(m_options.soundFont);
            return;
        }
        lastEvent = std::max(lastEvent, channel->synth->lastEventSample());
        m_channels.push_back(std::move(channel));
    }
    m_length = lastEvent + qint64(TailSeconds * m_options.sampleRate);

    // The click is a track like any other as far as the engine is concerned:
    // a part, a list of messages, and a synth. Nothing here had to learn a new
    // idea to have a metronome, which is the useful thing about building it
    // this way. Its own events do not lengthen the piece -- a beat clicking
    // over the last bar of silence is not a reason for the file to be longer.
    m_click = std::make_unique<Channel>();
    m_click->gain.store(1.0f);
    m_click->synth = std::make_unique<TrackSynth>(
        Timeline::clickTrack(), Timeline::clickFor(score, order), clock, synthOptions);
    if (!m_click->synth->isValid()) {
        m_click.reset();
    }

    m_scratchLeft.assign(size_t(MaximumBlock), 0.0f);
    m_scratchRight.assign(size_t(MaximumBlock), 0.0f);

    initialisePipeWire();

    if (m_options.perTrackPorts) {
        // One node with a pair of ports per part, and the click after them:
        // one callback, one position, one answer to what time it is.
        PortedOutput::Options ports;
        ports.sampleRate = m_options.sampleRate;
        for (const Track &track : score.tracks) {
            ports.ports.append(track.name);
        }
        if (m_click) {
            ports.ports.append(QStringLiteral("Click"));
        }
        m_ports = std::make_unique<PortedOutput>(ports, &Player::portCallback, this);
        if (!m_ports->isValid()) {
            m_error = m_ports->error();
            m_ports.reset();
            return;
        }
        m_driverName = QStringLiteral("pipewire ports");
        return;
    }

    // A settings object of its own: these configure the output device, not a
    // synth, and FluidSynth reads them when the driver is created.
    m_driverSettings = new_fluid_settings();
    fluid_settings_setnum(m_driverSettings, "synth.sample-rate", m_options.sampleRate);
    fluid_settings_setint(m_driverSettings, "audio.period-size", m_options.periodFrames);
    fluid_settings_setint(m_driverSettings, "audio.periods", 2);

    const QStringList candidates = driversToTry(m_options.audioDriver);
    for (const QString &driver : candidates) {
        fluid_settings_setstr(m_driverSettings, "audio.driver", qPrintable(driver));
        m_driver = new_fluid_audio_driver2(m_driverSettings, &Player::fillCallback, this);
        if (m_driver) {
            m_driverName = driver;
            break;
        }
    }
    if (!m_driver) {
        m_error = QStringLiteral("could not open an audio device; tried %1")
                      .arg(candidates.join(QStringLiteral(", ")));
    }
}

Player::~Player()
{
    // The driver first, always: it owns the thread that calls into everything
    // below, and deleting a synth out from under it is the one crash this
    // design can still produce.
    if (m_driver) {
        delete_fluid_audio_driver(m_driver);
    }
    if (m_driverSettings) {
        delete_fluid_settings(m_driverSettings);
    }
}

bool Player::isValid() const
{
    // Either way out counts: with ports there is no FluidSynth driver at all,
    // because the ports are the output rather than something beside it.
    return (m_driver || m_ports) && m_error.isEmpty();
}

QString Player::error() const
{
    return m_error;
}

QString Player::driverName() const
{
    return m_driverName;
}

int Player::trackCount() const
{
    return int(m_channels.size());
}

void Player::play()
{
    if (m_finished.load(std::memory_order_relaxed)) {
        seekSeconds(0);
    }
    m_playing.store(true, std::memory_order_release);
}

void Player::pause()
{
    m_playing.store(false, std::memory_order_release);
}

void Player::stop()
{
    m_playing.store(false, std::memory_order_release);
    seekSeconds(0);
}

bool Player::isPlaying() const
{
    return m_playing.load(std::memory_order_acquire);
}

bool Player::hasFinished() const
{
    return m_finished.load(std::memory_order_acquire);
}

void Player::seekSeconds(double seconds)
{
    const qint64 sample =
        std::clamp<qint64>(qint64(seconds * m_options.sampleRate), 0, m_length);
    // Handed to the audio thread rather than done here: two threads calling
    // into one synth is exactly the race this design exists to avoid.
    m_finished.store(false, std::memory_order_relaxed);
    m_seekTo.store(sample, std::memory_order_release);
}

double Player::positionSeconds() const
{
    return double(m_position.load(std::memory_order_acquire)) / m_options.sampleRate;
}

double Player::lengthSeconds() const
{
    return double(m_length) / m_options.sampleRate;
}

void Player::setMuted(int track, bool muted)
{
    if (track >= 0 && track < trackCount()) {
        m_channels[size_t(track)]->muted.store(muted, std::memory_order_relaxed);
    }
}

void Player::setSolo(int track, bool solo)
{
    if (track < 0 || track >= trackCount()) {
        return;
    }
    const bool was = m_channels[size_t(track)]->solo.exchange(solo, std::memory_order_relaxed);
    if (was != solo) {
        // Counted here rather than scanned in the callback, which would turn
        // every block into a walk over every track to answer one question.
        m_soloCount.fetch_add(solo ? 1 : -1, std::memory_order_relaxed);
    }
}

void Player::setGain(int track, float gain)
{
    if (track >= 0 && track < trackCount()) {
        m_channels[size_t(track)]->gain.store(std::clamp(gain, 0.0f, 4.0f),
                                              std::memory_order_relaxed);
    }
}

bool Player::isMuted(int track) const
{
    return track >= 0 && track < trackCount()
        && m_channels[size_t(track)]->muted.load(std::memory_order_relaxed);
}

bool Player::isSolo(int track) const
{
    return track >= 0 && track < trackCount()
        && m_channels[size_t(track)]->solo.load(std::memory_order_relaxed);
}

float Player::gain(int track) const
{
    if (track < 0 || track >= trackCount()) {
        return 0;
    }
    return m_channels[size_t(track)]->gain.load(std::memory_order_relaxed);
}

void Player::setClickEnabled(bool enabled)
{
    m_clickEnabled.store(enabled, std::memory_order_release);
}

bool Player::isClickEnabled() const
{
    return m_clickEnabled.load(std::memory_order_acquire);
}

void Player::setClickGain(float gain)
{
    if (m_click) {
        m_click->gain.store(std::clamp(gain, 0.0f, 2.0f), std::memory_order_release);
    }
}

float Player::clickGain() const
{
    return m_click ? m_click->gain.load(std::memory_order_acquire) : 0.0f;
}

int Player::portCount() const
{
    return m_ports ? m_ports->pairCount() : 0;
}

bool Player::isAudible(int track) const
{
    if (track < 0 || track >= trackCount()) {
        return false;
    }
    if (m_soloCount.load(std::memory_order_relaxed) > 0) {
        return isSolo(track);
    }
    return !isMuted(track);
}

void Player::portCallback(void *data, int frames, float *const *left, float *const *right)
{
    static_cast<Player *>(data)->spread(frames, left, right);
}

/**
 * A pair of buffers per track, instead of one pair for all of them.
 *
 * The same engine and the same clock as `mix`: one position, advanced once,
 * every synth asked for the same block. What is different is only where the
 * audio lands -- and that a track's own gain is applied to its own port, so
 * what a DAW records is what the mixer says, fader included.
 *
 * Mute and solo still silence a port. A DAW recording a muted track would
 * otherwise get audio the person at the mixer believes they turned off, which
 * is worse than a silent take because it is a surprise later.
 */
void Player::spread(int frames, float *const *left, float *const *right)
{
    const qint64 at = advance(0);
    if (at < 0) {
        // Not playing: the ports still have to be given silence, or they hold
        // whatever the graph left in them and buzz.
        for (int pair = 0; pair < int(m_channels.size()) + (m_ports ? 1 : 0); ++pair) {
            std::fill_n(left[pair], frames, 0.0f);
            std::fill_n(right[pair], frames, 0.0f);
        }
        return;
    }

    const bool soloing = m_soloCount.load(std::memory_order_relaxed) > 0;
    for (size_t index = 0; index < m_channels.size(); ++index) {
        Channel &channel = *m_channels[index];
        channel.synth->fill(left[index], right[index], frames, at);

        const bool audible = soloing ? channel.solo.load(std::memory_order_relaxed)
                                     : !channel.muted.load(std::memory_order_relaxed);
        const float gain =
            audible ? channel.gain.load(std::memory_order_relaxed) : 0.0f;
        if (gain != 1.0f) {
            for (int frame = 0; frame < frames; ++frame) {
                left[index][frame] *= gain;
                right[index][frame] *= gain;
            }
        }
    }

    if (m_click) {
        const size_t index = m_channels.size();
        m_click->synth->fill(left[index], right[index], frames, at);
        const float gain = m_clickEnabled.load(std::memory_order_relaxed)
            ? m_click->gain.load(std::memory_order_relaxed)
            : 0.0f;
        if (gain != 1.0f) {
            for (int frame = 0; frame < frames; ++frame) {
                left[index][frame] *= gain;
                right[index][frame] *= gain;
            }
        }
    }

    advance(frames);
}

/**
 * Where the transport is, and moving it on.
 *
 * Called with zero to ask and with a block to move: the two outputs need the
 * same answer to "what time is it" and the same rule for running off the end,
 * and two copies of that rule would eventually disagree about where a piece
 * stops. Returns -1 where nothing should be played at all.
 */
qint64 Player::advance(int frames)
{
    if (frames == 0) {
        const qint64 seek = m_seekTo.exchange(-1, std::memory_order_acquire);
        if (seek >= 0) {
            for (auto &channel : m_channels) {
                channel->synth->seek(seek);
            }
            if (m_click) {
                m_click->synth->seek(seek);
            }
            m_position.store(seek, std::memory_order_release);
        }
        return m_playing.load(std::memory_order_acquire)
            ? m_position.load(std::memory_order_relaxed)
            : -1;
    }

    const qint64 at = m_position.load(std::memory_order_relaxed) + frames;
    m_position.store(at, std::memory_order_release);
    if (at >= m_length) {
        m_playing.store(false, std::memory_order_release);
        m_finished.store(true, std::memory_order_release);
    }
    return at;
}

int Player::fillCallback(void *data, int frames, int, float *[], int outputCount,
                         float *outputs[])
{
    auto *player = static_cast<Player *>(data);
    if (outputCount < 2) {
        return FLUID_FAILED;
    }
    player->mix(frames, outputs[0], outputs[1]);
    return FLUID_OK;
}

void Player::mix(int frames, float *left, float *right)
{
    // Everything below runs on the audio thread. No allocation, no locks, no
    // logging, and no calls that might do any of those on our behalf.
    const qint64 seek = m_seekTo.exchange(-1, std::memory_order_acquire);
    if (seek >= 0) {
        for (auto &channel : m_channels) {
            channel->synth->seek(seek);
        }
        if (m_click) {
            m_click->synth->seek(seek);
        }
        m_position.store(seek, std::memory_order_release);
    }

    std::fill_n(left, frames, 0.0f);
    std::fill_n(right, frames, 0.0f);

    if (!m_playing.load(std::memory_order_acquire)) {
        return;
    }

    qint64 at = m_position.load(std::memory_order_relaxed);
    const bool soloing = m_soloCount.load(std::memory_order_relaxed) > 0;

    for (int done = 0; done < frames; done += MaximumBlock) {
        const int block = std::min(MaximumBlock, frames - done);

        for (auto &channel : m_channels) {
            // Filled whether or not it is heard, so that unmuting a track does
            // not empty every event it slept through into one block.
            channel->synth->fill(m_scratchLeft.data(), m_scratchRight.data(), block, at);

            const bool audible = soloing ? channel->solo.load(std::memory_order_relaxed)
                                         : !channel->muted.load(std::memory_order_relaxed);
            if (!audible) {
                continue;
            }
            const float gain = channel->gain.load(std::memory_order_relaxed);
            for (int frame = 0; frame < block; ++frame) {
                left[done + frame] += m_scratchLeft[size_t(frame)] * gain;
                right[done + frame] += m_scratchRight[size_t(frame)] * gain;
            }
        }

        if (m_click) {
            // Filled even when it is off, for the same reason the tracks are:
            // a synth that slept through a hundred beats would play all of
            // them into the block where somebody switched it back on.
            m_click->synth->fill(m_scratchLeft.data(), m_scratchRight.data(), block, at);
            // No solo test. A click is not one of the parts and is not
            // silenced by hearing one of them on its own.
            if (m_clickEnabled.load(std::memory_order_relaxed)) {
                const float gain = m_click->gain.load(std::memory_order_relaxed);
                for (int frame = 0; frame < block; ++frame) {
                    left[done + frame] += m_scratchLeft[size_t(frame)] * gain;
                    right[done + frame] += m_scratchRight[size_t(frame)] * gain;
                }
            }
        }
        at += block;
    }

    m_position.store(at, std::memory_order_release);
    if (at >= m_length) {
        m_playing.store(false, std::memory_order_release);
        m_finished.store(true, std::memory_order_release);
    }
}
