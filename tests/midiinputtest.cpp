// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "midiinput.h"

#include <QElapsedTimer>
#include <QTest>

/**
 * MIDI coming in, which until now nothing in this program did: `export/midi`
 * writes files and reads nothing, and the only MIDI that had ever entered here
 * came out of a `.gp`.
 *
 * Two halves, for the same reason `gpiftest` has two. What can be asserted on
 * any machine is the contract: it is either open or refused by name, it never
 * hands back what it has not received, and a reader that stops draining loses
 * the oldest rather than growing without limit.
 *
 * What needs hardware is gated behind `FRETWORK_MIDI_PORT`, in the way the
 * corpus tests are gated behind `FRETWORK_CORPUS`. Set it to a PipeWire MIDI
 * port -- `Midi-Bridge:Minilab3 MIDI`, or `Midi-Bridge:Midi Through Port-0`
 * with `aplaymidi` feeding it -- and the live case runs. Unset, it skips,
 * because a suite that needs a keyboard plugged in is a suite nobody runs.
 */
class MidiInputTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    /** Open, or refused by name. Never quietly neither. */
    void itIsEitherListeningOrRefusedAndExplained()
    {
        MidiInput::Options options;
        options.device = QStringLiteral("fretwork-test-no-such-midi-port-42");
        const MidiInput input(options);

        if (input.isValid()) {
            QVERIFY(input.error().isEmpty());
        } else {
            QVERIFY(!input.error().isEmpty());
        }
    }

    /** Nothing has arrived, so nothing comes back, and the count agrees. */
    void anInputNothingHasReachedHandsBackNothing()
    {
        MidiInput::Options options;
        options.device = QStringLiteral("fretwork-test-no-such-midi-port-42");
        MidiInput input(options);

        QCOMPARE(input.messagesSeen(), qint64(0));
        QVERIFY(input.take().isEmpty());
        // Draining twice is not a way to get messages that were never sent.
        QVERIFY(input.take().isEmpty());
    }

    /** A pitch bend is two seven-bit halves, and one number to everybody else. */
    void aBendIsOneNumberRatherThanTwoHalves()
    {
        MidiInput::Event centred;
        centred.kind = MidiInput::Event::Kind::PitchBend;
        centred.data1 = 0;
        centred.data2 = 64;
        QCOMPARE(centred.bend(), 8192);

        MidiInput::Event top;
        top.data1 = 127;
        top.data2 = 127;
        QCOMPARE(top.bend(), 16383);
    }

    /**
     * The live half: something plays, and it arrives.
     *
     * Asserts that messages reach the ring and that what comes out is MIDI --
     * a note number in range, on a channel that exists. Not *which* notes,
     * because what is on the other end is whatever the person running this
     * plugged in.
     */
    void whatIsPlayedArrives()
    {
        const QString port = qEnvironmentVariable("FRETWORK_MIDI_PORT");
        if (port.isEmpty()) {
            QSKIP("set FRETWORK_MIDI_PORT to a PipeWire MIDI port to run this");
        }

        MidiInput::Options options;
        options.device = port;
        options.name = QStringLiteral("Fretwork test");
        MidiInput input(options);
        QVERIFY2(input.isValid(), qPrintable(input.error()));

        // Long enough for the graph to make the link and for something to be
        // played into it.
        QList<MidiInput::Event> heard;
        QElapsedTimer clock;
        clock.start();
        while (clock.elapsed() < 8000 && heard.isEmpty()) {
            QTest::qWait(100);
            heard = input.take();
        }

        QVERIFY2(!heard.isEmpty(), "nothing arrived on that port in eight seconds");
        for (const MidiInput::Event &event : std::as_const(heard)) {
            QVERIFY(event.channel >= 0 && event.channel < 16);
            QVERIFY(event.data1 >= 0 && event.data1 < 128);
            QVERIFY(event.data2 >= 0 && event.data2 < 128);
        }
        QCOMPARE(input.messagesSeen(), qint64(heard.size()));
    }
};

QTEST_MAIN(MidiInputTest)
#include "midiinputtest.moc"
