// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#pragma once

#include <QString>

#include <memory>

/**
 * Starting, stopping and locating the transport the whole graph shares.
 *
 * Fretwork already *follows* that transport. This is the other direction, and
 * it exists because the direction that seemed obvious turns out not to be
 * available: Reaper on Linux references `jack_transport_query` and none of
 * `start`, `stop` or `locate`. It can follow a transport and cannot drive one.
 * So the program that presses play has to be this one.
 *
 * **Through JACK, because PipeWire offers no other way.** A native client
 * cannot set the graph's transport: the position comes from the driver node,
 * and the only public route to changing it is the JACK API, which PipeWire
 * implements precisely so that programs like this one work. Becoming the
 * driver ourselves was the alternative and is worse -- a node that is not a
 * sound card driving the graph's timing would take the clock away from the
 * sound card.
 *
 * **Loaded at runtime, PipeWire's copy first.** Two libjacks are installed on
 * an ordinary desktop: the real JACK's, and PipeWire's implementation of the
 * same interface. The loader prefers the real one, which on a machine running
 * PipeWire is a library for talking to a server that is not there -- so this
 * looks for PipeWire's copy by name before falling back. Where neither works
 * it says so and the transport stays this program's own.
 */
class JackTransport
{
public:
    JackTransport();
    ~JackTransport();

    JackTransport(const JackTransport &) = delete;
    JackTransport &operator=(const JackTransport &) = delete;

    bool isValid() const;
    QString error() const;

    /** Which library answered, for a window to say what it is talking to. */
    QString library() const;

    void start();
    void stop();

    /** Puts the transport at a sample position, rolling or not. */
    void locate(qint64 frame);

private:
    struct Private;
    std::unique_ptr<Private> d;
};
