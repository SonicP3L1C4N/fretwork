// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "mackie.h"

namespace
{
/** The transport buttons, which are notes rather than controllers. */
constexpr int RewindNote = 91;
constexpr int ForwardNote = 92;
constexpr int StopNote = 93;
constexpr int PlayNote = 94;
constexpr int RecordNote = 95;

/** A strip's buttons: eight of each, in blocks. */
constexpr int SoloFirst = 8;
constexpr int MuteFirst = 16;
constexpr int Strips = 8;

/** The eight encoders, which are controllers. */
constexpr int EncoderFirst = 16;
}

Mackie::Action Mackie::decode(const MidiInput::Event &event)
{
    Action action;

    switch (event.kind) {
    case MidiInput::Event::Kind::NoteOn:
    case MidiInput::Event::Kind::NoteOff: {
        // A surface sends the press and the release as note on and note off,
        // and both are worth passing on: a transport button acts on the press,
        // and a mute that only ever saw presses could never be let go of.
        action.pressed = event.kind == MidiInput::Event::Kind::NoteOn;
        switch (event.data1) {
        case PlayNote:
            action.kind = Kind::Play;
            return action;
        case StopNote:
            action.kind = Kind::Stop;
            return action;
        case RecordNote:
            action.kind = Kind::Record;
            return action;
        case RewindNote:
            action.kind = Kind::Rewind;
            return action;
        case ForwardNote:
            action.kind = Kind::Forward;
            return action;
        default:
            break;
        }
        if (event.data1 >= SoloFirst && event.data1 < SoloFirst + Strips) {
            action.kind = Kind::Solo;
            action.index = event.data1 - SoloFirst;
            return action;
        }
        if (event.data1 >= MuteFirst && event.data1 < MuteFirst + Strips) {
            action.kind = Kind::Mute;
            action.index = event.data1 - MuteFirst;
            return action;
        }
        return action;
    }

    case MidiInput::Event::Kind::ControlChange: {
        if (event.data1 < EncoderFirst || event.data1 >= EncoderFirst + Strips) {
            return action;
        }
        // Relative, and this is the part that catches people out: an encoder
        // has no position to send, so the value is a direction and a count of
        // detents rather than where the knob now points. Bit six is the
        // direction and the low six bits are how far it was turned, which is
        // why a fast spin arrives as one message saying five rather than five
        // messages saying one.
        action.kind = Kind::Encoder;
        action.index = event.data1 - EncoderFirst;
        const int detents = event.data2 & 0x3f;
        action.delta = (event.data2 & 0x40) ? -detents : detents;
        return action;
    }

    case MidiInput::Event::Kind::PitchBend:
        // A fader is a whole channel's pitch bend, which is the only thing in
        // the protocol with enough resolution for one: seven bits over a
        // hundred millimetres of travel would step in half-decibels.
        action.kind = Kind::Fader;
        action.index = event.channel;
        action.value = double(event.bend()) / 16383.0;
        return action;

    case MidiInput::Event::Kind::Other:
        break;
    }
    return action;
}
