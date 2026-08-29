// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#pragma once

#include "lv2chain.h"

#include <QList>
#include <QString>
#include <QStringList>

/**
 * Amplifier voicings lifted out of a guitarix bank.
 *
 * A chain and a sound are different things. Fretwork can already put gx_amp on
 * a track, and what comes out is gx_amp at its defaults -- an amplifier nobody
 * has turned up, which is not a tone, it is a starting point. Guitarix ships
 * nineteen presets named after the records they are aiming at, and those are
 * somebody's ears deciding where the knobs go.
 *
 * **What this deliberately does not do.** A `.gx` preset describes the whole
 * guitarix rig: forty modules, of which the ones the factory bank leans on
 * hardest -- `shaper` in nine presets, the `jconv` convolver in nine,
 * `stereoverb` in six -- ship no LV2 plugin at all. Loading one of those and
 * calling the result "Bass - Come Together" would be a sound that is not that
 * sound, announced as though it were. So this carries the amplifier and says
 * out loud what it left behind, which is the same bargain the rest of the
 * program makes: mark it rather than correct it.
 *
 * **The mapping is the plugin's, not this file's.** gx_amp declares its valve,
 * tone stack and cabinet as LV2 enumerations with the labels spelled the way
 * the bank spells them -- `6V6`, `12ax7`, `Bassman` against `Bassman Style`.
 * `fit` matches against the choices the plugin itself reports, so a plugin
 * that gains a model gains it here too, and nothing is hardcoded that the
 * plugin could be asked instead.
 */
namespace Gx
{
/** One voicing as the bank writes it down, before any plugin is consulted. */
struct Voicing {
    QString name;               //< "Bass - Come Together"
    QString bank;               //< the file it came out of, as a person would say it

    // The three choices, as strings, because that is how the bank stores them
    // and the plugin is the only thing that knows what they are worth.
    QString valve;              //< gx: tube.select
    QString toneStack;          //< gx: amp.tonestack.select
    QString cabinet;            //< gx: cab.select

    // The tone stack, nought to one, which is gx_amp's range as well.
    double bass = 0.5;
    double middle = 0.5;
    double treble = 0.5;

    double preGainDb = 0;       //< gx: amp.out_amp
    double masterGainDb = 0;    //< gx: amp.out_master

    /**
     * The modules this voicing switches on besides the amplifier, in rig order.
     *
     * Kept so that the window can say what it is not carrying. A voicing that
     * is mostly a convolver and a reverb is worth choosing differently from
     * one that is mostly an amplifier, and the only way to tell is to look.
     */
    QStringList rig;

    /**
     * Whether an amplifier is part of this sound at all.
     *
     * The three acoustic presets set `tube.select` to `noamp`: they are a
     * cabinet and an equaliser, and putting them on gx_amp would be answering
     * a question the preset did not ask.
     */
    bool usesAmplifier() const
    {
        return !valve.isEmpty() && valve != QLatin1String("noamp");
    }
};

/** Every `.gx` bank on the machine, factory settings and the user's own. */
QStringList banks();

/** Reads a bank. Empty, with `error` set, where it cannot. */
QList<Voicing> read(const QString &path, QString *error = nullptr);

/**
 * Parses bank text already in hand.
 *
 * Separate from `read` for the same reason the SFZ parser is: a bank that
 * behaved oddly can be pasted into a test without shipping the file.
 */
QList<Voicing> parse(const QByteArray &text, const QString &bank, QString *error = nullptr);

/** One knob to turn, by the name the plugin gives it. */
struct Setting {
    QString symbol;
    float value = 0;
};

/**
 * What a voicing becomes on a particular plugin, and what it could not become.
 *
 * `declined` is the point of the type. A fitting that quietly dropped the
 * cabinet because no name matched would be indistinguishable from one that
 * worked, and the person listening would have no way to know which they had.
 */
struct Fitting {
    QList<Setting> settings;
    QStringList declined;

    bool isEmpty() const
    {
        return settings.isEmpty();
    }
};

/**
 * Fits a voicing to the knobs a plugin actually has.
 *
 * Anything out of the plugin's range is declined rather than clamped: a bank
 * that asks for -32 dB on a control that stops at -20 is not asking for -20,
 * and a program that silently gave it -20 would be inventing a mix.
 */
Fitting fit(const Voicing &voicing, const QList<Lv2::Control> &controls);

/**
 * What a name resolved to, and what to say when it resolved to nothing.
 *
 * The list is a fixture of the machine rather than of the score, so both the
 * window and the command line are asking the same question of the same
 * nineteen presets. They used not to ask it the same way: `--voicing
 * 0:0=Iron Man` set the amplifier for `--render` and did nothing at all when
 * the window opened, because one end resolved fragments and the other wanted
 * the name exactly, and the exact name is "Distortion - Iron Man". A silent
 * nothing is the worst of the three possible answers, and it was the one the
 * window gave.
 */
struct Match {
    enum Outcome {
        Found,          //< exactly one voicing answers to this
        Unknown,        //< nothing does
        Ambiguous,      //< several do, and choosing between them is not this code's business
    };

    Outcome outcome = Unknown;
    Voicing voicing;
    QStringList candidates;     //< when ambiguous, all of them, so the caller can name them
};

/**
 * A voicing by name: exactly, then ignoring case, then as a fragment.
 *
 * "Bass - Come Together" is a lot to type accurately, so `rising` finds the
 * one about the house. A fragment matching two is reported as ambiguous rather
 * than resolved to the first, because picking one would be picking somebody's
 * amplifier for them.
 */
Match named(const QList<Voicing> &voicings, const QString &wanted);
}
