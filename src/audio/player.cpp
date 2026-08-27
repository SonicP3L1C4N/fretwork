// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "player.h"
#include "renderer.h"
#include "sampler.h"
#include "sfz.h"
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
        const QList<Timeline::Message> messages = Timeline::messagesFor(score, index, order);

        const QString sfz = m_options.samplers.value(index);
        if (!sfz.isEmpty()) {
            QString why;
            const Sfz::Instrument instrument = Sfz::read(sfz, &why);
            Sampler::Options samplerOptions;
            samplerOptions.sampleRate = m_options.sampleRate;
            samplerOptions.gain = m_options.gain;
            auto sampler =
                std::make_unique<Sampler>(instrument, messages, clock, samplerOptions);
            if (!sampler->isValid()) {
                m_error = QStringLiteral("%1: %2").arg(
                    sfz, why.isEmpty() ? sampler->error() : why);
                return;
            }
            channel->synth = std::move(sampler);
        } else {
            channel->synth =
                std::make_unique<TrackSynth>(score.tracks.at(index), messages, clock,
                                             synthOptions);
        }
        if (!channel->synth->isValid()) {
            m_error = QStringLiteral("could not load %1").arg(m_options.soundFont);
            return;
        }

        const QStringList chain = m_options.effects.value(index);
        if (!chain.isEmpty()) {
            Lv2::Chain::Options chainOptions;
            chainOptions.sampleRate = m_options.sampleRate;
            chainOptions.maximumFrames = MaximumBlock;
            auto hosted = std::make_unique<Lv2::Chain>(chain, chainOptions);
            if (!hosted->isValid()) {
                m_error = hosted->error();
                return;
            }
            channel->chain = std::move(hosted);
        }
        lastEvent = std::max(lastEvent, channel->synth->lastEventSample());
        m_channels.push_back(std::move(channel));
    }
    // The knobs, once every chain exists to have them.
    for (const Options::Knob &knob : m_options.knobs) {
        if (knob.track < 0 || knob.track >= int(m_channels.size())) {
            continue;
        }
        if (Lv2::Chain *chain = m_channels[size_t(knob.track)]->chain.get()) {
            chain->setControl(knob.stage, knob.symbol, knob.value);
        }
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

        // Following without being able to start it is waiting for a DAW that
        // cannot oblige, so a follower reaches for the transport as well.
        if (m_options.followTransport) {
            m_transport = std::make_unique<JackTransport>();
            if (!m_transport->isValid()) {
                m_transport.reset();
            }
        }
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
    // Following, the transport is the graph's -- so pressing play here starts
    // the graph's, and this program hears about it the same way anything else
    // in the graph does. Setting our own flag as well would be this program
    // rolling while the graph had not agreed to yet.
    if (m_transport) {
        m_transport->start();
        return;
    }
    m_playing.store(true, std::memory_order_release);
}

void Player::pause()
{
    if (m_transport) {
        m_transport->stop();
        return;
    }
    m_playing.store(false, std::memory_order_release);
}

void Player::stop()
{
    if (m_transport) {
        m_transport->stop();
        m_transport->locate(0);
        return;
    }
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
    // The graph's playhead, where there is one to move: everything following
    // it goes to the same place, which is the point of following it.
    if (m_transport) {
        m_transport->locate(sample);
        return;
    }
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

QStringList Player::effectsOn(int track) const
{
    if (track < 0 || track >= int(m_channels.size()) || !m_channels[size_t(track)]->chain) {
        return {};
    }
    return m_channels[size_t(track)]->chain->loaded();
}

QList<Lv2::Stage> Player::chainOn(int track) const
{
    if (track < 0 || track >= int(m_channels.size()) || !m_channels[size_t(track)]->chain) {
        return {};
    }
    return m_channels[size_t(track)]->chain->stages();
}

void Player::setEffectControl(int track, int stage, quint32 index, float value)
{
    if (track < 0 || track >= int(m_channels.size())) {
        return;
    }
    if (Lv2::Chain *chain = m_channels[size_t(track)]->chain.get()) {
        chain->setControl(stage, index, value);
    }
}

Gx::Fitting Player::applyVoicing(int track, int stage, const Gx::Voicing &voicing)
{
    const QList<Lv2::Stage> stages = chainOn(track);
    if (stage < 0 || stage >= stages.size()) {
        return {};
    }

    // Fitted against the knobs this plugin actually reports, so a chain whose
    // second slot is a delay rather than an amplifier simply takes nothing.
    const Gx::Fitting fitting = Gx::fit(voicing, stages.at(stage).controls);
    for (const Gx::Setting &setting : fitting.settings) {
        for (const Lv2::Control &control : stages.at(stage).controls) {
            if (control.symbol == setting.symbol) {
                setEffectControl(track, stage, control.index, setting.value);
                break;
            }
        }
    }
    return fitting;
}

bool Player::isFollowing() const
{
    return m_options.followTransport && m_ports != nullptr;
}

int Player::portLinkCount() const
{
    return m_ports ? m_ports->linkCount() : 0;
}

bool Player::canDriveTransport() const
{
    return m_transport != nullptr;
}

QString Player::transportDriver() const
{
    return m_transport ? m_transport->library() : QString();
}

bool Player::hasGraphTransport() const
{
    return m_graphTransport.load(std::memory_order_relaxed);
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

void Player::portCallback(void *data, int frames, const PortedOutput::Transport &transport,
                          float *const *left, float *const *right)
{
    static_cast<Player *>(data)->spread(frames, transport, left, right);
}

/**
 * Where the graph says the transport is, taken as this program's own.
 *
 * A jump is the case that matters. Somebody dragging the playhead in a DAW
 * moves the position by a lot in one cycle, and a synth whose event cursor
 * stayed where it was would play the wrong part of the piece from then on --
 * so anything that is not the next block along is a seek. Seeking is safe
 * here: it silences and repositions a cursor and allocates nothing, which is
 * the rule everything in this callback lives by.
 */
qint64 Player::followed(const PortedOutput::Transport &transport, int frames)
{
    const qint64 was = m_position.load(std::memory_order_relaxed);
    const qint64 now = transport.at;

    // A block's worth of slack either way, because the graph's idea of a
    // cycle and ours are the same length but need not be exactly in step.
    if (now < was || now > was + qint64(frames) * 2) {
        for (auto &channel : m_channels) {
            channel->synth->seek(now);
        }
        if (m_click) {
            m_click->synth->seek(now);
        }
    }
    m_position.store(now, std::memory_order_release);

    // The graph decides when it is over, not the length of the score: a DAW
    // rolling past the end of a piece is a DAW recording silence on purpose.
    m_playing.store(transport.rolling, std::memory_order_release);
    m_finished.store(false, std::memory_order_release);
    return transport.rolling ? now : -1;
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
void Player::spread(int frames, const PortedOutput::Transport &transport,
                    float *const *left, float *const *right)
{
    const bool following = m_options.followTransport && transport.known;
    m_graphTransport.store(transport.known, std::memory_order_relaxed);

    const qint64 at = following ? followed(transport, frames) : advance(0);
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
        if (channel.chain) {
            channel.chain->process(left[index], right[index], frames);
        }

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

    // Following, the graph moves the transport on and telling it where we
    // think we are would be arguing with it.
    if (!following) {
        advance(frames);
    }
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
            if (channel->chain) {
                // Between the instrument and the fader, which is where an
                // amplifier stands: behind it, the distortion would change
                // every time somebody adjusted a level.
                channel->chain->process(m_scratchLeft.data(), m_scratchRight.data(), block);
            }

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
