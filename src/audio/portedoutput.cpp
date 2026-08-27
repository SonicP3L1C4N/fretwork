// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "portedoutput.h"

#include <KLocalizedString>

#ifdef FRETWORK_HAVE_PIPEWIRE
#include <pipewire/filter.h>
#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>
#include <pipewire/extensions/metadata.h>
#include <spa/utils/string.h>
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
    pw_context *context = nullptr;
    pw_core *core = nullptr;
    pw_registry *registry = nullptr;
    spa_hook registryHook{};
    pw_filter *filter = nullptr;
    /** One per port, in the order they were added: left, right, left, right. */
    std::vector<void *> ports;

    // What the registry has told us, so the links can be made once both ends
    // are known: a sink to plug into, and our own ports to plug in.
    uint32_t ourNode = SPA_ID_INVALID;
    uint32_t sinkNode = SPA_ID_INVALID;

    /**
     * Every audio port the graph has told us about.
     *
     * All of them rather than the ones that matter, because the order things
     * arrive in is not ours to choose: our own node's id is not known until
     * the filter has connected, and its ports are announced either side of
     * that. Keeping everything and deciding later is a few dozen structs and
     * no ordering to get wrong.
     */
    struct SeenPort {
        uint32_t id = 0;
        uint32_t node = 0;
        bool output = false;
        QString channel;
    };
    QList<SeenPort> seenPorts;

    /** Which of our ports are already plugged in, so none is done twice. */
    QSet<uint32_t> linked;

    /**
     * Which sink the desktop actually plays through.
     *
     * The first one the registry offers is not it. A machine set up for music
     * has virtual sinks on it -- this one has two -- and sending the audio to
     * whichever was created first is sending it somewhere nobody is
     * listening. The graph keeps the answer in its metadata and this asks.
     */
    QHash<uint32_t, QString> nodeNames;
    QHash<uint32_t, bool> isSink;
    QString defaultSink;
    pw_metadata *metadata = nullptr;
    spa_hook metadataHook{};

    /**
     * Whether the graph has finished introducing itself.
     *
     * Nothing is linked before this. The registry announces what already
     * exists in whatever order it likes, and the first sink to arrive is
     * rarely the one somebody is listening to -- on this machine it was an
     * HDMI output nothing is plugged into. One round trip and the answer is
     * known, and a round trip is what the core's `done` means.
     */
    bool enumerated = false;
    pw_core *listening = nullptr;
    spa_hook coreHook{};

    int links = 0;
    bool wanted = false;
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

    // Where the graph's transport is, worked out the way PipeWire describes
    // it: running time is the clock less the offset, and the segment maps that
    // onto a position somebody's timeline agrees with.
    PortedOutput::Transport transport;
    transport.rolling = position->state == SPA_IO_POSITION_STATE_RUNNING;
    if (position->n_segments > 0) {
        const spa_io_segment &segment = position->segments[0];
        const bool positioned = (segment.flags & SPA_IO_SEGMENT_FLAG_NO_POSITION) == 0;
        if (positioned) {
            const int64_t running = int64_t(position->clock.position) - position->offset;
            const double along = double(running - int64_t(segment.start)) * segment.rate;
            transport.at = qint64(segment.position) + qint64(along);
            transport.known = transport.at >= 0;
        }
    }

    self->process(self->data, frames, transport, self->left.data(), self->right.data());
}

/**
 * Plugs the node into the first sink the graph offers.
 *
 * Every one of our left ports to its left, every right to its right, so a
 * person pressing play hears the piece summed while a DAW records the parts
 * separately. Many-to-one links are what a mixer is, and the graph does the
 * summing.
 */
void makeLinks(PortedOutput::Private *self)
{
    if (!self->wanted || !self->enumerated || self->ourNode == SPA_ID_INVALID
        || self->sinkNode == SPA_ID_INVALID) {
        return;
    }

    QList<PortedOutput::Private::SeenPort> ours;
    QList<PortedOutput::Private::SeenPort> theirs;
    for (const auto &port : self->seenPorts) {
        if (port.node == self->ourNode && port.output) {
            ours.append(port);
        } else if (port.node == self->sinkNode && !port.output) {
            theirs.append(port);
        }
    }
    if (ours.isEmpty() || theirs.isEmpty()) {
        return;
    }

    // Paired by position, not by name. A sink is not obliged to call its
    // inputs FL and FR: an interface in its professional mode calls them AUX0
    // to AUX3, and matching on the name found nothing at all and linked
    // nothing at all. First to first, second to second, which is what every
    // patching tool does when told to join two nodes.
    std::sort(theirs.begin(), theirs.end(),
              [](const PortedOutput::Private::SeenPort &a,
                 const PortedOutput::Private::SeenPort &b) { return a.id < b.id; });

    // Every port of ours that is not plugged in yet. They are announced as
    // the graph gets round to them, so this runs again on each arrival rather
    // than once when the first one shows up.
    for (const auto &ourPort : ours) {
        if (self->linked.contains(ourPort.id)) {
            continue;
        }
        // Left to the sink's first input, right to its second -- and both to
        // the first where a sink has only one, so a mono output still hears
        // the piece rather than half of it.
        const int side = ourPort.channel == QLatin1String("FR") ? 1 : 0;
        const auto &sinkPort = theirs.at(std::min(side, int(theirs.size()) - 1));

        pw_properties *props = pw_properties_new(nullptr, nullptr);
        pw_properties_setf(props, PW_KEY_LINK_OUTPUT_PORT, "%u", ourPort.id);
        pw_properties_setf(props, PW_KEY_LINK_INPUT_PORT, "%u", sinkPort.id);
        // Not owned by us: a link that outlived the program would be a link
        // pointing at a node that has gone.
        pw_properties_set(props, PW_KEY_OBJECT_LINGER, "false");
        void *made = pw_core_create_object(self->core, "link-factory",
                                           PW_TYPE_INTERFACE_Link, PW_VERSION_LINK,
                                           &props->dict, 0);
        pw_properties_free(props);
        if (made) {
            ++self->links;
            self->linked.insert(ourPort.id);
        }
    }
}

void makeLinks(PortedOutput::Private *self);

/**
 * Settles on a sink: the desktop's own if the graph has said which, else the
 * first one there is, which is better than nothing coming out at all.
 */
void chooseSink(PortedOutput::Private *self)
{
    if (!self->enumerated || self->sinkNode != SPA_ID_INVALID) {
        return;
    }

    // The one the desktop is using, where the graph has said which.
    if (!self->defaultSink.isEmpty()) {
        for (auto entry = self->isSink.constBegin(); entry != self->isSink.constEnd();
             ++entry) {
            if (self->nodeNames.value(entry.key()) == self->defaultSink) {
                self->sinkNode = entry.key();
                makeLinks(self);
                return;
            }
        }
    }

    // Only where there is nothing to ask. A graph with a default metadata
    // object will answer in a moment, and picking the first sink in the
    // meantime lands the audio in an HDMI socket nothing is plugged into --
    // which is what this did before it learned to wait.
    if (self->metadata) {
        return;
    }
    for (auto entry = self->isSink.constBegin(); entry != self->isSink.constEnd(); ++entry) {
        self->sinkNode = entry.key();
        makeLinks(self);
        return;
    }
}

int onMetadata(void *data, uint32_t, const char *key, const char *, const char *value)
{
    auto *self = static_cast<PortedOutput::Private *>(data);
    if (!key || !value || !spa_streq(key, "default.audio.sink")) {
        return 0;
    }
    // The value is a scrap of JSON: {"name":"alsa_output...."}. One key is
    // wanted out of it and a parser for the rest would be a parser for the
    // rest.
    const QString said = QString::fromUtf8(value);
    const int at = said.indexOf(QLatin1String("\"name\""));
    if (at < 0) {
        return 0;
    }
    const int open = said.indexOf(QLatin1Char('"'), said.indexOf(QLatin1Char(':'), at));
    const int close = open < 0 ? -1 : said.indexOf(QLatin1Char('"'), open + 1);
    if (open < 0 || close < 0) {
        return 0;
    }
    self->defaultSink = said.mid(open + 1, close - open - 1);
    chooseSink(self);
    return 0;
}

const pw_metadata_events &metadataEvents()
{
    static const pw_metadata_events events = [] {
        pw_metadata_events filled{};
        filled.version = PW_VERSION_METADATA_EVENTS;
        filled.property = onMetadata;
        return filled;
    }();
    return events;
}

void onCoreDone(void *data, uint32_t id, int)
{
    if (id != PW_ID_CORE) {
        return;
    }
    auto *self = static_cast<PortedOutput::Private *>(data);
    self->enumerated = true;
    chooseSink(self);
}

const pw_core_events &coreEvents()
{
    static const pw_core_events events = [] {
        pw_core_events filled{};
        filled.version = PW_VERSION_CORE_EVENTS;
        filled.done = onCoreDone;
        return filled;
    }();
    return events;
}

void onGlobal(void *data, uint32_t id, uint32_t, const char *type, uint32_t,
              const struct spa_dict *props)
{
    auto *self = static_cast<PortedOutput::Private *>(data);
    if (!props) {
        return;
    }

    if (spa_streq(type, PW_TYPE_INTERFACE_Metadata)) {
        const char *which = spa_dict_lookup(props, PW_KEY_METADATA_NAME);
        if (which && spa_streq(which, "default") && !self->metadata) {
            self->metadata = static_cast<pw_metadata *>(
                pw_registry_bind(self->registry, id, type, PW_VERSION_METADATA, 0));
            if (self->metadata) {
                pw_metadata_add_listener(self->metadata, &self->metadataHook,
                                         &metadataEvents(), self);
            }
        }
        return;
    }

    if (spa_streq(type, PW_TYPE_INTERFACE_Node)) {
        const char *media = spa_dict_lookup(props, PW_KEY_MEDIA_CLASS);
        const char *name = spa_dict_lookup(props, PW_KEY_NODE_NAME);
        if (name) {
            self->nodeNames.insert(id, QString::fromUtf8(name));
        }
        if (media && spa_streq(media, "Audio/Sink")) {
            self->isSink.insert(id, true);
        }
        chooseSink(self);
        return;
    }

    if (!spa_streq(type, PW_TYPE_INTERFACE_Port)) {
        return;
    }
    const char *nodeId = spa_dict_lookup(props, PW_KEY_NODE_ID);
    const char *direction = spa_dict_lookup(props, PW_KEY_PORT_DIRECTION);
    const char *channel = spa_dict_lookup(props, PW_KEY_AUDIO_CHANNEL);
    if (!nodeId || !direction || !channel) {
        return;
    }
    self->seenPorts.append({id, uint32_t(atoi(nodeId)), spa_streq(direction, "out"),
                            QString::fromUtf8(channel)});
    makeLinks(self);
}

const pw_registry_events &registryEvents()
{
    static const pw_registry_events events = [] {
        pw_registry_events filled{};
        filled.version = PW_VERSION_REGISTRY_EVENTS;
        filled.global = onGlobal;
        return filled;
    }();
    return events;
}

/**
 * The node has an id once it exists, and not a moment sooner.
 *
 * Asking `pw_filter_get_node_id` straight after connecting gives nothing --
 * the node is made by the server and announced back. This is where it is
 * known, and therefore where the links become possible.
 */
void onFilterState(void *data, enum pw_filter_state, enum pw_filter_state, const char *)
{
    auto *self = static_cast<PortedOutput::Private *>(data);
    if (self->filter) {
        self->ourNode = pw_filter_get_node_id(self->filter);
        makeLinks(self);
    }
}

const pw_filter_events &filterEvents()
{
    static const pw_filter_events events = [] {
        pw_filter_events filled{};
        filled.version = PW_VERSION_FILTER_EVENTS;
        filled.state_changed = onFilterState;
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
                          PW_KEY_MEDIA_CLASS, "Stream/Output/Audio",
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

    d->context = pw_context_new(pw_thread_loop_get_loop(d->loop), nullptr, 0);
    if (!d->context) {
        d->error = i18n("PipeWire would not make a context to play through");
        return;
    }

    // Started before anything is asked of it. Connecting a core is a
    // conversation with the server, and a conversation held while the loop
    // that would carry it is not running is a deadlock -- which is exactly
    // what this did.
    if (pw_thread_loop_start(d->loop) < 0) {
        d->error = i18n("the ports would not start");
        return;
    }

    pw_thread_loop_lock(d->loop);
    // Our own core rather than the one a simple filter would make for itself:
    // making links means asking the core to make them, and the convenience
    // constructor keeps its core to itself.
    d->core = pw_context_connect(d->context, nullptr, 0);
    if (!d->core) {
        pw_thread_loop_unlock(d->loop);
        d->error = i18n("PipeWire would not connect");
        return;
    }

    d->filter = pw_filter_new(d->core, options.name.toUtf8().constData(), properties);
    if (!d->filter) {
        pw_thread_loop_unlock(d->loop);
        d->error = i18n("PipeWire would not open a filter to play through");
        return;
    }
    static spa_hook filterHook;
    pw_filter_add_listener(d->filter, &filterHook, &filterEvents(), d.get());

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
    d->wanted = options.autoConnect;
    d->ourNode = pw_filter_get_node_id(d->filter);
    if (d->wanted) {
        // Watch the graph until both ends of the links exist. Our own ports
        // appear here too: the node id is known the moment it connects, but
        // the ports arrive as the graph gets round to them.
        d->registry = pw_core_get_registry(d->core, PW_VERSION_REGISTRY, 0);
        pw_registry_add_listener(d->registry, &d->registryHook, &registryEvents(), d.get());

        // Ask when the introductions are over. Everything that exists now is
        // announced before this comes back, so the default sink is known by
        // then rather than raced against.
        d->listening = d->core;
        pw_core_add_listener(d->core, &d->coreHook, &coreEvents(), d.get());
        pw_core_sync(d->core, PW_ID_CORE, 0);
    }
    pw_thread_loop_unlock(d->loop);

    if (connected < 0) {
        d->error = i18n("the ports could not be connected to the graph");
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
    if (d->metadata) {
        pw_proxy_destroy(reinterpret_cast<pw_proxy *>(d->metadata));
    }
    if (d->registry) {
        pw_proxy_destroy(reinterpret_cast<pw_proxy *>(d->registry));
    }
    if (d->core) {
        pw_core_disconnect(d->core);
    }
    if (d->context) {
        pw_context_destroy(d->context);
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

int PortedOutput::linkCount() const
{
#ifdef FRETWORK_HAVE_PIPEWIRE
    return d->links;
#else
    return 0;
#endif
}
