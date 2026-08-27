// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "lv2chain.h"

#include <KLocalizedString>

#include <QHash>
#include <QMutex>
#include <QMutexLocker>

#ifdef FRETWORK_HAVE_LILV
#include <lilv/lilv.h>
#include <lv2/atom/atom.h>
#include <lv2/buf-size/buf-size.h>
#include <lv2/options/options.h>
#include <lv2/parameters/parameters.h>
#include <lv2/urid/urid.h>
#include <lv2/worker/worker.h>
#endif

#include <semaphore.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <thread>
#include <vector>

#ifdef FRETWORK_HAVE_LILV

namespace
{
/**
 * The world, opened once.
 *
 * lilv parses every manifest on the machine to answer the first question asked
 * of it, and there are over a hundred bundles on an ordinary desktop. Doing
 * that per track, or per menu, would be doing it far too often.
 */
LilvWorld *world()
{
    static LilvWorld *once = [] {
        LilvWorld *fresh = lilv_world_new();
        lilv_world_load_all(fresh);
        return fresh;
    }();
    return once;
}

/**
 * The URID map every modern plugin asks for.
 *
 * A number per URI, and the same number for the same URI for the life of the
 * program. Shared across every chain rather than one per plugin, because two
 * plugins that disagree about which number means which URI cannot exchange
 * anything -- which is the entire point of the extension.
 *
 * Guarded by a mutex. Plugins map at instantiation, which is not the audio
 * thread; one that mapped from `run` would be one this could not host, and
 * would say so by blocking rather than by corrupting a table.
 */
class Urids
{
public:
    static Urids &shared()
    {
        static Urids once;
        return once;
    }

    LV2_URID map(const char *uri)
    {
        const QMutexLocker locked(&m_lock);
        const QString key = QString::fromUtf8(uri);
        const auto found = m_byUri.constFind(key);
        if (found != m_byUri.constEnd()) {
            return found.value();
        }
        const LV2_URID given = LV2_URID(m_byUri.size()) + 1;
        m_byUri.insert(key, given);
        m_byId.insert(given, key.toUtf8());
        return given;
    }

    const char *unmap(LV2_URID id)
    {
        const QMutexLocker locked(&m_lock);
        const auto found = m_byId.constFind(id);
        return found == m_byId.constEnd() ? nullptr : found.value().constData();
    }

private:
    QMutex m_lock;
    QHash<QString, LV2_URID> m_byUri;
    QHash<LV2_URID, QByteArray> m_byId;
};

LV2_URID mapUri(LV2_URID_Map_Handle, const char *uri)
{
    return Urids::shared().map(uri);
}

const char *unmapUri(LV2_URID_Unmap_Handle, LV2_URID id)
{
    return Urids::shared().unmap(id);
}

/**
 * A plugin's non-realtime errand runner.
 *
 * Some plugins have work that cannot be done in an audio callback -- guitarix
 * amplifiers build their tube models, convolvers load impulse responses -- and
 * LV2 has an extension for exactly that: the plugin asks from `run`, a host
 * thread does the work, and the answer is handed back on the next block.
 *
 * The lazy way to satisfy this is to do the work on the spot and pretend, and
 * it would even appear to function: the plugin would get its answer and the
 * audio thread would block for however long the errand took. This program has
 * said too many times that the callback does not block to start now.
 *
 * Two byte rings and a semaphore. Framed with a length, because a worker
 * message is a blob and the ring has to know where one ends.
 */
class Worker
{
public:
    Worker()
    {
        sem_init(&m_waiting, 0, 0);
    }

    /**
     * The schedule the plugin is handed, which must outlive instantiation.
     *
     * A plugin keeps this pointer and calls through it from `run`, long after
     * the feature array it arrived in has gone. Held here so that its lifetime
     * is the worker's, which is the instance's.
     */
    LV2_Worker_Schedule *schedule()
    {
        m_schedule = {this, scheduleWorkFor};
        return &m_schedule;
    }

    /** Once the instance exists, which is the earliest it can be known. */
    void attach(LilvInstance *instance, const LV2_Worker_Interface *interface)
    {
        m_handle = lilv_instance_get_handle(instance);
        m_interface = interface;
        m_thread = std::thread([this] { serve(); });
    }

    bool isAttached() const
    {
        return m_interface != nullptr;
    }

    ~Worker()
    {
        m_stopping.store(true);
        sem_post(&m_waiting);
        if (m_thread.joinable()) {
            m_thread.join();
        }
        sem_destroy(&m_waiting);
    }

    Worker(const Worker &) = delete;
    Worker &operator=(const Worker &) = delete;

    /** Called from the audio thread by the plugin. */
    LV2_Worker_Status schedule(uint32_t size, const void *data)
    {
        if (!push(m_requests, size, data)) {
            return LV2_WORKER_ERR_NO_SPACE;
        }
        sem_post(&m_waiting);
        return LV2_WORKER_SUCCESS;
    }

    /** Called from the audio thread after run, before the next block. */
    void deliver()
    {
        if (!m_interface) {
            return;
        }
        std::vector<char> message;
        while (pop(m_responses, &message)) {
            m_interface->work_response(m_handle, uint32_t(message.size()), message.data());
        }
        if (m_interface->end_run) {
            m_interface->end_run(m_handle);
        }
    }

private:
    struct Ring {
        std::vector<char> bytes = std::vector<char>(1 << 16);
        std::atomic<size_t> written{0};
        std::atomic<size_t> read{0};
    };

    static bool push(Ring &ring, uint32_t size, const void *data)
    {
        const size_t need = sizeof(uint32_t) + size;
        const size_t written = ring.written.load(std::memory_order_relaxed);
        const size_t read = ring.read.load(std::memory_order_acquire);
        if (written - read + need > ring.bytes.size()) {
            return false;
        }
        const size_t mask = ring.bytes.size() - 1;
        for (size_t index = 0; index < sizeof(uint32_t); ++index) {
            ring.bytes[(written + index) & mask] = reinterpret_cast<const char *>(&size)[index];
        }
        for (size_t index = 0; index < size; ++index) {
            ring.bytes[(written + sizeof(uint32_t) + index) & mask] =
                static_cast<const char *>(data)[index];
        }
        ring.written.store(written + need, std::memory_order_release);
        return true;
    }

    static bool pop(Ring &ring, std::vector<char> *into)
    {
        const size_t written = ring.written.load(std::memory_order_acquire);
        size_t read = ring.read.load(std::memory_order_relaxed);
        if (written - read < sizeof(uint32_t)) {
            return false;
        }
        const size_t mask = ring.bytes.size() - 1;
        uint32_t size = 0;
        for (size_t index = 0; index < sizeof(uint32_t); ++index) {
            reinterpret_cast<char *>(&size)[index] = ring.bytes[(read + index) & mask];
        }
        if (written - read < sizeof(uint32_t) + size) {
            return false;
        }
        read += sizeof(uint32_t);
        into->resize(size);
        for (size_t index = 0; index < size; ++index) {
            (*into)[index] = ring.bytes[(read + index) & mask];
        }
        ring.read.store(read + size, std::memory_order_release);
        return true;
    }

    static LV2_Worker_Status respond(LV2_Worker_Respond_Handle handle, uint32_t size,
                                     const void *data)
    {
        auto *self = static_cast<Worker *>(handle);
        return push(self->m_responses, size, data) ? LV2_WORKER_SUCCESS
                                                   : LV2_WORKER_ERR_NO_SPACE;
    }

    void serve()
    {
        std::vector<char> message;
        while (true) {
            sem_wait(&m_waiting);
            if (m_stopping.load()) {
                return;
            }
            while (pop(m_requests, &message)) {
                m_interface->work(m_handle, respond, this, uint32_t(message.size()),
                                  message.data());
            }
        }
    }

    static LV2_Worker_Status scheduleWorkFor(LV2_Worker_Schedule_Handle handle,
                                             uint32_t size, const void *data)
    {
        return static_cast<Worker *>(handle)->schedule(size, data);
    }

    LV2_Handle m_handle = nullptr;
    const LV2_Worker_Interface *m_interface = nullptr;
    LV2_Worker_Schedule m_schedule{};
    Ring m_requests;
    Ring m_responses;
    sem_t m_waiting{};
    std::atomic<bool> m_stopping{false};
    std::thread m_thread;
};

int countPorts(const LilvPlugin *plugin, const LilvNode *audio, const LilvNode *direction)
{
    int found = 0;
    const uint32_t total = lilv_plugin_get_num_ports(plugin);
    for (uint32_t index = 0; index < total; ++index) {
        const LilvPort *port = lilv_plugin_get_port_by_index(plugin, index);
        if (lilv_port_is_a(plugin, port, audio) && lilv_port_is_a(plugin, port, direction)) {
            ++found;
        }
    }
    return found;
}

/**
 * The knobs, as the plugin describes them.
 *
 * Read once at load, because it means parsing the plugin's own turtle and a
 * window redrawing a panel should not do that.
 */
QList<Lv2::Control> readControls(const LilvPlugin *plugin, std::vector<float> &values)
{
    QList<Lv2::Control> controls;

    LilvNode *controlPort = lilv_new_uri(world(), LV2_CORE__ControlPort);
    LilvNode *inputPort = lilv_new_uri(world(), LV2_CORE__InputPort);
    LilvNode *toggled = lilv_new_uri(world(), LV2_CORE__toggled);
    LilvNode *integer = lilv_new_uri(world(), LV2_CORE__integer);
    LilvNode *enumeration = lilv_new_uri(world(), LV2_CORE__enumeration);
    LilvNode *logarithmic =
        lilv_new_uri(world(), "http://lv2plug.in/ns/ext/port-props#logarithmic");

    const uint32_t total = lilv_plugin_get_num_ports(plugin);
    std::vector<float> minimums(total);
    std::vector<float> maximums(total);
    std::vector<float> defaults(total);
    lilv_plugin_get_port_ranges_float(plugin, minimums.data(), maximums.data(),
                                      defaults.data());

    for (uint32_t index = 0; index < total; ++index) {
        const LilvPort *port = lilv_plugin_get_port_by_index(plugin, index);
        // Input controls only: an output control is a meter, and a meter with
        // a slider on it is a lie about which way the information flows.
        if (!lilv_port_is_a(plugin, port, controlPort)
            || !lilv_port_is_a(plugin, port, inputPort)) {
            continue;
        }

        Lv2::Control control;
        control.index = index;
        control.symbol =
            QString::fromUtf8(lilv_node_as_string(lilv_port_get_symbol(plugin, port)));
        if (LilvNode *name = lilv_port_get_name(plugin, port)) {
            control.name = QString::fromUtf8(lilv_node_as_string(name));
            lilv_node_free(name);
        }
        control.minimum = std::isnan(minimums[index]) ? 0.0f : minimums[index];
        control.maximum = std::isnan(maximums[index]) ? 1.0f : maximums[index];
        control.value = index < values.size() ? values[index] : 0.0f;
        control.toggled = lilv_port_has_property(plugin, port, toggled);
        control.integer = lilv_port_has_property(plugin, port, integer);
        control.logarithmic = lilv_port_has_property(plugin, port, logarithmic);

        if (lilv_port_has_property(plugin, port, enumeration)) {
            if (LilvScalePoints *points = lilv_port_get_scale_points(plugin, port)) {
                LILV_FOREACH (scale_points, iterator, points) {
                    const LilvScalePoint *point = lilv_scale_points_get(points, iterator);
                    control.choices.append(QString::fromUtf8(
                        lilv_node_as_string(lilv_scale_point_get_label(point))));
                    control.choiceValues.append(
                        float(lilv_node_as_float(lilv_scale_point_get_value(point))));
                }
                lilv_scale_points_free(points);
            }
        }
        controls.append(control);
    }

    lilv_node_free(controlPort);
    lilv_node_free(inputPort);
    lilv_node_free(toggled);
    lilv_node_free(integer);
    lilv_node_free(enumeration);
    lilv_node_free(logarithmic);
    return controls;
}

Lv2::Description describeOne(const LilvPlugin *plugin)
{
    Lv2::Description described;
    described.uri = QString::fromUtf8(lilv_node_as_uri(lilv_plugin_get_uri(plugin)));
    if (LilvNode *name = lilv_plugin_get_name(plugin)) {
        described.name = QString::fromUtf8(lilv_node_as_string(name));
        lilv_node_free(name);
    }

    LilvNode *audio = lilv_new_uri(world(), LV2_CORE__AudioPort);
    LilvNode *input = lilv_new_uri(world(), LV2_CORE__InputPort);
    LilvNode *output = lilv_new_uri(world(), LV2_CORE__OutputPort);
    described.audioInputs = countPorts(plugin, audio, input);
    described.audioOutputs = countPorts(plugin, audio, output);
    lilv_node_free(audio);
    lilv_node_free(input);
    lilv_node_free(output);
    return described;
}
}

QList<Lv2::Description> Lv2::installed()
{
    QList<Description> found;
    const LilvPlugins *plugins = lilv_world_get_all_plugins(world());
    LILV_FOREACH (plugins, iterator, plugins) {
        const LilvPlugin *plugin = lilv_plugins_get(plugins, iterator);
        const Description described = describeOne(plugin);
        // Only what a stereo insert can actually use. A synth with no audio in
        // and an analyser with no audio out are both real plugins and neither
        // belongs in a chain between an instrument and a fader.
        if (described.usable()) {
            found.append(described);
        }
    }
    std::sort(found.begin(), found.end(), [](const Description &a, const Description &b) {
        return a.name.localeAwareCompare(b.name) < 0;
    });
    return found;
}

Lv2::Description Lv2::describe(const QString &uri)
{
    LilvNode *wanted = lilv_new_uri(world(), uri.toUtf8().constData());
    const LilvPlugin *plugin = lilv_plugins_get_by_uri(lilv_world_get_all_plugins(world()),
                                                       wanted);
    lilv_node_free(wanted);
    return plugin ? describeOne(plugin) : Description{};
}

/** One plugin in the chain, which may be two instances of a mono one. */
struct Hosted {
    QString uri;
    QString name;
    LilvInstance *left = nullptr;
    /** Only for a mono plugin, treating the other side. */
    LilvInstance *right = nullptr;
    std::unique_ptr<Worker> leftWorker;
    std::unique_ptr<Worker> rightWorker;
    std::vector<float> controls;        //< held for the life of the stage

    /** Port indices, found once: the callback must not go looking. */
    std::vector<uint32_t> audioIn;
    std::vector<uint32_t> audioOut;

    /** What the knobs are, read from the plugin's own description. */
    QList<Lv2::Control> described;
};

struct Lv2::Chain::Private {
    Options options;
    QString error;
    std::vector<Hosted> stages;

    /** Scratch for a mono plugin's one output, sized once. */
    std::vector<float> spare;

    // Held for the life of the chain: a plugin may read an option long after
    // it was handed one, and pointing it at a local would be pointing it at
    // a stack frame that has gone.
    int32_t maximumBlock = 8192;
    int32_t minimumBlock = 1;
    float rate = 48000;
    std::vector<LV2_Options_Option> options_;
};

Lv2::Chain::Chain(const QStringList &uris, const Options &options)
    : d(std::make_unique<Private>())
{
    d->options = options;
    d->spare.assign(size_t(std::max(64, options.maximumFrames)), 0.0f);

    static LV2_URID_Map map{nullptr, mapUri};
    static LV2_URID_Unmap unmap{nullptr, unmapUri};
    static const LV2_Feature mapFeature{LV2_URID__map, &map};
    static const LV2_Feature unmapFeature{LV2_URID__unmap, &unmap};

    // The options a plugin is entitled to ask for before it will start.
    // Guitarix refuses to instantiate without them, and says "Missing feature
    // options." rather than which one, so they are all provided: how long a
    // block may be, and how fast the audio is.
    d->maximumBlock = int32_t(options.maximumFrames);
    d->rate = float(options.sampleRate);
    const LV2_URID intType = Urids::shared().map(LV2_ATOM__Int);
    const LV2_URID floatType = Urids::shared().map(LV2_ATOM__Float);
    d->options_ = {
        {LV2_OPTIONS_INSTANCE, 0, Urids::shared().map(LV2_BUF_SIZE__maxBlockLength),
         sizeof(int32_t), intType, &d->maximumBlock},
        {LV2_OPTIONS_INSTANCE, 0, Urids::shared().map(LV2_BUF_SIZE__minBlockLength),
         sizeof(int32_t), intType, &d->minimumBlock},
        {LV2_OPTIONS_INSTANCE, 0, Urids::shared().map(LV2_BUF_SIZE__nominalBlockLength),
         sizeof(int32_t), intType, &d->maximumBlock},
        {LV2_OPTIONS_INSTANCE, 0, Urids::shared().map(LV2_PARAMETERS__sampleRate),
         sizeof(float), floatType, &d->rate},
        {LV2_OPTIONS_INSTANCE, 0, 0, 0, 0, nullptr},
    };
    const LV2_Feature optionsFeature{LV2_OPTIONS__options, d->options_.data()};
    const LV2_Feature boundedFeature{LV2_BUF_SIZE__boundedBlockLength, nullptr};
    // The worker's own feature is added per instance below, because its
    // handle is the instance's errand runner and there is one of those each.

    const LilvPlugins *plugins = lilv_world_get_all_plugins(world());
    LilvNode *audio = lilv_new_uri(world(), LV2_CORE__AudioPort);
    LilvNode *control = lilv_new_uri(world(), LV2_CORE__ControlPort);
    LilvNode *input = lilv_new_uri(world(), LV2_CORE__InputPort);

    for (const QString &uri : uris) {
        LilvNode *wanted = lilv_new_uri(world(), uri.toUtf8().constData());
        const LilvPlugin *plugin = lilv_plugins_get_by_uri(plugins, wanted);
        lilv_node_free(wanted);
        if (!plugin) {
            d->error = i18n("no plugin called %1 is installed", uri);
            break;
        }

        const Description described = describeOne(plugin);
        if (!described.usable()) {
            d->error = i18n("%1 has %2 audio inputs and %3 outputs, which is not something "
                            "this can put in a chain",
                            described.name, QString::number(described.audioInputs),
                            QString::number(described.audioOutputs));
            break;
        }

        Hosted stage;
        stage.uri = uri;
        stage.name = described.name;

        // Each instance gets an errand runner of its own, because each has its
        // own handle and its own queue of work.
        const auto start = [&](std::unique_ptr<Worker> &worker) -> LilvInstance * {
            worker = std::make_unique<Worker>();
            const LV2_Feature scheduleFeature{LV2_WORKER__schedule, worker->schedule()};
            const LV2_Feature *const withWorker[] = {
                &mapFeature, &unmapFeature, &optionsFeature, &boundedFeature,
                &scheduleFeature, nullptr};
            LilvInstance *made =
                lilv_plugin_instantiate(plugin, options.sampleRate, withWorker);
            if (!made) {
                return nullptr;
            }
            if (const auto *interface = static_cast<const LV2_Worker_Interface *>(
                    lilv_instance_get_extension_data(made, LV2_WORKER__interface))) {
                worker->attach(made, interface);
            }
            return made;
        };

        stage.left = start(stage.leftWorker);
        if (described.audioInputs == 1) {
            stage.right = start(stage.rightWorker);
        }
        if (!stage.left || (described.audioInputs == 1 && !stage.right)) {
            d->error = i18n("%1 would not start", described.name);
            break;
        }

        // Control ports keep the value the plugin says they should have. A
        // chain that is not adjustable yet is still a chain; one whose gains
        // were left pointing at whatever memory held is a chain that screams.
        const uint32_t total = lilv_plugin_get_num_ports(plugin);
        stage.controls.assign(size_t(total), 0.0f);
        std::vector<float> minimums(total);
        std::vector<float> maximums(total);
        std::vector<float> defaults(total);
        lilv_plugin_get_port_ranges_float(plugin, minimums.data(), maximums.data(),
                                          defaults.data());

        for (uint32_t index = 0; index < total; ++index) {
            const LilvPort *port = lilv_plugin_get_port_by_index(plugin, index);
            if (!lilv_port_is_a(plugin, port, control)) {
                continue;
            }
            const float value = std::isnan(defaults[index]) ? 0.0f : defaults[index];
            stage.controls[size_t(index)] = value;
            lilv_instance_connect_port(stage.left, index, &stage.controls[size_t(index)]);
            if (stage.right) {
                lilv_instance_connect_port(stage.right, index,
                                           &stage.controls[size_t(index)]);
            }
        }
        // Which port index is which, worked out here rather than in the
        // callback: looking a port up by class per block would be parsing the
        // plugin's description sixty times a second.
        for (uint32_t index = 0; index < total; ++index) {
            const LilvPort *port = lilv_plugin_get_port_by_index(plugin, index);
            if (!lilv_port_is_a(plugin, port, audio)) {
                continue;
            }
            if (lilv_port_is_a(plugin, port, input)) {
                stage.audioIn.push_back(index);
            } else {
                stage.audioOut.push_back(index);
            }
        }

        stage.described = readControls(plugin, stage.controls);
        d->stages.push_back(std::move(stage));
    }

    lilv_node_free(audio);
    lilv_node_free(control);
    lilv_node_free(input);

    if (!d->error.isEmpty()) {
        d->stages.clear();
        return;
    }
    for (Hosted &stage : d->stages) {
        lilv_instance_activate(stage.left);
        if (stage.right) {
            lilv_instance_activate(stage.right);
        }
    }
}

Lv2::Chain::~Chain()
{
    for (Hosted &stage : d->stages) {
        if (stage.left) {
            lilv_instance_deactivate(stage.left);
            lilv_instance_free(stage.left);
        }
        if (stage.right) {
            lilv_instance_deactivate(stage.right);
            lilv_instance_free(stage.right);
        }
    }
}

bool Lv2::Chain::isValid() const
{
    return d->error.isEmpty() && !d->stages.empty();
}

QString Lv2::Chain::error() const
{
    return d->error;
}

QStringList Lv2::Chain::loaded() const
{
    QStringList names;
    for (const Hosted &stage : d->stages) {
        names.append(stage.name);
    }
    return names;
}

QList<Lv2::Stage> Lv2::Chain::stages() const
{
    QList<Stage> described;
    for (const Hosted &hosted : d->stages) {
        Stage stage;
        stage.uri = hosted.uri;
        stage.name = hosted.name;
        stage.controls = hosted.described;
        // The value a knob is at now, rather than the one it started at.
        for (Control &control : stage.controls) {
            if (control.index < hosted.controls.size()) {
                control.value = hosted.controls[control.index];
            }
        }
        described.append(stage);
    }
    return described;
}

void Lv2::Chain::setControl(int stage, uint32_t index, float value)
{
    if (stage < 0 || stage >= int(d->stages.size())) {
        return;
    }
    Hosted &hosted = d->stages[size_t(stage)];
    if (index >= hosted.controls.size()) {
        return;
    }
    // Straight into the float the plugin reads. Both instances of a mono
    // plugin are connected to the same one, so a stereo pair cannot drift
    // apart into two different settings of the same knob.
    hosted.controls[index] = value;
}

bool Lv2::Chain::setControl(int stage, const QString &symbol, float value)
{
    if (stage < 0 || stage >= int(d->stages.size())) {
        return false;
    }
    for (const Control &control : d->stages[size_t(stage)].described) {
        if (control.symbol == symbol) {
            setControl(stage, control.index, value);
            return true;
        }
    }
    return false;
}

void Lv2::Chain::process(float *left, float *right, int frames)
{
    if (frames <= 0 || frames > int(d->spare.size())) {
        return;
    }

    for (Hosted &stage : d->stages) {
        // In place: every plugin here has as many outputs as inputs, so the
        // block that came in is the block that goes out, and a chain of six
        // needs no buffer of its own.
        if (stage.right) {
            // A mono plugin, twice: one instance per side.
            lilv_instance_connect_port(stage.left, stage.audioIn.at(0), left);
            lilv_instance_connect_port(stage.left, stage.audioOut.at(0), left);
            lilv_instance_connect_port(stage.right, stage.audioIn.at(0), right);
            lilv_instance_connect_port(stage.right, stage.audioOut.at(0), right);
            lilv_instance_run(stage.left, uint32_t(frames));
            lilv_instance_run(stage.right, uint32_t(frames));
            stage.leftWorker->deliver();
            stage.rightWorker->deliver();
        } else {
            lilv_instance_connect_port(stage.left, stage.audioIn.at(0), left);
            lilv_instance_connect_port(stage.left, stage.audioIn.at(1), right);
            lilv_instance_connect_port(stage.left, stage.audioOut.at(0), left);
            lilv_instance_connect_port(stage.left, stage.audioOut.at(1), right);
            lilv_instance_run(stage.left, uint32_t(frames));
            stage.leftWorker->deliver();
        }
    }
}

#else   // no lilv

QList<Lv2::Description> Lv2::installed()
{
    return {};
}

Lv2::Description Lv2::describe(const QString &)
{
    return {};
}

struct Lv2::Chain::Private {
    QString error;
};

Lv2::Chain::Chain(const QStringList &, const Options &)
    : d(std::make_unique<Private>())
{
    d->error = i18n("this copy of Fretwork was built without lilv, so it hosts no effects");
}

Lv2::Chain::~Chain() = default;

bool Lv2::Chain::isValid() const
{
    return false;
}

QString Lv2::Chain::error() const
{
    return d->error;
}

QStringList Lv2::Chain::loaded() const
{
    return {};
}

QList<Lv2::Stage> Lv2::Chain::stages() const
{
    return {};
}

void Lv2::Chain::setControl(int, uint32_t, float)
{
}

bool Lv2::Chain::setControl(int, const QString &, float)
{
    return false;
}

void Lv2::Chain::process(float *, float *, int)
{
}

#endif
