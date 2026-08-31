// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#pragma once

#include <QList>
#include <QString>

#include <memory>

/**
 * MIDI arriving from a controller.
 *
 * The plumbing under two features that share almost no code above it: a
 * control surface turning knobs on an amplifier, and a keyboard typing notes
 * into the score. Both want the same thing from here -- messages, in order,
 * without blocking anybody -- so this knows about neither.
 *
 * **Through PipeWire rather than the ALSA sequencer**, decided once and worth
 * recording. The program already owns a PipeWire node with a considered
 * position on clocks and callbacks, and an ALSA sequencer client alongside it
 * would be a second idea of the graph. What settled it was that nothing is
 * lost by choosing it: PipeWire's bridge already exposes every hardware port
 * on this machine by its own name -- `Midi-Bridge:Minilab3 MCU/HUI` and the
 * rest -- so a controller with four ports is four things to link to rather
 * than one thing to demultiplex. And it adds no dependency, since PipeWire is
 * already an optional one and this is optional in exactly the same way.
 *
 * **What comes in is an edit or a control, and never a recording.** There is
 * no timestamp here beyond the order things arrived in, and that is
 * deliberate: the moment this carries time, somebody will ask it to carry a
 * performance, and a captured performance sitting beside the score is a
 * different program.
 */
class MidiInput
{
public:
    struct Options {
        /**
         * A PipeWire MIDI port to listen to; empty takes whatever the graph
         * offers.
         *
         * Named rather than searched for, because a controller is several
         * ports and only one of them is the one being asked about: the
         * Minilab3's `MCU/HUI` is its transport and encoders, and its `MIDI`
         * is its keys.
         */
        QString device;

        /** What this node is called in the graph. */
        QString name = QStringLiteral("Fretwork");
    };

    /**
     * One message, as MIDI 1.0 says it.
     *
     * Converted from whatever the graph delivered -- PipeWire 1.6 carries
     * universal MIDI packets, older ones carry raw bytes -- because everything
     * above this speaks in note numbers and controller numbers, and neither
     * cares which century the wire was from.
     */
    struct Event {
        enum class Kind {
            NoteOn,
            NoteOff,
            ControlChange,
            PitchBend,
            Other,      //< anything this does not translate, kept so it can be counted
        };

        Kind kind = Kind::Other;
        int channel = 0;    //< 0 to 15
        int data1 = 0;      //< note number, or controller number, or bend low bits
        int data2 = 0;      //< velocity, or controller value, or bend high bits

        /** A pitch bend as one number, 0 to 16383, centred at 8192. */
        int bend() const
        {
            return data1 | (data2 << 7);
        }
    };

    explicit MidiInput(const Options &options);
    ~MidiInput();

    MidiInput(const MidiInput &) = delete;
    MidiInput &operator=(const MidiInput &) = delete;

    bool isValid() const;
    QString error() const;

    /** True once the graph has connected the stream to something. */
    bool isRunning() const;

    /** How many messages have arrived, for "is this controller plugged in". */
    qint64 messagesSeen() const;

    /**
     * Everything that has arrived since the last call, oldest first.
     *
     * Drained by whoever is polling rather than pushed at them, because the
     * thread it arrives on may not allocate, take a lock, or touch a score.
     * A caller that stops draining loses the oldest messages rather than
     * growing without limit: a knob turned while nobody was looking is not
     * worth a megabyte.
     */
    QList<Event> take();

    /** What the graph's thread and the reader share. Nothing else may name it. */
    struct Private;

private:
    std::unique_ptr<Private> d;
};
