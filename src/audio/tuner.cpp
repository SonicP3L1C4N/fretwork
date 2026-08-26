// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "tuner.h"
#include "notename.h"

#include <KLocalizedString>

#include <algorithm>
#include <cmath>

double Tuner::hertzForMidi(double midi, double concertA)
{
    // MIDI 69 is the A above middle C, which is the note the number is defined
    // against; everything else is twelve equal steps away from it.
    return concertA * std::pow(2.0, (midi - 69.0) / 12.0);
}

double Tuner::midiForHertz(double hertz, double concertA)
{
    if (hertz <= 0) {
        return 0;
    }
    return 69.0 + 12.0 * std::log2(hertz / concertA);
}

double Tuner::cents(double hertz, double reference)
{
    if (hertz <= 0 || reference <= 0) {
        return 0;
    }
    return 1200.0 * std::log2(hertz / reference);
}

QList<Tuner::StringTarget> Tuner::targetsFor(const Track &track, double concertA)
{
    QList<StringTarget> targets;
    if (track.isPercussion()) {
        return targets;
    }
    targets.reserve(track.tuning.size());
    for (int index = 0; index < track.tuning.size(); ++index) {
        StringTarget target;
        target.index = index;
        target.midi = track.tuning.at(index) + track.capo;
        target.hertz = hertzForMidi(target.midi, concertA);
        target.name = NoteName::of(target.midi);
        targets.append(target);
    }
    return targets;
}

QList<Tuner::StringTarget> Tuner::standardGuitar(double concertA)
{
    Track guitar;
    guitar.instrumentType = QStringLiteral("electricGuitar");
    guitar.tuning = {40, 45, 50, 55, 59, 64};    // E2 A2 D3 G3 B3 E4
    return targetsFor(guitar, concertA);
}

double Tuner::acceptanceCents(const QList<StringTarget> &targets)
{
    double smallest = 400.0;    // two strings a major third apart, the closest standard tuning gets
    for (int index = 1; index < targets.size(); ++index) {
        const double gap = std::abs(cents(targets.at(index).hertz, targets.at(index - 1).hertz));
        if (gap > 1.0) {
            smallest = std::min(smallest, gap);
        }
    }
    return std::min(200.0, smallest / 2.0);
}

Tuner::Reading Tuner::read(double hertz, double clarity, const QList<StringTarget> &targets,
                           double concertA)
{
    Reading reading;
    if (hertz <= 0) {
        return reading;
    }

    reading.heard = true;
    reading.hertz = hertz;
    reading.clarity = clarity;

    const double midi = midiForHertz(hertz, concertA);
    reading.nearestMidi = int(std::lround(midi));
    reading.nearestCents = (midi - reading.nearestMidi) * 100.0;
    reading.noteName = NoteName::of(reading.nearestMidi);

    const double window = acceptanceCents(targets);
    double best = window;
    for (const StringTarget &target : targets) {
        const double distance = cents(hertz, target.hertz);
        if (std::abs(distance) <= best) {
            best = std::abs(distance);
            reading.string = target.index;
            reading.cents = distance;
        }
    }
    // Nothing within the window: the note is reported and the string is not
    // guessed at.
    if (reading.string < 0) {
        reading.cents = 0;
    }
    return reading;
}

QString Tuner::describe(const Reading &reading)
{
    if (!reading.heard) {
        return i18n("listening");
    }
    if (reading.string < 0) {
        return i18n("no string near");
    }
    const double distance = reading.cents;
    if (std::abs(distance) <= InTuneCents) {
        return i18n("in tune");
    }
    // Twenty cents is about where a guitarist stops hearing "close enough" and
    // starts hearing a beat against the other strings.
    if (distance < 0) {
        return std::abs(distance) > 20 ? i18n("very flat") : i18n("flat");
    }
    return distance > 20 ? i18n("very sharp") : i18n("sharp");
}
