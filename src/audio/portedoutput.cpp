// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "portedoutput.h"

#include <KLocalizedString>

#ifdef FRETWORK_HAVE_PIPEWIRE
#include <pipewire/filter.h>
#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>
#endif

#include <algorithm>
#include <vector>

namespace
{
/**
 * A port name a graph can live with.
 *
 * Letters, digits and underscores. A port called "Chris \"Whipper\" Layton_FL"
 * is a port every tool that ever prints a graph has to quote, and some of them
 * will not.
 */
QString portName(const QString &track, int index, bool right)
{
    QString safe;
    safe.reserve(track.size());
    for (const QChar letter : track) {
        safe.append(letter.isLetterOrNumber() ? letter : QLatin1Char('_'));
    }
    while (safe.contains(QLatin1String("__"))) {
        safe.replace(QLatin1String("__"), QLatin1String("_"));
    }
    if (safe.isEmpty()) {
        safe = QStringLiteral("track");
    }
    // Numbered as well as named, because two parts in a score may be called
    // the same thing and two ports may not.
    return QStringLiteral("%1_%2_%3").arg(index + 1, 2, 10, QLatin1Char('0'))
        .arg(safe, right ? QStringLiteral("FR") : QStringLiteral("FL"));
}
}

struct PortedOutput::Private {
    PortedOutput::Process process = nullptr;
    void *data = nullptr;
    QString error;
    int pairs = 0;

    /** Handed to the filler for a port nobody has linked. */
    std::vector<float> spare;

    std::vector<float *> left;
    std::vector<float *> right;

#ifdef FRETWORK_HAVE_PIPEWIRE
    pw_thread_loop *loop = nullptr;
    pw_filter *filter = nullptr;
    /** One per port, in the order they were added: left, right, left, right. */
    std::vector<void *> ports;
#endif
};

#ifdef FRETWORK_HAVE_PIPEWIRE
namespace
{
void initialisePipeWire()
{
    static bool once = [] {
        pw_init(nullptr, nullptr);
        return true;
    }();
    Q_UNUSED(once);
}

void onProcess(void *data, struct spa_io_position *position)
{
    auto *self = static_cast<PortedOutput::Private *>(data);
    const int frames = int(position->clock.duration);
    if (frames <= 0 || !self->process) {
        return;
    }

    // Larger than any quantum a graph has ever asked for; a run that asked for
    // more is answered with silence rather than with somebody else's memory.
    if (frames > int(self->spare.size())) {
        return;
    }

    for (int pair = 0; pair < self->pairs; ++pair) {
        auto *toLeft = static_cast<float *>(
            pw_filter_get_dsp_buffer(self->ports[size_t(pair * 2)], uint32_t(frames)));
        auto *toRight = static_cast<float *>(
            pw_filter_get_dsp_buffer(self->ports[size_t(pair * 2 + 1)], uint32_t(frames)));
        // A port nobody has linked hands back nothing. The synth behind it has
        // to run anyway, or a track linked halfway through a piece would play
        // everything it slept through into the block where it was plugged in.
        self->left[size_t(pair)] = toLeft ? toLeft : self->spare.data();
        self->right[size_t(pair)] = toRight ? toRight : self->spare.data();
    }

    self->process(self->data, frames, self->left.data(), self->right.data());
}

const pw_filter_events &filterEvents()
{
    static const pw_filter_events events = [] {
        pw_filter_events filled{};
        filled.version = PW_VERSION_FILTER_EVENTS;
        filled.process = onProcess;
        return filled;
    }();
    return events;
}
}
#endif

PortedOutput::PortedOutput(const Options &options, Process process, void *data)
    : d(std::make_unique<Private>())
{
    d->process = process;
    d->data = data;
    d->pairs = int(options.ports.size());
    if (d->pairs <= 0) {
        d->error = i18n("there are no parts to give ports to");
        return;
    }

#ifndef FRETWORK_HAVE_PIPEWIRE
    d->error = i18n("this copy of Fretwork was built without PipeWire, so it has no ports");
#else
    initialisePipeWire();

    // Room for the largest block a graph is going to ask for, allocated once
    // and never in the callback.
    d->spare.assign(16384, 0.0f);
    d->left.assign(size_t(d->pairs), nullptr);
    d->right.assign(size_t(d->pairs), nullptr);

    d->loop = pw_thread_loop_new("fretwork-ports", nullptr);
    if (!d->loop) {
        d->error = i18n("PipeWire would not start a thread to play on");
        return;
    }

    pw_properties *properties =
        pw_properties_new(PW_KEY_MEDIA_TYPE, "Audio", PW_KEY_MEDIA_CATEGORY, "Playback",
                          PW_KEY_MEDIA_ROLE, "Production", PW_KEY_APP_NAME, "Fretwork",
                          PW_KEY_NODE_NAME, options.name.toUtf8().constData(),
                          PW_KEY_NODE_DESCRIPTION, options.name.toUtf8().constData(),
                          PW_KEY_NODE_AUTOCONNECT, options.autoConnect ? "true" : "false",
                          // Driven whether or not anybody has linked it. A
                          // graph does not schedule a node with nothing
                          // attached, and a transport that would not start
                          // until a DAW had been wired up would look broken
                          // rather than patient -- the whole point of these
                          // ports is that something may plug into them part
                          // way through, and the music has to be playing when
                          // it does.
                          PW_KEY_NODE_ALWAYS_PROCESS, "true",
                          PW_KEY_NODE_WANT_DRIVER, "true", nullptr);

    pw_thread_loop_lock(d->loop);
    d->filter = pw_filter_new_simple(pw_thread_loop_get_loop(d->loop),
                                     options.name.toUtf8().constData(), properties,
                                     &filterEvents(), d.get());
    if (!d->filter) {
        pw_thread_loop_unlock(d->loop);
        d->error = i18n("PipeWire would not open a filter to play through");
        return;
    }

    for (int pair = 0; pair < d->pairs; ++pair) {
        for (const bool right : {false, true}) {
            const QString name = portName(options.ports.at(pair), pair, right);
            void *port = pw_filter_add_port(
                d->filter, PW_DIRECTION_OUTPUT, PW_FILTER_PORT_FLAG_MAP_BUFFERS, 0,
                pw_properties_new(PW_KEY_FORMAT_DSP, "32 bit float mono audio",
                                  PW_KEY_PORT_NAME, name.toUtf8().constData(),
                                  PW_KEY_AUDIO_CHANNEL, right ? "FR" : "FL", nullptr),
                nullptr, 0);
            if (!port) {
                pw_thread_loop_unlock(d->loop);
                d->error = i18n("PipeWire would not make a port called \"%1\"", name);
                return;
            }
            d->ports.push_back(port);
        }
    }

    const int connected = pw_filter_connect(d->filter, PW_FILTER_FLAG_RT_PROCESS, nullptr, 0);
    pw_thread_loop_unlock(d->loop);

    if (connected < 0) {
        d->error = i18n("the ports could not be connected to the graph");
        return;
    }
    if (pw_thread_loop_start(d->loop) < 0) {
        d->error = i18n("the ports would not start");
    }
#endif
}

PortedOutput::~PortedOutput()
{
#ifdef FRETWORK_HAVE_PIPEWIRE
    if (d->loop) {
        pw_thread_loop_stop(d->loop);
    }
    if (d->filter) {
        pw_filter_destroy(d->filter);
    }
    if (d->loop) {
        pw_thread_loop_destroy(d->loop);
    }
#endif
}

bool PortedOutput::isValid() const
{
    return d->error.isEmpty();
}

QString PortedOutput::error() const
{
    return d->error;
}

int PortedOutput::pairCount() const
{
    return d->pairs;
}
