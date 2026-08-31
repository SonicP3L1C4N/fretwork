// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "audioinput.h"
#include "portedoutput.h"

#include <QTest>

#include <vector>

/**
 * The two ends of the graph: the ports a part is played out of, and the input
 * the tuner listens on.
 *
 * Both were named by the release assessment as untested, and both are awkward
 * to test for the same reason -- what they do is open a device on somebody's
 * machine. So this suite deliberately does not do that. It never opens the
 * default input, because that is a microphone and a test suite has no business
 * turning one on; and it never asks PortedOutput to connect itself to the
 * speakers, because a `ctest` run should not make a noise.
 *
 * What is left is still worth pinning, and is the same contract
 * `jacktransporttest` holds these classes to: whatever the machine has
 * installed, they never crash, they are never silent about which state they
 * ended up in, and what they report about themselves adds up.
 */
class AudioPortsTest : public QObject
{
    Q_OBJECT

private:
    static void nothing(void *, int, const PortedOutput::Transport &, float *const *,
                        float *const *)
    {
    }

private Q_SLOTS:
    /** No parts is no ports, and it says so rather than opening an empty node. */
    void portsForNothingAreRefusedAndExplained()
    {
        PortedOutput::Options options;
        options.autoConnect = false;
        const PortedOutput output(options, &nothing, nullptr);

        QVERIFY(!output.isValid());
        QVERIFY(!output.error().isEmpty());
        QCOMPARE(output.pairCount(), 0);
        QCOMPARE(output.linkCount(), 0);
    }

    /**
     * A pair per part, named after it -- or a refusal that says why.
     *
     * `autoConnect` is off throughout: the point of that flag is to plug the
     * node into the speakers, and this is a test suite.
     */
    void itIsEitherOpenWithAPairPerPartOrRefusedAndExplained()
    {
        PortedOutput::Options options;
        options.name = QStringLiteral("Fretwork test");
        options.ports = {QStringLiteral("Guitar"), QStringLiteral("Bass"),
                         QStringLiteral("Drums")};
        options.autoConnect = false;
        const PortedOutput output(options, &nothing, nullptr);

        if (output.isValid()) {
            QVERIFY(output.error().isEmpty());
            QCOMPARE(output.pairCount(), int(options.ports.size()));
            // Nothing was asked to be plugged in, so nothing is.
            QCOMPARE(output.linkCount(), 0);
        } else {
            QVERIFY(!output.error().isEmpty());
            // pairCount is what was asked for and stands whether or not the
            // graph gave them: a refusal must not also lose the question.
            QCOMPARE(output.pairCount(), int(options.ports.size()));
        }
    }

    /** Nothing has been read, so the transport says it does not know. */
    void aTransportNobodyIsDrivingIsNotGuessedAt()
    {
        const PortedOutput::Transport transport;
        QVERIFY(!transport.known);
        QVERIFY(!transport.rolling);
        QCOMPARE(transport.at, qint64(0));
    }

    /**
     * An input that is not there is refused by name.
     *
     * A device name nothing can match, so this opens no real capture stream on
     * anybody's machine -- and it is the path that matters most anyway, since
     * a tuner that silently fails to listen is a tuner that says every string
     * is fine.
     */
    void anInputThatIsNotThereIsRefusedAndExplained()
    {
        AudioInput::Options options;
        options.device = QStringLiteral("fretwork-test-no-such-input-42");
        const AudioInput input(options);

        if (!input.isValid()) {
            QVERIFY(!input.error().isEmpty());
        } else {
            // Some graphs hand out a default rather than refusing a name they
            // do not know. That is the graph's business; what this class must
            // not do is claim to be valid and describe itself as nothing.
            QVERIFY(input.sampleRate() > 0);
            QVERIFY(input.channelCount() > 0);
        }
    }

    /**
     * Reading from an input with no history writes nothing, and writes it
     * nowhere.
     *
     * The header promises 0 rather than a short read, and the caller sizes its
     * own buffer from the answer -- so a `latest()` that returned frames it had
     * not written, or wrote frames it had not promised, would be a buffer of
     * somebody else's memory in a pitch detector.
     */
    void readingAnInputWithNothingInItTouchesNothing()
    {
        AudioInput::Options options;
        options.device = QStringLiteral("fretwork-test-no-such-input-42");
        const AudioInput input(options);

        constexpr int Frames = 256;
        constexpr float Guard = -12345.0f;
        std::vector<float> buffer(Frames + 8, Guard);

        const int written = input.latest(buffer.data(), Frames);
        QVERIFY(written == 0 || written == Frames);

        // Whatever it said, it must not have written past what it was given.
        for (int index = Frames; index < Frames + 8; ++index) {
            QCOMPARE(buffer[size_t(index)], Guard);
        }
        if (written == 0) {
            // Nothing written means nothing touched.
            for (int index = 0; index < Frames; ++index) {
                QCOMPARE(buffer[size_t(index)], Guard);
            }
        }
    }

    /** Counters on a stream that never opened are nought, not rubbish. */
    void anInputThatNeverOpenedCountsNothing()
    {
        AudioInput::Options options;
        options.device = QStringLiteral("fretwork-test-no-such-input-42");
        const AudioInput input(options);

        if (!input.isValid()) {
            QVERIFY(!input.isRunning());
            QCOMPARE(input.framesCaptured(), qint64(0));
        }
    }
};

QTEST_GUILESS_MAIN(AudioPortsTest)
#include "audioportstest.moc"
