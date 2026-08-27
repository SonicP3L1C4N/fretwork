// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "gxpreset.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QStandardPaths>

#include <algorithm>

namespace
{
/** Where guitarix keeps banks: the package's, then anything the user saved. */
QStringList bankDirectories()
{
    QStringList directories{QStringLiteral("/usr/share/gx_head/factorysettings"),
                            QStringLiteral("/usr/local/share/gx_head/factorysettings")};

    const QString config = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    if (!config.isEmpty()) {
        directories << config + QStringLiteral("/guitarix/banks");
        directories << config + QStringLiteral("/guitarix");
    }
    return directories;
}

double number(const QJsonObject &engine, const char *key, double fallback)
{
    const QJsonValue value = engine.value(QLatin1String(key));
    return value.isDouble() ? value.toDouble() : fallback;
}

QString text(const QJsonObject &engine, const char *key)
{
    return engine.value(QLatin1String(key)).toString();
}

/**
 * The modules a voicing switches on, in the order the signal meets them.
 *
 * The bank records a `position` per module for exactly this, and the amp's own
 * parts are left out because they are not things the amp is missing -- they
 * are the amp.
 */
QStringList rigOf(const QJsonObject &engine)
{
    QList<QPair<double, QString>> modules;
    for (auto it = engine.begin(); it != engine.end(); ++it) {
        const QString key = it.key();
        if (!key.endsWith(QLatin1String(".on_off")) || it.value().toInt() != 1)
            continue;

        const QString module = key.left(key.size() - 7);
        if (module.startsWith(QLatin1String("amp.")) || module == QLatin1String("amp")
            || module == QLatin1String("tube") || module == QLatin1String("cab")) {
            continue;
        }
        modules.append({number(engine, (module + QStringLiteral(".position")).toLatin1().constData(), 999),
                        module});
    }

    std::sort(modules.begin(), modules.end(),
              [](const auto &a, const auto &b) { return a.first < b.first; });

    QStringList names;
    names.reserve(modules.size());
    for (const auto &module : std::as_const(modules))
        names << module.second;
    return names;
}

/** Finds a knob by the name the plugin gives it. */
const Lv2::Control *controlFor(const QList<Lv2::Control> &controls, QLatin1String symbol)
{
    for (const Lv2::Control &control : controls) {
        if (control.symbol == symbol)
            return &control;
    }
    return nullptr;
}

/**
 * Turns a bank's word for a model into the number the plugin wants.
 *
 * Three passes, narrowing: the label as written, then the label with the
 * plugin's own " Style" suffix taken off -- gx_amp says "Bassman Style" where
 * the bank says "Bassman" -- then letter case. What none of them match is not
 * guessed at: "Mesa Boogie" against a list offering "Mesa Style" is a question
 * with two defensible answers, and a program that picked one would be picking
 * somebody's amplifier for them.
 */
bool choose(const Lv2::Control &control, const QString &wanted, float *value)
{
    const int count = std::min(control.choices.size(), control.choiceValues.size());

    for (int i = 0; i < count; ++i) {
        if (control.choices.at(i) == wanted) {
            *value = control.choiceValues.at(i);
            return true;
        }
    }
    for (int i = 0; i < count; ++i) {
        QString label = control.choices.at(i);
        if (label.endsWith(QLatin1String(" Style")))
            label.chop(6);
        if (label == wanted) {
            *value = control.choiceValues.at(i);
            return true;
        }
    }
    for (int i = 0; i < count; ++i) {
        if (control.choices.at(i).compare(wanted, Qt::CaseInsensitive) == 0) {
            *value = control.choiceValues.at(i);
            return true;
        }
    }
    return false;
}

QString rounded(double value)
{
    return QString::number(value, 'g', 3);
}
}

namespace Gx
{
QStringList banks()
{
    QStringList paths;
    const QStringList directories = bankDirectories();
    for (const QString &directory : directories) {
        const QFileInfoList files =
            QDir(directory).entryInfoList({QStringLiteral("*.gx")}, QDir::Files, QDir::Name);
        for (const QFileInfo &file : files)
            paths << file.absoluteFilePath();
    }
    paths.removeDuplicates();
    return paths;
}

QList<Voicing> read(const QString &path, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("Cannot open %1: %2").arg(path, file.errorString());
        return {};
    }
    return parse(file.readAll(), QFileInfo(path).completeBaseName(), error);
}

QList<Voicing> parse(const QByteArray &text, const QString &bank, QString *error)
{
    QJsonParseError trouble;
    const QJsonDocument document = QJsonDocument::fromJson(text, &trouble);
    if (trouble.error != QJsonParseError::NoError) {
        if (error)
            *error = QStringLiteral("%1 is not a readable bank: %2").arg(bank, trouble.errorString());
        return {};
    }
    if (!document.isArray()) {
        if (error)
            *error = QStringLiteral("%1 is not a bank.").arg(bank);
        return {};
    }

    // A bank is a flat array: a version marker, its version, then a name and a
    // settings object for each preset, over and over.
    const QJsonArray array = document.array();
    QList<Voicing> voicings;
    for (int i = 0; i + 1 < array.size(); ++i) {
        if (!array.at(i).isString() || !array.at(i + 1).isObject())
            continue;

        const QJsonObject engine = array.at(i + 1).toObject().value(QStringLiteral("engine")).toObject();
        if (engine.isEmpty())
            continue;

        Voicing voicing;
        voicing.name = array.at(i).toString();
        voicing.bank = bank;
        voicing.valve = ::text(engine, "tube.select");
        voicing.toneStack = ::text(engine, "amp.tonestack.select");
        voicing.cabinet = ::text(engine, "cab.select");
        voicing.bass = number(engine, "amp.tonestack.Bass", 0.5);
        voicing.middle = number(engine, "amp.tonestack.Middle", 0.5);
        voicing.treble = number(engine, "amp.tonestack.Treble", 0.5);
        voicing.preGainDb = number(engine, "amp.out_amp", 0);
        voicing.masterGainDb = number(engine, "amp.out_master", 0);
        voicing.rig = rigOf(engine);
        voicings.append(voicing);
        ++i;
    }

    if (voicings.isEmpty() && error)
        *error = QStringLiteral("%1 holds no presets.").arg(bank);
    return voicings;
}

Fitting fit(const Voicing &voicing, const QList<Lv2::Control> &controls)
{
    Fitting fitting;

    // A knob the plugin does not have is not a failure worth reporting: it
    // means this is not gx_amp, and the caller is about to find that out from
    // an empty fitting anyway.
    const auto continuous = [&](QLatin1String symbol, double value, const QString &called,
                                const QString &unit) {
        const Lv2::Control *control = controlFor(controls, symbol);
        if (!control)
            return;

        if (value < control->minimum || value > control->maximum) {
            // Saying what was refused is half the job; saying what the knob is
            // therefore left at is the other half. A master level declined for
            // being too quiet leaves an amplifier much louder than the preset
            // asked for, and a person reading "declined" would not guess that.
            fitting.declined << QStringLiteral("%1 wants %2%3, which this amplifier cannot go "
                                               "%4 than %5%3 -- so it stays at %6%3.")
                                    .arg(called, rounded(value), unit,
                                         value < control->minimum ? QStringLiteral("lower")
                                                                  : QStringLiteral("higher"),
                                         rounded(value < control->minimum ? control->minimum
                                                                         : control->maximum),
                                         rounded(control->value));
            return;
        }
        fitting.settings.append({QString(symbol), float(value)});
    };

    const auto enumerated = [&](QLatin1String symbol, const QString &wanted, const QString &called) {
        if (wanted.isEmpty())
            return;
        const Lv2::Control *control = controlFor(controls, symbol);
        if (!control)
            return;

        float value = 0;
        if (choose(*control, wanted, &value))
            fitting.settings.append({QString(symbol), value});
        else
            fitting.declined << QStringLiteral("No %1 here is called \"%2\".").arg(called, wanted);
    };

    if (!voicing.usesAmplifier()) {
        fitting.declined << QStringLiteral(
            "This voicing switches the amplifier off: it is a cabinet and an equaliser, "
            "and an amplifier is not what plays it.");
    } else {
        enumerated(QLatin1String("model"), voicing.valve, QStringLiteral("valve"));
    }

    enumerated(QLatin1String("t_model"), voicing.toneStack, QStringLiteral("tone stack"));
    enumerated(QLatin1String("c_model"), voicing.cabinet, QStringLiteral("cabinet"));

    continuous(QLatin1String("Bass"), voicing.bass, QStringLiteral("Bass"), QString());
    continuous(QLatin1String("Middle"), voicing.middle, QStringLiteral("Middle"), QString());
    continuous(QLatin1String("Treble"), voicing.treble, QStringLiteral("Treble"), QString());
    continuous(QLatin1String("PreGain"), voicing.preGainDb, QStringLiteral("Pre-gain"),
               QStringLiteral(" dB"));
    continuous(QLatin1String("MasterGain"), voicing.masterGainDb, QStringLiteral("Master"),
               QStringLiteral(" dB"));

    if (!voicing.rig.isEmpty()) {
        fitting.declined << QStringLiteral("Not carried: %1.")
                                .arg(voicing.rig.join(QStringLiteral(", ")));
    }
    return fitting;
}
}
