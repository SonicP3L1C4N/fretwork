// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#pragma once

#include <QList>
#include <QString>

#include <memory>

/**
 * A chain of LV2 effects on one track's audio.
 *
 * This is the differentiator the whole project was written for. A tablature
 * program that plays a score is common; one where the guitar part goes through
 * an amplifier simulation of its own, the bass through a different one, and
 * both come out as separate stems is not. Everything before this -- a synth
 * per track, ports per track, stems -- was building the place to put it.
 *
 * **In-process with lilv, not delegated to Carla.** That was the architecture's
 * open question and this is the answer. Handing a chain to another process
 * means the audio leaving the callback and coming back: a second clock and a
 * buffer of latency per track, in a program whose ports are one node precisely
 * because there is one clock. Having argued that a node per part was wrong, a
 * process per chain would be the same mistake with more moving parts.
 *
 * **What runs in the callback.** `process` connects buffers and calls each
 * plugin's own run. It allocates nothing: every buffer a chain needs is sized
 * when it is built, for the largest block a graph will ever ask for.
 *
 * **Mono plugins are instantiated twice.** A great many guitar effects are one
 * in and one out, and a stereo track needs both sides treated. Two copies of a
 * mono plugin is what every host does and what a player would do with two
 * pedals; a plugin that is neither mono nor stereo is refused by name rather
 * than guessed at.
 */
namespace Lv2
{
/**
 * Where in a signal path a plugin belongs, in the order the signal goes.
 *
 * A pedalboard read left to right: what shapes the dynamics of the pickup
 * comes first, then what colours it, then the amplifier and the cabinet it
 * drives, and then the room -- modulation, delay and reverb -- which sound
 * wrong in front of a distortion and right behind one. Utilities are last
 * because they belong anywhere, and meters last of all because they change
 * nothing. A menu in this order is a menu somebody can build a rig from top
 * to bottom; one in alphabetical order puts a reverb between a preamp and a
 * ring modulator.
 */
enum class Section {
    Dynamics,       //< compressors, gates, expanders, limiters
    Filter,         //< wahs, envelope filters, tone controls before the drive
    Drive,          //< overdrive, distortion, fuzz, waveshapers
    Amplifier,      //< preamps and amplifier simulations
    Cabinet,        //< speaker simulations
    Eq,             //< graphic and parametric equalisers
    Modulation,     //< chorus, flanger, phaser, tremolo, vibrato, pitch
    Delay,
    Reverb,
    Utility,        //< gain, routing, delay lines, and anything unclassified
    Meter,          //< analysers: audio in, the same audio out, a reading
};

/** One plugin as the world describes it, before anything is instantiated. */
struct Description {
    QString uri;
    QString name;
    int audioInputs = 0;
    int audioOutputs = 0;

    /**
     * The plugin's class and its parent, as LV2 URIs.
     *
     * `http://lv2plug.in/ns/lv2core#DistortionPlugin` and the like. The
     * parent is kept because the useful classes are often one level up: a
     * chorus is a modulator, a parametric EQ is an EQ, and a stage is decided
     * at the level that says what the thing does to a signal.
     */
    QString classUri;
    QString parentClassUri;

    /** Whether this chain can use it: one in and one out, or two and two. */
    bool usable() const
    {
        return (audioInputs == 1 && audioOutputs == 1)
            || (audioInputs == 2 && audioOutputs == 2);
    }
};

/**
 * Every plugin installed, read once.
 *
 * Scanning the world is slow enough to be worth doing once and holding: lilv
 * parses every manifest on the machine, and there are a hundred and eighteen
 * bundles on an ordinary desktop.
 */
QList<Description> installed();

/** One plugin found by its URI, or a description with an empty uri. */
Description describe(const QString &uri);

/**
 * Where a plugin sits in a signal path, from what it says it is.
 *
 * The LV2 class decides it, at whichever level names what the plugin does:
 * a `ChorusPlugin` is modulation because its parent is, a `ParaEQPlugin` is
 * EQ for the same reason, and an `AmplifierPlugin` -- which the spec files
 * under dynamics because it is a gain -- is an amplifier, because every
 * plugin that claims the class is a preamp. LV2 has no class for a speaker
 * cabinet, so a simulator or anything else with "cabinet" in its name is one.
 * Anything with no class this recognises is a utility, which is honest: it
 * goes anywhere because nothing is known about where it goes.
 */
Section sectionOf(const Description &plugin);

/** The section as a heading for a menu. */
QString sectionName(Section section);

/** One knob on one plugin: what it is called, what it may be, and what it is. */
struct Control {
    uint32_t index = 0;
    QString symbol;
    QString name;

    float minimum = 0;
    float maximum = 1;
    float value = 0;

    /** A switch rather than a knob. */
    bool toggled = false;
    /** Whole numbers only. */
    bool integer = false;
    /** Turns as an ear hears, not as a number counts. */
    bool logarithmic = false;

    /**
     * What the number is in, where the plugin troubles to say.
     *
     * The symbol as the manifest gives it -- "dB", "Hz", "ms" -- and empty
     * where nothing was declared, which is the ordinary case rather than a
     * fault: twenty-eight of the hundred and eighteen bundles on this machine
     * declare a unit anywhere, and guitarix's amplifier describes nine
     * controls and a unit for none of them. Anything reading this must be as
     * legible without one as with, because usually there is not one.
     */
    QString unit;

    /**
     * The choices, where the control is a list rather than a range.
     *
     * A guitarix amplifier picks its valve model this way, and a slider from
     * nought to eleven labelled nothing would be a worse way to ask.
     */
    QStringList choices;
    QList<float> choiceValues;
};

/**
 * The knobs a plugin has, read from its manifest without loading it.
 *
 * For deciding what a setting would do before committing to it: fitting a
 * guitarix voicing needs the plugin's own list of valve models, and asking
 * that question should not cost an instantiation.
 */
QList<Control> controlsOf(const QString &uri);

/** One plugin in a chain, and the knobs on it. */
struct Stage {
    QString uri;
    QString name;
    QList<Control> controls;
};

class Chain
{
public:
    struct Options {
        int sampleRate = 48000;
        /** The largest block this will ever be asked for. */
        int maximumFrames = 8192;
    };

    /** Builds the chain in order; the first plugin is nearest the instrument. */
    Chain(const QStringList &uris, const Options &options);
    ~Chain();

    Chain(const Chain &) = delete;
    Chain &operator=(const Chain &) = delete;

    bool isValid() const;
    QString error() const;

    /** What actually loaded, in order. */
    QStringList loaded() const;

    /** Every plugin in the chain and every knob on it, in order. */
    QList<Stage> stages() const;

    /**
     * Turns one knob, from any thread.
     *
     * A control port is a float the plugin reads at the top of each block, so
     * this writes one. Every host does exactly that and none of them lock: a
     * torn read would be one block at a strange value on a machine where a
     * float write is not atomic, which is no machine this runs on.
     */
    void setControl(int stage, uint32_t index, float value);

    /** The same by the name the plugin gives it. False if there is no such knob. */
    bool setControl(int stage, const QString &symbol, float value);

    /** Runs the chain over a block, in place. Safe in an audio callback. */
    void process(float *left, float *right, int frames);

private:
    struct Private;
    std::unique_ptr<Private> d;
};
}
