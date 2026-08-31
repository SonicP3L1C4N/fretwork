// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#pragma once

#include "midiinput.h"

/**
 * Mackie Control, read off the wire.
 *
 * A published protocol every DAW speaks, which is the reason the control
 * surface is the easy half of the MIDI work: a controller in MCU mode already
 * says "play" rather than "note 94 on channel 1", and every hardware maker has
 * agreed what the bytes are for thirty years.
 *
 * Pure translation, deliberately. Nothing here knows what a track or a plugin
 * is; it turns one MIDI message into one statement about what somebody did
 * with their hands, and whoever asked decides what that means. That is what
 * makes it testable without a controller plugged in, which matters for a
 * feature whose whole point is hardware.
 */
namespace Mackie
{
enum class Kind {
    None,       //< anything this does not recognise, which is most of MIDI

    // The transport, which Session already has and the graph already drives.
    Play,
    Stop,
    Record,
    Rewind,
    Forward,

    /** One of the eight encoders, turned. `delta` is signed detents. */
    Encoder,
    /** One of the faders, moved. `value` is 0 to 1. */
    Fader,
    /** A strip's mute or solo, pressed or released. */
    Mute,
    Solo,
};

struct Action {
    Kind kind = Kind::None;
    /** Which strip or encoder, counting from nought. */
    int index = 0;
    /** Encoders: how many detents, and which way. */
    int delta = 0;
    /** Faders: where it now is, 0 to 1. */
    double value = 0;
    /** Buttons: whether this is the press or the release. */
    bool pressed = false;
};

/**
 * What one message means, or `Kind::None`.
 *
 * The protocol's own numbers, and worth stating rather than hiding in a table
 * nobody can check: transport buttons are notes 91 to 95, the eight encoders
 * are controllers 16 to 23 sending *relative* movement rather than a position,
 * the strips' solo and mute are notes 8 to 15 and 16 to 23, and the faders are
 * pitch bend on a channel each.
 */
Action decode(const MidiInput::Event &event);
}
