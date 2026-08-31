// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "midiinput.h"

#include <KLocalizedString>

#ifdef FRETWORK_HAVE_PIPEWIRE
#include <pipewire/pipewire.h>
#include <spa/control/control.h>
#include <spa/control/ump-utils.h>
#include <spa/pod/iter.h>
#include <spa/utils/string.h>
#endif

#include <QHash>

#include <atomic>
#include <array>
#include <cstdint>

namespace
{
/**
 * How many messages are kept between one drain and the next.
 *
 * A power of two so the ring wraps with a mask. Two hundred and fifty six is
 * about four seconds of a knob being swept at the rate a controller sends,
 * which is far more than any caller polling at frame rate will ever be behind
 * by -- and if one is that far behind, the oldest are the ones worth losing.
 */
constexpr int Capacity = 256;
}

struct MidiInput::Private {
    Options options;
    QString error;

    /**
     * A single-producer, single-consumer ring.
     *
     * The graph's thread writes and the reader reads, and neither waits for
     * the other. `written` only ever grows; `read` only ever grows; the
     * difference is what is waiting. Both are counts rather than indices so
     * that "how far behind is the reader" is a subtraction rather than a case
     * analysis about which way round they are.
     */
    std::array<Event, Capacity> ring{};
    std::atomic<qint64> written{0};
    std::atomic<qint64> read{0};
    std::atomic<bool> running{false};

    void push(const Event &event)
    {
        const qint64 at = written.load(std::memory_order_relaxed);
        ring[size_t(at % Capacity)] = event;
        written.store(at + 1, std::memory_order_release);
    }

#ifdef FRETWORK_HAVE_PIPEWIRE
    pw_thread_loop *loop = nullptr;
    pw_context *context = nullptr;
    pw_core *core = nullptr;
    pw_stream *stream = nullptr;
    spa_hook listener{};

    /**
     * The graph is watched so that the named port can be linked to.
     *
     * Autoconnect does not do it. A MIDI capture stream is connected to
     * nothing by the session manager, and `target.object` names a *node*,
     * while what somebody wants to listen to is a port: a controller is four
     * of them on one node, and `Minilab3 MCU/HUI` is a different feature from
     * `Minilab3 MIDI`. So this does what PortedOutput does at the other end of
     * the graph, and for the same stated reason -- ourselves, because nothing
     * else will.
     */
    struct SeenPort {
        uint32_t id = 0;
        uint32_t node = 0;
        QString name;
        bool input = false;
    };
    pw_registry *registry = nullptr;
    spa_hook registryHook{};
    spa_hook coreHook{};
    QHash<uint32_t, QString> nodeNames;
    /**
     * Every port the graph has announced, whosever it is.
     *
     * Kept in one list and sorted out later rather than as it arrives, because
     * "which of these is ours" cannot be answered when they arrive: a stream's
     * node id is not assigned until it has connected, and its ports are
     * announced around the same moment. Matching on the node's *name* once
     * both are known is the answer that does not race.
     */
    QList<SeenPort> ports;
    bool enumerated = false;
    bool linked = false;
#endif
};

namespace
{
#ifdef FRETWORK_HAVE_PIPEWIRE
void initialisePipeWire()
{
    static bool once = [] {
        pw_init(nullptr, nullptr);
        return true;
    }();
    Q_UNUSED(once);
}

/** One MIDI 1.0 message, as the rest of the program wants it. */
MidiInput::Event fromBytes(const uint8_t *bytes, int size)
{
    MidiInput::Event event;
    if (size < 2) {
        return event;
    }
    event.channel = bytes[0] & 0x0f;
    event.data1 = bytes[1] & 0x7f;
    event.data2 = size > 2 ? (bytes[2] & 0x7f) : 0;

    switch (bytes[0] & 0xf0) {
    case 0x80:
        event.kind = MidiInput::Event::Kind::NoteOff;
        break;
    case 0x90:
        // Nought velocity is a note off, which is how most controllers say one
        // and how every parser that does not know it gets stuck notes.
        event.kind = event.data2 == 0 ? MidiInput::Event::Kind::NoteOff
                                      : MidiInput::Event::Kind::NoteOn;
        break;
    case 0xb0:
        event.kind = MidiInput::Event::Kind::ControlChange;
        break;
    case 0xe0:
        event.kind = MidiInput::Event::Kind::PitchBend;
        break;
    default:
        event.kind = MidiInput::Event::Kind::Other;
        break;
    }
    return event;
}

/**
 * Link our input to the port that was asked for, once both ends exist.
 *
 * Run again on every announcement, because the two ends arrive in whatever
 * order the graph likes and neither is worth waiting on in particular.
 */
void linkWanted(MidiInput::Private *self)
{
    if (self->linked || !self->enumerated || self->options.device.isEmpty()) {
        return;
    }

    // PipeWire's own tools print a port as "node:port (capture)", and that is
    // the string somebody has in front of them when they name one. The suffix
    // says which direction it is and is not part of what it is called, so it
    // is dropped from both sides rather than demanded or forbidden.
    const auto bare = [](QString name) {
        for (const QLatin1String suffix : {QLatin1String(" (capture)"), QLatin1String(" (playback)")}) {
            if (name.endsWith(suffix)) {
                name.chop(suffix.size());
            }
        }
        return name;
    };
    const auto fullName = [self, bare](const MidiInput::Private::SeenPort &port) {
        const QString node = self->nodeNames.value(port.node);
        return bare(node.isEmpty() ? port.name : node + QLatin1Char(':') + port.name);
    };
    const QString wanted = bare(self->options.device);

    // Ours is the input port on the node we asked to be called.
    uint32_t mine = 0;
    for (const auto &port : std::as_const(self->ports)) {
        if (port.input && self->nodeNames.value(port.node) == self->options.name) {
            mine = port.id;
            break;
        }
    }
    if (mine == 0) {
        return;
    }

    for (const auto &port : std::as_const(self->ports)) {
        // Matched on the full "node:port" the patching tools print, because
        // that is the name somebody has in front of them, and on the port's
        // own name alone for anyone who typed the short one.
        if (port.input || (fullName(port) != wanted && bare(port.name) != wanted)) {
            continue;
        }

        pw_properties *props = pw_properties_new(nullptr, nullptr);
        pw_properties_setf(props, PW_KEY_LINK_OUTPUT_PORT, "%u", port.id);
        pw_properties_setf(props, PW_KEY_LINK_INPUT_PORT, "%u", mine);
        // A link that outlived the program would point at a node that has gone.
        pw_properties_set(props, PW_KEY_OBJECT_LINGER, "false");
        void *made = pw_core_create_object(self->core, "link-factory", PW_TYPE_INTERFACE_Link,
                                           PW_VERSION_LINK, &props->dict, 0);
        pw_properties_free(props);
        if (made) {
            self->linked = true;
        }
        return;
    }
}

void onGlobal(void *data, uint32_t id, uint32_t, const char *type, uint32_t,
              const struct spa_dict *props)
{
    auto *self = static_cast<MidiInput::Private *>(data);
    if (!props) {
        return;
    }
    if (spa_streq(type, PW_TYPE_INTERFACE_Node)) {
        if (const char *name = spa_dict_lookup(props, PW_KEY_NODE_NAME)) {
            self->nodeNames.insert(id, QString::fromUtf8(name));
        }
        linkWanted(self);
        return;
    }
    if (!spa_streq(type, PW_TYPE_INTERFACE_Port)) {
        return;
    }

    const char *name = spa_dict_lookup(props, PW_KEY_PORT_NAME);
    const char *direction = spa_dict_lookup(props, PW_KEY_PORT_DIRECTION);
    const char *node = spa_dict_lookup(props, PW_KEY_NODE_ID);
    if (!name || !direction || !node) {
        return;
    }
    MidiInput::Private::SeenPort port;
    port.id = id;
    port.node = uint32_t(atoi(node));
    port.name = QString::fromUtf8(name);
    port.input = spa_streq(direction, "in");
    self->ports.append(port);

    linkWanted(self);
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

void onCoreDone(void *data, uint32_t id, int)
{
    if (id != PW_ID_CORE) {
        return;
    }
    auto *self = static_cast<MidiInput::Private *>(data);
    self->enumerated = true;
    linkWanted(self);
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

void onStateChanged(void *data, enum pw_stream_state, enum pw_stream_state state, const char *)
{
    auto *d = static_cast<MidiInput::Private *>(data);
    d->running.store(state == PW_STREAM_STATE_STREAMING, std::memory_order_release);
}

void onProcess(void *data)
{
    auto *d = static_cast<MidiInput::Private *>(data);
    pw_buffer *buffer = pw_stream_dequeue_buffer(d->stream);
    if (!buffer) {
        return;
    }
    spa_data &block = buffer->buffer->datas[0];
    if (block.data) {
        auto *sequence = static_cast<spa_pod_sequence *>(block.data);
        spa_pod_control *control = nullptr;
        SPA_POD_SEQUENCE_FOREACH(sequence, control)
        {
            const void *body = SPA_POD_BODY(&control->value);
            const uint32_t size = SPA_POD_BODY_SIZE(&control->value);
            if (control->type == SPA_CONTROL_UMP) {
                // Universal MIDI packets, which is what PipeWire carries now.
                // Turned back into MIDI 1.0 bytes rather than understood as
                // themselves: nothing above here wants a second way to say
                // "note on".
                const uint32_t *ump = static_cast<const uint32_t *>(body);
                size_t left = size;
                uint64_t state = 0;
                uint8_t bytes[32];
                int written = 0;
                while (left >= 4
                       && (written = spa_ump_to_midi(&ump, &left, bytes, sizeof(bytes), &state))
                           > 0) {
                    d->push(fromBytes(bytes, written));
                }
            } else if (control->type == SPA_CONTROL_Midi) {
                // What older servers send. Deprecated upstream and cheap to
                // keep: it is the same parser with nothing in front of it.
                d->push(fromBytes(static_cast<const uint8_t *>(body), int(size)));
            }
        }
    }
    pw_stream_queue_buffer(d->stream, buffer);
}

const pw_stream_events &streamEvents()
{
    static const pw_stream_events events = [] {
        pw_stream_events filled{};
        filled.version = PW_VERSION_STREAM_EVENTS;
        filled.state_changed = onStateChanged;
        filled.process = onProcess;
        return filled;
    }();
    return events;
}
#endif
}

MidiInput::MidiInput(const Options &options)
    : d(std::make_unique<Private>())
{
    d->options = options;

#ifndef FRETWORK_HAVE_PIPEWIRE
    d->error = i18n("this copy of Fretwork was built without PipeWire, so it has no MIDI in");
#else
    initialisePipeWire();

    d->loop = pw_thread_loop_new("fretwork-midi", nullptr);
    if (!d->loop) {
        d->error = i18n("PipeWire would not start a thread to listen for MIDI on");
        return;
    }

    pw_properties *properties = pw_properties_new(PW_KEY_MEDIA_TYPE, "Midi",
                                                  PW_KEY_MEDIA_CATEGORY, "Capture",
                                                  PW_KEY_MEDIA_ROLE, "Production",
                                                  PW_KEY_APP_NAME, "Fretwork",
                                                  PW_KEY_NODE_NAME,
                                                  options.name.toUtf8().constData(), nullptr);
    if (!options.device.isEmpty()) {
        pw_properties_set(properties, PW_KEY_TARGET_OBJECT,
                          options.device.toUtf8().constData());
    }

    pw_thread_loop_lock(d->loop);
    d->context = pw_context_new(pw_thread_loop_get_loop(d->loop), nullptr, 0);
    d->core = d->context ? pw_context_connect(d->context, nullptr, 0) : nullptr;
    if (!d->core) {
        pw_thread_loop_unlock(d->loop);
        d->error = i18n("PipeWire would not let Fretwork into the graph");
        return;
    }

    d->stream = pw_stream_new(d->core, options.name.toUtf8().constData(), properties);
    if (!d->stream) {
        pw_thread_loop_unlock(d->loop);
        d->error = i18n("PipeWire would not open a MIDI stream");
        return;
    }
    pw_stream_add_listener(d->stream, &d->listener, &streamEvents(), d.get());

    // Not autoconnected: nothing links a MIDI capture stream on its own, and
    // the port that was asked for is linked to below once the graph has
    // introduced both ends of it.
    const int connected =
        pw_stream_connect(d->stream, PW_DIRECTION_INPUT, PW_ID_ANY,
                          pw_stream_flags(PW_STREAM_FLAG_MAP_BUFFERS
                                          | PW_STREAM_FLAG_RT_PROCESS),
                          nullptr, 0);
    if (connected >= 0 && !options.device.isEmpty()) {
        d->registry = pw_core_get_registry(d->core, PW_VERSION_REGISTRY, 0);
        pw_registry_add_listener(d->registry, &d->registryHook, &registryEvents(), d.get());
        // Everything that already exists is announced before this comes back,
        // so the port being looked for is either known by then or is not there.
        pw_core_add_listener(d->core, &d->coreHook, &coreEvents(), d.get());
        pw_core_sync(d->core, PW_ID_CORE, 0);
    }
    pw_thread_loop_unlock(d->loop);

    if (connected < 0) {
        d->error = options.device.isEmpty()
            ? i18n("no MIDI input could be connected to")
            : i18n("no MIDI input called \"%1\" could be connected to", options.device);
        return;
    }

    if (pw_thread_loop_start(d->loop) < 0) {
        d->error = i18n("the MIDI stream would not start");
    }
#endif
}

MidiInput::~MidiInput()
{
#ifdef FRETWORK_HAVE_PIPEWIRE
    if (d->loop) {
        pw_thread_loop_stop(d->loop);
    }
    if (d->stream) {
        pw_stream_destroy(d->stream);
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

bool MidiInput::isValid() const
{
    return d->error.isEmpty();
}

QString MidiInput::error() const
{
    return d->error;
}

bool MidiInput::isRunning() const
{
    return d->running.load(std::memory_order_acquire);
}

qint64 MidiInput::messagesSeen() const
{
    return d->written.load(std::memory_order_acquire);
}

QList<MidiInput::Event> MidiInput::take()
{
    const qint64 written = d->written.load(std::memory_order_acquire);
    qint64 read = d->read.load(std::memory_order_relaxed);

    // A reader that has fallen more than a ring behind has had the oldest
    // overwritten under it. Skipping to what is still there is the honest
    // answer: the alternative is handing back messages that were replaced
    // while they were being copied.
    if (written - read > Capacity) {
        read = written - Capacity;
    }

    QList<Event> events;
    events.reserve(int(written - read));
    for (qint64 at = read; at < written; ++at) {
        events.append(d->ring[size_t(at % Capacity)]);
    }
    d->read.store(written, std::memory_order_release);
    return events;
}
