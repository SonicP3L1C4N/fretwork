// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "gxpreset.h"
#include "lv2chain.h"

#include <QTest>

/**
 * Reading guitarix banks, and fitting what they say onto a plugin.
 *
 * The fitting is the half worth testing hardest. Parsing a bank wrongly gives
 * a preset that is obviously missing; fitting one wrongly gives a preset that
 * loads, sounds wrong, and says nothing -- so every case where a value cannot
 * be carried is asserted to be *declined*, not merely absent.
 */
class GxPresetTest : public QObject
{
    Q_OBJECT

private:
    /** gx_amp's knobs, as the plugin reports them, trimmed to what fit() reads. */
    static QList<Lv2::Control> amplifier()
    {
        Lv2::Control model;
        model.symbol = QStringLiteral("model");
        model.maximum = 18;
        model.choices = {QStringLiteral("12ax7"), QStringLiteral("6V6")};
        model.choiceValues = {0, 5};

        Lv2::Control toneStack;
        toneStack.symbol = QStringLiteral("t_model");
        toneStack.maximum = 26;
        toneStack.choices = {QStringLiteral("default"), QStringLiteral("Bassman Style")};
        toneStack.choiceValues = {0, 1};

        Lv2::Control cabinet;
        cabinet.symbol = QStringLiteral("c_model");
        cabinet.maximum = 18;
        cabinet.choices = {QStringLiteral("4x12"), QStringLiteral("Mesa Style")};
        cabinet.choiceValues = {0, 13};

        auto knob = [](const char *symbol, float minimum, float maximum) {
            Lv2::Control control;
            control.symbol = QLatin1String(symbol);
            control.minimum = minimum;
            control.maximum = maximum;
            return control;
        };

        return {model, toneStack, cabinet, knob("Bass", 0, 1), knob("Middle", 0, 1),
                knob("Treble", 0, 1), knob("PreGain", -20, 20), knob("MasterGain", -20, 20)};
    }

    static float valueOf(const Gx::Fitting &fitting, const char *symbol)
    {
        for (const Gx::Setting &setting : fitting.settings) {
            if (setting.symbol == QLatin1String(symbol))
                return setting.value;
        }
        return -12345;
    }

    static bool mentions(const QStringList &lines, const char *word)
    {
        for (const QString &line : lines) {
            if (line.contains(QLatin1String(word)))
                return true;
        }
        return false;
    }

    static QByteArray bank(const QByteArray &engine)
    {
        return "[\"gx_head_file_version\", [1, 2, \"0.44.1\"], \"A Sound\", {\"engine\": {" + engine
            + "}}]";
    }

private Q_SLOTS:
    void readsAPreset()
    {
        const QList<Gx::Voicing> voicings = Gx::parse(bank(R"(
            "tube.select": "6V6",
            "amp.tonestack.select": "Bassman",
            "cab.select": "4x12",
            "amp.tonestack.Bass": 0.595,
            "amp.tonestack.Treble": 0.5,
            "amp.out_master": -8
        )"), QStringLiteral("Test"));

        QCOMPARE(voicings.size(), 1);
        QCOMPARE(voicings.first().name, QStringLiteral("A Sound"));
        QCOMPARE(voicings.first().bank, QStringLiteral("Test"));
        QCOMPARE(voicings.first().valve, QStringLiteral("6V6"));
        QCOMPARE(voicings.first().bass, 0.595);
        QCOMPARE(voicings.first().middle, 0.5);   //< absent means the middle of the range
        QCOMPARE(voicings.first().masterGainDb, -8.0);
        QVERIFY(voicings.first().usesAmplifier());
    }

    void readsTheRigInSignalOrder()
    {
        const QList<Gx::Voicing> voicings = Gx::parse(bank(R"(
            "echo.on_off": 1, "echo.position": 40,
            "jconv.on_off": 1, "jconv.position": 12,
            "chorus.on_off": 0, "chorus.position": 3,
            "amp.clip.on_off": 1, "amp.clip.position": 8
        )"), QStringLiteral("Test"));

        QCOMPARE(voicings.size(), 1);
        // Ordered by position, the switched-off one absent, and the amplifier's
        // own parts left out -- they are the amp, not something it is missing.
        QCOMPARE(voicings.first().rig, QStringList({QStringLiteral("jconv"), QStringLiteral("echo")}));
    }

    void fitsWhatThePluginDeclares()
    {
        Gx::Voicing voicing;
        voicing.valve = QStringLiteral("6V6");
        voicing.toneStack = QStringLiteral("Bassman");   //< the plugin says "Bassman Style"
        voicing.cabinet = QStringLiteral("4x12");
        voicing.bass = 0.595;
        voicing.masterGainDb = -8;

        const Gx::Fitting fitting = Gx::fit(voicing, amplifier());
        QCOMPARE(valueOf(fitting, "model"), 5.0f);
        QCOMPARE(valueOf(fitting, "t_model"), 1.0f);     //< matched across the suffix
        QCOMPARE(valueOf(fitting, "c_model"), 0.0f);
        QCOMPARE(valueOf(fitting, "Bass"), 0.595f);
        QCOMPARE(valueOf(fitting, "MasterGain"), -8.0f);
        QVERIFY(fitting.declined.isEmpty());
    }

    void declinesAModelItCannotName()
    {
        Gx::Voicing voicing;
        voicing.valve = QStringLiteral("6V6");
        voicing.cabinet = QStringLiteral("Mesa Boogie");   //< the plugin offers "Mesa Style"

        const Gx::Fitting fitting = Gx::fit(voicing, amplifier());
        QCOMPARE(valueOf(fitting, "c_model"), -12345);     //< not set to anything
        QVERIFY(mentions(fitting.declined, "Mesa Boogie"));
    }

    void refusesRatherThanClamps()
    {
        Gx::Voicing voicing;
        voicing.valve = QStringLiteral("6V6");
        voicing.masterGainDb = -32.2;   //< the plugin floor is -20

        const Gx::Fitting fitting = Gx::fit(voicing, amplifier());
        QCOMPARE(valueOf(fitting, "MasterGain"), -12345);
        QVERIFY(mentions(fitting.declined, "-32.2"));
        QVERIFY(mentions(fitting.declined, "-20"));
        // And says what the knob is therefore left at, because "declined" on a
        // master level means "louder than asked for" and should read that way.
        QVERIFY(mentions(fitting.declined, "stays at"));
    }

    void saysWhenTheAmplifierIsNotThePoint()
    {
        Gx::Voicing voicing;
        voicing.valve = QStringLiteral("noamp");
        voicing.cabinet = QStringLiteral("4x12");

        QVERIFY(!voicing.usesAmplifier());
        const Gx::Fitting fitting = Gx::fit(voicing, amplifier());
        QCOMPARE(valueOf(fitting, "model"), -12345);
        QVERIFY(mentions(fitting.declined, "switches the amplifier off"));
        // The cabinet still fits: it is the part of the sound that survives.
        QCOMPARE(valueOf(fitting, "c_model"), 0.0f);
    }

    void saysWhatItLeavesBehind()
    {
        Gx::Voicing voicing;
        voicing.valve = QStringLiteral("6V6");
        voicing.rig = {QStringLiteral("jconv"), QStringLiteral("stereoverb")};

        const Gx::Fitting fitting = Gx::fit(voicing, amplifier());
        QVERIFY(mentions(fitting.declined, "Not carried"));
        QVERIFY(mentions(fitting.declined, "jconv"));
    }

    /**
     * The mapping against the plugin itself, not a hand-written stand-in.
     *
     * The stub above proves the matching rules; this proves they are the rules
     * gx_amp actually needs. If guitarix renames a valve in a later release,
     * this is what notices.
     */
    void fitsTheRealAmplifier()
    {
        const QStringList banks = Gx::banks();
        if (banks.isEmpty()) {
            QSKIP("guitarix is not installed");
        }
        if (Lv2::describe(QStringLiteral("http://guitarix.sourceforge.net/plugins/gx_amp#GUITARIX")).uri.isEmpty()) {
            QSKIP("the gx_amp plugin is not installed");
        }

        Lv2::Chain::Options options;
        Lv2::Chain chain({QStringLiteral("http://guitarix.sourceforge.net/plugins/gx_amp#GUITARIX")},
                         options);
        QVERIFY2(chain.isValid(), qPrintable(chain.error()));
        const QList<Lv2::Control> controls = chain.stages().first().controls;

        int amplified = 0;
        int carried = 0;
        const QList<Gx::Voicing> voicings = Gx::read(banks.first());
        for (const Gx::Voicing &voicing : voicings) {
            const Gx::Fitting fitting = Gx::fit(voicing, controls);
            carried += fitting.settings.size();
            if (voicing.usesAmplifier()) {
                ++amplified;
                // A voicing with an amplifier must at least name a valve the
                // plugin knows; that is the whole claim being made here.
                bool hasValve = false;
                for (const Gx::Setting &setting : fitting.settings) {
                    hasValve = hasValve || setting.symbol == QStringLiteral("model");
                }
                QVERIFY2(hasValve, qPrintable(voicing.name + QStringLiteral(": ")
                                              + fitting.declined.join(QStringLiteral(" "))));
            }
        }
        qInfo("%d voicings, %d with an amplifier, %d knobs carried", int(voicings.size()),
              amplified, carried);
        QVERIFY(carried > 5 * voicings.size());
    }

    /**
     * A plugin's named choices come back in order, and paired with their own
     * numbers.
     *
     * lilv reports scale points in whatever order the RDF parsed in, which for
     * gx_amp put the valve numbered 1 before the valve numbered 0. Anything
     * pairing a label with a value by position -- a menu sending back which
     * row was clicked, a panel showing the name at the value's index -- picks
     * the wrong amplifier, quietly. This is what stops that coming back.
     */
    void ordersNamedChoicesByTheirOwnValue()
    {
        const QString uri =
            QStringLiteral("http://guitarix.sourceforge.net/plugins/gx_amp#GUITARIX");
        if (Lv2::describe(uri).uri.isEmpty()) {
            QSKIP("the gx_amp plugin is not installed");
        }
        Lv2::Chain::Options options;
        Lv2::Chain chain({uri}, options);
        QVERIFY(chain.isValid());

        // Held in a local: `chain.stages()` is a temporary, and a range-for
        // over a member of one walks memory that has already gone.
        const QList<Lv2::Stage> stages = chain.stages();
        int enumerations = 0;
        for (const Lv2::Control &control : stages.first().controls) {
            if (control.choices.isEmpty()) {
                continue;
            }
            ++enumerations;
            QCOMPARE(control.choices.size(), control.choiceValues.size());
            for (int i = 1; i < control.choiceValues.size(); ++i) {
                QVERIFY2(control.choiceValues.at(i) > control.choiceValues.at(i - 1),
                         qPrintable(control.symbol));
            }
        }
        QVERIFY(enumerations >= 3);

        // And the amplifier's own defaults are the manifest's, so a fresh
        // chain shows what a fresh chain is.
        const QList<Lv2::Control> manifest = Lv2::controlsOf(uri);
        for (const Lv2::Control &live : stages.first().controls) {
            for (const Lv2::Control &same : manifest) {
                if (same.symbol == live.symbol) {
                    QCOMPARE(same.value, live.value);
                }
            }
        }
    }

    void readsTheFactoryBankIfItIsInstalled()
    {
        const QStringList banks = Gx::banks();
        if (banks.isEmpty())
            QSKIP("guitarix is not installed");

        QString error;
        const QList<Gx::Voicing> voicings = Gx::read(banks.first(), &error);
        QVERIFY2(!voicings.isEmpty(), qPrintable(error));

        // Every preset is named, and every one fits something on a real amp.
        for (const Gx::Voicing &voicing : voicings) {
            QVERIFY(!voicing.name.isEmpty());
            QVERIFY(!Gx::fit(voicing, amplifier()).isEmpty());
        }
    }
};

QTEST_GUILESS_MAIN(GxPresetTest)
#include "gxpresettest.moc"
