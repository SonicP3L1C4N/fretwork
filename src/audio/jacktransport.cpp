// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "jacktransport.h"

#include <KLocalizedString>

#include <QDir>
#include <QFileInfo>

#include <dlfcn.h>

#include <algorithm>

namespace
{
/** The handful of calls this needs; the rest of JACK is somebody else's. */
struct Api {
    void *(*open)(const char *, int, int *) = nullptr;
    int (*close)(void *) = nullptr;
    int (*activate)(void *) = nullptr;
    int (*deactivate)(void *) = nullptr;
    void (*start)(void *) = nullptr;
    void (*stop)(void *) = nullptr;
    int (*locate)(void *, unsigned int) = nullptr;
};

/**
 * Where to look, in the order worth looking.
 *
 * PipeWire's own copy first. The loader would give us the real JACK's, which
 * on a machine running PipeWire is a library for talking to a server that is
 * not running -- and the failure is a timeout rather than an error, which is
 * the worst kind.
 */
QStringList candidates()
{
    QStringList found;
    for (const QString &root : {QStringLiteral("/usr/lib"), QStringLiteral("/usr/lib64"),
                                QStringLiteral("/usr/local/lib")}) {
        QDir directory(root);
        const QStringList architectures =
            directory.entryList({QStringLiteral("*-linux-gnu*")}, QDir::Dirs);
        QStringList bases = {root};
        for (const QString &architecture : architectures) {
            bases.append(directory.filePath(architecture));
        }
        for (const QString &base : bases) {
            const QString path =
                base + QStringLiteral("/pipewire-0.3/jack/libjack.so.0");
            if (QFileInfo::exists(path)) {
                found.append(path);
            }
        }
    }
    // And whatever the loader would have chosen, for a machine actually
    // running JACK rather than PipeWire's impression of it.
    found.append(QStringLiteral("libjack.so.0"));
    return found;
}
}

struct JackTransport::Private {
    void *library = nullptr;
    void *client = nullptr;
    Api api;
    QString error;
    QString which;

    /**
     * Whether we are the reason it is rolling.
     *
     * A shared transport is not ours to stop on the way out -- unless we
     * started it, in which case leaving it running leaves whatever is
     * following it running for ever, with nothing playing.
     */
    bool started = false;
};

JackTransport::JackTransport()
    : d(std::make_unique<Private>())
{
    const QStringList paths = candidates();
    for (const QString &path : paths) {
        void *library = dlopen(path.toUtf8().constData(), RTLD_NOW | RTLD_LOCAL);
        if (!library) {
            continue;
        }

        Api api;
        const auto symbol = [library](const char *name) { return dlsym(library, name); };
        api.open = reinterpret_cast<decltype(api.open)>(symbol("jack_client_open"));
        api.close = reinterpret_cast<decltype(api.close)>(symbol("jack_client_close"));
        api.activate = reinterpret_cast<decltype(api.activate)>(symbol("jack_activate"));
        api.deactivate =
            reinterpret_cast<decltype(api.deactivate)>(symbol("jack_deactivate"));
        api.start = reinterpret_cast<decltype(api.start)>(symbol("jack_transport_start"));
        api.stop = reinterpret_cast<decltype(api.stop)>(symbol("jack_transport_stop"));
        api.locate = reinterpret_cast<decltype(api.locate)>(symbol("jack_transport_locate"));
        if (!api.open || !api.close || !api.activate || !api.start || !api.stop
            || !api.locate) {
            dlclose(library);
            continue;
        }

        // JackNoStartServer, so that a machine with real JACK installed and
        // not running does not have one started underneath it by a tablature
        // program asking about a transport.
        int status = 0;
        void *client = api.open("Fretwork transport", 0x01, &status);
        if (!client) {
            dlclose(library);
            continue;
        }
        api.activate(client);

        d->library = library;
        d->client = client;
        d->api = api;
        d->which = path;
        return;
    }
    d->error = i18n("no JACK transport could be reached, so the transport stays "
                    "Fretwork's own");
}

JackTransport::~JackTransport()
{
    if (d->client && d->started) {
        d->api.stop(d->client);
    }
    if (d->client) {
        if (d->api.deactivate) {
            d->api.deactivate(d->client);
        }
        d->api.close(d->client);
    }
    if (d->library) {
        // Left loaded on purpose: unloading a library that has started threads
        // and registered atexit handlers is a way to crash on the way out.
        // A few hundred kilobytes until the process ends is the better trade.
    }
}

bool JackTransport::isValid() const
{
    return d->client != nullptr;
}

QString JackTransport::error() const
{
    return d->error;
}

QString JackTransport::library() const
{
    return d->which;
}

void JackTransport::start()
{
    if (d->client) {
        d->api.start(d->client);
        d->started = true;
    }
}

void JackTransport::stop()
{
    if (d->client) {
        d->api.stop(d->client);
        d->started = false;
    }
}

void JackTransport::locate(qint64 frame)
{
    if (d->client) {
        d->api.locate(d->client, static_cast<unsigned int>(std::max<qint64>(0, frame)));
    }
}
