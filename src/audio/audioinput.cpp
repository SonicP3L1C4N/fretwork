// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "audioinput.h"

#include <KLocalizedString>

#ifdef FRETWORK_HAVE_PIPEWIRE
#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>
#endif

#include <algorithm>
#include <cstring>

namespace
{
/**
 * A power of two, so that wrapping the ring is a mask rather than a division.
 *
 * Rounded up from whatever was asked for: a ring a little larger than
 * requested costs a few hundred kilobytes and saves the callback an integer
 * divide per block, which is the sort of trade the audio thread always wins.
 */
int ringSizeFor(double seconds, double rate)
{
    const double wanted = std::max(seconds, 0.25) * std::max(rate, 8000.0);
    int size = 1024;
    while (size < wanted && size < (1 << 22)) {
        size *= 2;
    }
    return size;
}
}

struct AudioInput::Private {
    Options options;
    QString error;

    std::vector<float> ring;
    int mask = 0;

    /**
     * Frames written since the stream opened, and the only thing the callback
     * and the reader share.
     *
     * The count is never reset and never wraps in any usable lifetime: at 48
     * kHz a 64-bit frame count lasts about twelve million years, which is long
     * enough to tune a guitar.
     */
    std::atomic<qint64> written{0};
    std::atomic<double> rate{0};
    std::atomic<int> channels{0};
    std::atomic<bool> running{false};

#ifdef FRETWORK_HAVE_PIPEWIRE
    pw_thread_loop *loop = nullptr;
    pw_stream *stream = nullptr;
    spa_hook listener{};
#endif
};

#ifdef FRETWORK_HAVE_PIPEWIRE
namespace
{
/** Once, and before anything else in PipeWire is called. */
void initialisePipeWire()
{
    static bool once = [] {
        pw_init(nullptr, nullptr);
        return true;
    }();
    Q_UNUSED(once);
}

void onStateChanged(void *data, enum pw_stream_state, enum pw_stream_state state,
                    const char *message)
{
    auto *self = static_cast<AudioInput::Private *>(data);
    self->running.store(state == PW_STREAM_STATE_STREAMING, std::memory_order_release);
    if (state == PW_STREAM_STATE_ERROR && self->error.isEmpty()) {
        // Written from the loop's own thread and read from another after the
        // stream has stopped, which is the only moment anybody asks.
        self->error = message ? QString::fromUtf8(message) : i18n("the input stream failed");
    }
}

/**
 * What the graph decided the format is.
 *
 * The stream asks only for 32-bit float and lets PipeWire choose the rate and
 * the channel count, because a capture stream that insisted on 48 kHz mono
 * would refuse to open on an interface running at 44.1 -- and a tuner that
 * will not start on somebody's audio interface is not a tuner.
 */
void onParamChanged(void *data, uint32_t id, const struct spa_pod *param)
{
    auto *self = static_cast<AudioInput::Private *>(data);
    if (!param || id != SPA_PARAM_Format) {
        return;
    }
    uint32_t mediaType = 0;
    uint32_t mediaSubtype = 0;
    if (spa_format_parse(param, &mediaType, &mediaSubtype) < 0
        || mediaType != SPA_MEDIA_TYPE_audio || mediaSubtype != SPA_MEDIA_SUBTYPE_raw) {
        return;
    }
    spa_audio_info_raw info{};
    if (spa_format_audio_raw_parse(param, &info) < 0) {
        return;
    }
    self->rate.store(double(info.rate), std::memory_order_release);
    self->channels.store(int(info.channels), std::memory_order_release);
}

/**
 * The audio thread. Mixes to mono, copies into the ring, advances the count.
 *
 * Mono because pitch is not a stereo property: a guitar through one input of a
 * two-input interface arrives on one channel and silence on the other, and
 * summing is what makes "which input did you plug into" a question nobody has
 * to answer.
 */
void onProcess(void *data)
{
    auto *self = static_cast<AudioInput::Private *>(data);
    pw_buffer *buffer = pw_stream_dequeue_buffer(self->stream);
    if (!buffer) {
        return;
    }

    spa_buffer *spa = buffer->buffer;
    const auto *samples = static_cast<const float *>(spa->datas[0].data);
    const int channels = std::max(1, self->channels.load(std::memory_order_acquire));
    if (samples && spa->datas[0].chunk) {
        const uint32_t offset = spa->datas[0].chunk->offset / sizeof(float);
        const int frames = int(spa->datas[0].chunk->size / sizeof(float)) / channels;
        qint64 written = self->written.load(std::memory_order_relaxed);
        for (int frame = 0; frame < frames; ++frame) {
            float sum = 0;
            for (int channel = 0; channel < channels; ++channel) {
                sum += samples[offset + size_t(frame) * channels + channel];
            }
            self->ring[size_t(written & self->mask)] = sum / channels;
            ++written;
        }
        // Released last, so a reader that sees this count sees the samples
        // that go with it.
        self->written.store(written, std::memory_order_release);
    }

    pw_stream_queue_buffer(self->stream, buffer);
}

const pw_stream_events &streamEvents()
{
    // Assigned field by field rather than with designated initialisers, which
    // this project does not compile new enough a standard to use.
    static const pw_stream_events events = [] {
        pw_stream_events filled{};
        filled.version = PW_VERSION_STREAM_EVENTS;
        filled.state_changed = onStateChanged;
        filled.param_changed = onParamChanged;
        filled.process = onProcess;
        return filled;
    }();
    return events;
}
}
#endif

AudioInput::AudioInput(const Options &options)
    : d(std::make_unique<Private>())
{
    d->options = options;

#ifndef FRETWORK_HAVE_PIPEWIRE
    d->error = i18n("this copy of Fretwork was built without PipeWire, so it cannot listen");
#else
    initialisePipeWire();

    d->ring.assign(size_t(ringSizeFor(options.historySeconds, options.sampleRate)), 0.0f);
    d->mask = int(d->ring.size()) - 1;
    d->rate.store(double(options.sampleRate), std::memory_order_release);
    d->channels.store(1, std::memory_order_release);

    d->loop = pw_thread_loop_new("fretwork-input", nullptr);
    if (!d->loop) {
        d->error = i18n("PipeWire would not start a thread to listen on");
        return;
    }

    pw_properties *properties = pw_properties_new(PW_KEY_MEDIA_TYPE, "Audio",
                                                  PW_KEY_MEDIA_CATEGORY, "Capture",
                                                  PW_KEY_MEDIA_ROLE, "Production",
                                                  PW_KEY_APP_NAME, "Fretwork",
                                                  PW_KEY_NODE_NAME, "Fretwork",
                                                  nullptr);
    if (!options.device.isEmpty()) {
        pw_properties_set(properties, PW_KEY_TARGET_OBJECT,
                          options.device.toUtf8().constData());
    }

    pw_thread_loop_lock(d->loop);
    d->stream = pw_stream_new_simple(pw_thread_loop_get_loop(d->loop), "Fretwork", properties,
                                     &streamEvents(), d.get());
    if (!d->stream) {
        pw_thread_loop_unlock(d->loop);
        d->error = i18n("PipeWire would not open an input stream");
        return;
    }

    uint8_t scratch[1024];
    spa_pod_builder builder = SPA_POD_BUILDER_INIT(scratch, sizeof(scratch));
    spa_audio_info_raw wanted{};
    wanted.format = SPA_AUDIO_FORMAT_F32;
    const spa_pod *params[1] = {
        spa_format_audio_raw_build(&builder, SPA_PARAM_EnumFormat, &wanted),
    };

    const int connected =
        pw_stream_connect(d->stream, PW_DIRECTION_INPUT, PW_ID_ANY,
                          pw_stream_flags(PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS
                                          | PW_STREAM_FLAG_RT_PROCESS),
                          params, 1);
    pw_thread_loop_unlock(d->loop);

    if (connected < 0) {
        d->error = options.device.isEmpty()
            ? i18n("no audio input could be connected to")
            : i18n("no audio input called \"%1\" could be connected to", options.device);
        return;
    }

    if (pw_thread_loop_start(d->loop) < 0) {
        d->error = i18n("the input stream would not start");
    }
#endif
}

AudioInput::~AudioInput()
{
#ifdef FRETWORK_HAVE_PIPEWIRE
    if (d->loop) {
        pw_thread_loop_stop(d->loop);
    }
    if (d->stream) {
        pw_stream_destroy(d->stream);
    }
    if (d->loop) {
        pw_thread_loop_destroy(d->loop);
    }
#endif
}

bool AudioInput::isValid() const
{
    return d->error.isEmpty();
}

QString AudioInput::error() const
{
    return d->error;
}

bool AudioInput::isRunning() const
{
    return d->running.load(std::memory_order_acquire);
}

double AudioInput::sampleRate() const
{
    return d->rate.load(std::memory_order_acquire);
}

int AudioInput::channelCount() const
{
    return d->channels.load(std::memory_order_acquire);
}

qint64 AudioInput::framesCaptured() const
{
    return d->written.load(std::memory_order_acquire);
}

int AudioInput::latest(float *destination, int frames) const
{
    if (!destination || frames <= 0 || d->ring.empty() || frames > int(d->ring.size())) {
        return 0;
    }

    const qint64 end = d->written.load(std::memory_order_acquire);
    if (end < frames) {
        return 0;
    }

    const qint64 start = end - frames;
    for (int index = 0; index < frames; ++index) {
        destination[index] = d->ring[size_t((start + index) & d->mask)];
    }

    // Read the count again: if the callback has come round the ring while this
    // was copying, some of what was copied has been written over. At a second
    // and a half of history and a window of a tenth of one, that means the
    // reader was suspended for a second, and the honest answer is nothing.
    const qint64 now = d->written.load(std::memory_order_acquire);
    if (now - start > qint64(d->ring.size())) {
        return 0;
    }
    return frames;
}
