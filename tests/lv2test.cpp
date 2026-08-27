// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "lv2chain.h"

#include <QTest>

#include <cmath>

/**
 * Hosting effects that belong to somebody else.
 *
 * Half of this can be tested anywhere and half of it cannot: what is installed
 * on a machine is a property of the machine, so the cases that need a plugin
 * skip where there is none rather than failing. That is the same bargain the
 * corpus tests make, and for the same reason -- a test that fails because a
 * package is absent is a test that trains people to ignore it.
 *
 * What is always true is the refusals. A chain asked for something that is not
 * there has to say so by name, because the alternative is a track that is
 * silently dry and a person turning knobs that do nothing.
 */
class Lv2Test : public QObject
{
    Q_OBJECT

private:
    /** The first plugin that can sit in a stereo chain, or an empty uri. */
    static Lv2::Description any(int inputs)
    {
        for (const Lv2::Description &plugin : Lv2::installed()) {
            if (plugin.audioInputs == inputs) {
                return plugin;
            }
        }
        return {};
    }

private Q_SLOTS:
    void saysSoWhenAPluginIsNotThere()
    {
        const Lv2::Chain chain({QStringLiteral("urn:fretwork:no-such-plugin")}, {});
        QVERIFY(!chain.isValid());
        QVERIFY2(chain.error().contains(QLatin1String("no-such-plugin")),
                 qPrintable(chain.error()));
    }

    void describesNothingAsNothing()
    {
        QVERIFY(Lv2::describe(QStringLiteral("urn:fretwork:nowhere")).uri.isEmpty());
    }

    void anEmptyChainIsNotAChain()
    {
        const Lv2::Chain chain({}, {});
        QVERIFY(!chain.isValid());
    }

    void onlyOffersWhatCanSitInAChain()
    {
        // A synth with no audio in and an analyser with no audio out are both
        // real plugins, and neither belongs between an instrument and a fader.
        for (const Lv2::Description &plugin : Lv2::installed()) {
            QVERIFY2(plugin.usable(), qPrintable(plugin.name));
            QVERIFY(!plugin.uri.isEmpty());
        }
    }

    void runsABlockThroughAMonoPlugin()
    {
        const Lv2::Description plugin = any(1);
        if (plugin.uri.isEmpty()) {
            QSKIP("no mono LV2 plugin installed on this machine");
        }

        Lv2::Chain::Options options;
        options.maximumFrames = 1024;
        Lv2::Chain chain({plugin.uri}, options);
        QVERIFY2(chain.isValid(), qPrintable(chain.error()));
        QCOMPARE(chain.loaded().size(), 1);

        // A tone in each channel, and something finite out of both: a mono
        // plugin is instantiated twice so that a stereo track keeps both
        // sides, and a chain that dropped one would be half a guitar.
        std::vector<float> left(1024), right(1024);
        for (int frame = 0; frame < 1024; ++frame) {
            left[size_t(frame)] = float(0.2 * std::sin(frame * 0.05));
            right[size_t(frame)] = float(0.2 * std::sin(frame * 0.07));
        }
        chain.process(left.data(), right.data(), 1024);

        for (int frame = 0; frame < 1024; ++frame) {
            QVERIFY(std::isfinite(left[size_t(frame)]));
            QVERIFY(std::isfinite(right[size_t(frame)]));
        }
    }

    void refusesABlockLargerThanItWasBuiltFor()
    {
        const Lv2::Description plugin = any(1);
        if (plugin.uri.isEmpty()) {
            QSKIP("no mono LV2 plugin installed on this machine");
        }
        Lv2::Chain::Options options;
        options.maximumFrames = 256;
        Lv2::Chain chain({plugin.uri}, options);
        QVERIFY(chain.isValid());

        // Silence rather than somebody else's memory. A plugin told it would
        // never see more than 256 frames and then handed 4096 is a plugin
        // writing past the end of its own buffers.
        std::vector<float> left(4096, 0.5f), right(4096, 0.5f);
        chain.process(left.data(), right.data(), 4096);
        QCOMPARE(left.at(0), 0.5f);
    }
};

QTEST_GUILESS_MAIN(Lv2Test)
#include "lv2test.moc"
