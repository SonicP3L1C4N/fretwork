// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "jacktransport.h"
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
    /**
     * A chain naming a plugin nobody has refuses, and says which one.
     *
     * Where this copy was built without lilv there is no particular plugin to
     * be missing: the refusal is that it hosts no effects at all, which is the
     * honest answer and the only one that helps somebody on that build. The
     * thing being tested is the same either way -- that it announces the
     * refusal rather than going quietly dry -- so the assertion follows the
     * build rather than accepting whichever message turns up.
     */
    void saysSoWhenAPluginIsNotThere()
    {
        const Lv2::Chain chain({QStringLiteral("urn:fretwork:no-such-plugin")}, {});
        QVERIFY(!chain.isValid());
#ifdef FRETWORK_HAVE_LILV
        QVERIFY2(chain.error().contains(QLatin1String("no-such-plugin")),
                 qPrintable(chain.error()));
#else
        QVERIFY2(chain.error().contains(QLatin1String("lilv")),
                 qPrintable(chain.error()));
#endif
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

    /**
     * Where a plugin goes is read off its class, at whichever level of the
     * class tree says what it does, with the two exceptions the spec forces:
     * an amplifier is not a fader, and a cabinet is not a class at all.
     */
    void aPluginKnowsWhereItGoesInTheSignalPath()
    {
        const auto described = [](const char *klass, const char *parent, const QString &name) {
            Lv2::Description plugin;
            plugin.name = name;
            plugin.classUri = QStringLiteral("http://lv2plug.in/ns/lv2core#") + QLatin1String(klass);
            if (parent) {
                plugin.parentClassUri =
                    QStringLiteral("http://lv2plug.in/ns/lv2core#") + QLatin1String(parent);
            }
            return plugin;
        };
        using Lv2::Section;
        QCOMPARE(Lv2::sectionOf(described("DistortionPlugin", "Plugin", QStringLiteral("GxRat"))), Section::Drive);
        QCOMPARE(Lv2::sectionOf(described("CompressorPlugin", "DynamicsPlugin", QStringLiteral("GxCompressor"))), Section::Dynamics);
        // By the parent, where the class itself says nothing this knows.
        QCOMPARE(Lv2::sectionOf(described("ChorusPlugin", "ModulatorPlugin", QStringLiteral("GxChorus"))), Section::Modulation);
        // An EQ is a filter to the spec and a different stage to a guitarist.
        QCOMPARE(Lv2::sectionOf(described("ParaEQPlugin", "EQPlugin", QStringLiteral("x42-eq"))), Section::Eq);
        QCOMPARE(Lv2::sectionOf(described("EQPlugin", "FilterPlugin", QStringLiteral("GxGraphicEQ"))), Section::Eq);
        QCOMPARE(Lv2::sectionOf(described("FilterPlugin", "Plugin", QStringLiteral("GxWah"))), Section::Filter);
        // An amplifier is filed under dynamics by the spec and is a preamp in
        // every plugin that claims it.
        QCOMPARE(Lv2::sectionOf(described("AmplifierPlugin", "DynamicsPlugin", QStringLiteral("Gxjcm800pre"))), Section::Amplifier);
        QCOMPARE(Lv2::sectionOf(described("SimulatorPlugin", "Plugin", QStringLiteral("GxAmplifier-X"))), Section::Amplifier);
        // A cabinet is a simulator with no class of its own.
        QCOMPARE(Lv2::sectionOf(described("SimulatorPlugin", "Plugin", QStringLiteral("GxCabinet"))), Section::Cabinet);
        QCOMPARE(Lv2::sectionOf(described("ReverbPlugin", "Plugin", QStringLiteral("Cab IR loader"))), Section::Cabinet);
        QCOMPARE(Lv2::sectionOf(described("ReverbPlugin", "Plugin", QStringLiteral("x42 - IR Convolver"))), Section::Reverb);
        QCOMPARE(Lv2::sectionOf(described("PitchPlugin", "SpectralPlugin", QStringLiteral("Gxdetune"))), Section::Modulation);
        QCOMPARE(Lv2::sectionOf(described("DelayPlugin", "Plugin", QStringLiteral("GxDelay"))), Section::Delay);
        QCOMPARE(Lv2::sectionOf(described("AnalyserPlugin", "UtilityPlugin", QStringLiteral("BBC Meter"))), Section::Meter);
        QCOMPARE(Lv2::sectionOf(described("UtilityPlugin", "Plugin", QStringLiteral("GxBooster"))), Section::Utility);
        // Nothing known about it is nothing known about where it goes.
        QCOMPARE(Lv2::sectionOf(Lv2::Description{}), Section::Utility);

        for (const Lv2::Section section : {Section::Dynamics, Section::Filter, Section::Drive, Section::Amplifier,
                                       Section::Cabinet, Section::Eq, Section::Modulation, Section::Delay,
                                       Section::Reverb, Section::Utility, Section::Meter}) {
            QVERIFY(!Lv2::sectionName(section).isEmpty());
        }
    }

    /** The list is in the order a rig is built, and by name within a section. */
    void offersPluginsInTheOrderTheSignalGoes()
    {
        const QList<Lv2::Description> found = Lv2::installed();
        for (int index = 1; index < found.size(); ++index) {
            const Lv2::Section before = Lv2::sectionOf(found.at(index - 1));
            const Lv2::Section here = Lv2::sectionOf(found.at(index));
            QVERIFY2(before <= here, qPrintable(found.at(index).name));
            if (before == here) {
                QVERIFY2(found.at(index - 1).name.localeAwareCompare(found.at(index).name) <= 0,
                         qPrintable(found.at(index).name));
            }
        }
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

    void readsTheKnobsAPluginSaysItHas()
    {
        const Lv2::Description plugin = any(1);
        if (plugin.uri.isEmpty()) {
            QSKIP("no mono LV2 plugin installed on this machine");
        }
        Lv2::Chain chain({plugin.uri}, {});
        QVERIFY(chain.isValid());
        // Held in a local. `stages()` hands back a list by value, and ranging
        // over something reached through it destroys the list before the loop
        // body runs -- which is a dangling reference and, here, a crash.
        const QList<Lv2::Stage> stages = chain.stages();
        QCOMPARE(stages.size(), 1);

        for (const Lv2::Control &control : stages.first().controls) {
            // Every knob has a name, a range that is a range, and a value
            // inside it: a control starting outside its own bounds is a
            // plugin about to be asked for something it cannot do.
            QVERIFY(!control.name.isEmpty());
            QVERIFY(control.maximum >= control.minimum);
            QVERIFY(control.value >= control.minimum);
            QVERIFY(control.value <= control.maximum);
            QCOMPARE(control.choices.size(), control.choiceValues.size());
        }
    }

    void turningAKnobSticks()
    {
        // Something with a knob to turn, whatever is installed.
        for (const Lv2::Description &plugin : Lv2::installed()) {
            Lv2::Chain chain({plugin.uri}, {});
            if (!chain.isValid()) {
                continue;
            }
            const QList<Lv2::Stage> stages = chain.stages();
            if (stages.isEmpty() || stages.first().controls.isEmpty()) {
                continue;
            }
            const Lv2::Control knob = stages.first().controls.first();
            if (knob.maximum <= knob.minimum) {
                continue;
            }

            const auto valueNow = [&chain] {
                return chain.stages().first().controls.first().value;
            };
            const float wanted = knob.minimum + (knob.maximum - knob.minimum) * 0.75f;
            chain.setControl(0, knob.index, wanted);
            QCOMPARE(valueNow(), wanted);

            // And by the name the plugin gives it, which is how a saved
            // setting would have to find it again.
            QVERIFY(chain.setControl(0, knob.symbol, knob.minimum));
            QCOMPARE(valueNow(), knob.minimum);
            QVERIFY(!chain.setControl(0, QStringLiteral("no_such_knob"), 1));
            return;
        }
        QSKIP("no LV2 plugin with a control installed on this machine");
    }

    // ---- driving the graph's transport ----

    void findsATransportOrSaysWhyNot()
    {
        // Machine-dependent like the plugins above: what this asserts is that
        // it is never ambiguous. Either there is a transport to drive and it
        // says which library answered, or there is not and it says so. A
        // silent nothing would be a play button that does nothing.
        const JackTransport transport;
        if (transport.isValid()) {
            QVERIFY2(!transport.library().isEmpty(), "drove a transport from nowhere");
        } else {
            QVERIFY2(!transport.error().isEmpty(), "no transport and no reason given");
        }
    }
};

QTEST_GUILESS_MAIN(Lv2Test)
#include "lv2test.moc"
