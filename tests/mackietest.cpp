// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "mackie.h"

#include <QTest>

/**
 * Mackie Control, decoded.
 *
 * The whole reason this is a separate thing from the surface that acts on it:
 * a published protocol can be tested against its own published numbers, with
 * no controller plugged in and no session to drive. What is asserted here is
 * what the specification says, not what this machine's Minilab3 happens to
 * send -- so a different controller in MCU mode is covered by the same tests.
 */
class MackieTest : public QObject
{
    Q_OBJECT

private:
    static MidiInput::Event note(int number, bool on, int channel = 0)
    {
        MidiInput::Event event;
        event.kind = on ? MidiInput::Event::Kind::NoteOn : MidiInput::Event::Kind::NoteOff;
        event.channel = channel;
        event.data1 = number;
        event.data2 = on ? 127 : 0;
        return event;
    }

    static MidiInput::Event controller(int number, int value)
    {
        MidiInput::Event event;
        event.kind = MidiInput::Event::Kind::ControlChange;
        event.data1 = number;
        event.data2 = value;
        return event;
    }

    static MidiInput::Event bend(int channel, int value)
    {
        MidiInput::Event event;
        event.kind = MidiInput::Event::Kind::PitchBend;
        event.channel = channel;
        event.data1 = value & 0x7f;
        event.data2 = (value >> 7) & 0x7f;
        return event;
    }

private Q_SLOTS:
    void theTransportIsNotesNinetyOneToNinetyFive()
    {
        QCOMPARE(Mackie::decode(note(91, true)).kind, Mackie::Kind::Rewind);
        QCOMPARE(Mackie::decode(note(92, true)).kind, Mackie::Kind::Forward);
        QCOMPARE(Mackie::decode(note(93, true)).kind, Mackie::Kind::Stop);
        QCOMPARE(Mackie::decode(note(94, true)).kind, Mackie::Kind::Play);
        QCOMPARE(Mackie::decode(note(95, true)).kind, Mackie::Kind::Record);
    }

    /**
     * The press and the release both arrive, and are told apart.
     *
     * A transport acts on the press; a mute held down has to be let go of. A
     * decoder that dropped note-offs would make the second impossible and
     * nobody would notice until they tried it.
     */
    void aPressAndAReleaseAreDifferentThings()
    {
        QVERIFY(Mackie::decode(note(94, true)).pressed);
        QVERIFY(!Mackie::decode(note(94, false)).pressed);

        const Mackie::Action held = Mackie::decode(note(16, true));
        QCOMPARE(held.kind, Mackie::Kind::Mute);
        QVERIFY(held.pressed);
        QVERIFY(!Mackie::decode(note(16, false)).pressed);
    }

    void eachStripHasItsOwnSoloAndMute()
    {
        for (int strip = 0; strip < 8; ++strip) {
            const Mackie::Action solo = Mackie::decode(note(8 + strip, true));
            QCOMPARE(solo.kind, Mackie::Kind::Solo);
            QCOMPARE(solo.index, strip);

            const Mackie::Action mute = Mackie::decode(note(16 + strip, true));
            QCOMPARE(mute.kind, Mackie::Kind::Mute);
            QCOMPARE(mute.index, strip);
        }
        // A note either side of the blocks is not a strip.
        QCOMPARE(Mackie::decode(note(7, true)).kind, Mackie::Kind::None);
        QCOMPARE(Mackie::decode(note(24, true)).kind, Mackie::Kind::None);
    }

    /**
     * An encoder says how far it was turned and which way, not where it is.
     *
     * The part of this protocol that catches people out, and the reason the
     * knobs it drives can be turned past their old value rather than jumping
     * to wherever the hardware thinks it is pointing.
     */
    void anEncoderSendsMovementRatherThanAPosition()
    {
        const Mackie::Action up = Mackie::decode(controller(16, 0x01));
        QCOMPARE(up.kind, Mackie::Kind::Encoder);
        QCOMPARE(up.index, 0);
        QCOMPARE(up.delta, 1);

        // Bit six set is anticlockwise; the low six bits are the count.
        QCOMPARE(Mackie::decode(controller(16, 0x41)).delta, -1);
        // A fast spin is one message saying five, not five saying one.
        QCOMPARE(Mackie::decode(controller(19, 0x05)).delta, 5);
        QCOMPARE(Mackie::decode(controller(19, 0x05)).index, 3);
        QCOMPARE(Mackie::decode(controller(23, 0x45)).delta, -5);
        QCOMPARE(Mackie::decode(controller(23, 0x45)).index, 7);

        // A controller outside the block is somebody else's.
        QCOMPARE(Mackie::decode(controller(24, 0x01)).kind, Mackie::Kind::None);
        QCOMPARE(Mackie::decode(controller(7, 0x01)).kind, Mackie::Kind::None);
    }

    /** A fader is a channel's worth of pitch bend, and lands on 0 to 1. */
    void aFaderIsAWholeChannelsPitchBend()
    {
        const Mackie::Action bottom = Mackie::decode(bend(0, 0));
        QCOMPARE(bottom.kind, Mackie::Kind::Fader);
        QCOMPARE(bottom.index, 0);
        QCOMPARE(bottom.value, 0.0);

        const Mackie::Action top = Mackie::decode(bend(3, 16383));
        QCOMPARE(top.index, 3);
        QCOMPARE(top.value, 1.0);

        // Halfway, to within the resolution of fourteen bits.
        QVERIFY(qAbs(Mackie::decode(bend(1, 8192)).value - 0.5) < 0.001);
    }

    /** Most of MIDI is not Mackie, and saying so is the common case. */
    void whatIsNotTheProtocolIsNotDecoded()
    {
        MidiInput::Event middleC;
        middleC.kind = MidiInput::Event::Kind::NoteOn;
        middleC.data1 = 60;
        QCOMPARE(Mackie::decode(middleC).kind, Mackie::Kind::None);

        MidiInput::Event unknown;
        unknown.kind = MidiInput::Event::Kind::Other;
        QCOMPARE(Mackie::decode(unknown).kind, Mackie::Kind::None);
    }
};

QTEST_GUILESS_MAIN(MackieTest)
#include "mackietest.moc"
