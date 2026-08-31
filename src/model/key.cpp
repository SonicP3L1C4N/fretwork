// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "key.h"

#include <KLocalizedString>

#include <QStringList>

#include <array>
#include <cstdlib>

namespace
{
/** The seven letters, and the pitch each one is before anything is done to it. */
constexpr std::array<int, 7> NaturalPitchClass = {0, 2, 4, 5, 7, 9, 11};

/**
 * The order accidentals are added in, which is the only reason a key signature
 * can be written as a number at all: four sharps is always F C G D and never
 * any other four.
 */
constexpr std::array<int, 7> SharpOrder = {3, 0, 4, 1, 5, 2, 6};   // F C G D A E B
constexpr std::array<int, 7> FlatOrder = {6, 2, 5, 1, 4, 0, 3};    // B E A D G C F

constexpr int MostAccidentals = 7;

/**
 * The note each signature is named after, from seven flats to seven sharps.
 *
 * Written out rather than derived. The arithmetic that produces them is the
 * circle of fifths and it is perfectly real, but it wraps the letters and the
 * accidentals at different points, and a table of fifteen entries anybody can
 * check against a music book is worth more here than four lines nobody can.
 */
struct Tonic {
    int step;
    int alteration;
};

constexpr std::array<Tonic, 15> MajorTonics = {{
    {0, -1}, {4, -1}, {1, -1}, {5, -1}, {2, -1}, {6, -1}, {3, 0},   // Cb Gb Db Ab Eb Bb F
    {0, 0},                                                        // C
    {4, 0}, {1, 0}, {5, 0}, {2, 0}, {6, 0}, {3, 1}, {0, 1},        // G D A E B F# C#
}};

constexpr std::array<Tonic, 15> MinorTonics = {{
    {5, -1}, {2, -1}, {6, -1}, {3, 0}, {0, 0}, {4, 0}, {1, 0},     // Ab Eb Bb F C G D
    {5, 0},                                                        // A
    {2, 0}, {6, 0}, {3, 1}, {0, 1}, {4, 1}, {1, 1}, {5, 1},        // E B F# C# G# D# A#
}};

int pitchClassOf(int midi)
{
    return ((midi % 12) + 12) % 12;
}

/** What the signature does to each of the seven letters. */
std::array<int, 7> alterationsOf(const Key::Signature &signature)
{
    std::array<int, 7> alterations = {0, 0, 0, 0, 0, 0, 0};
    if (!Key::isValid(signature)) {
        return alterations;
    }
    const int count = std::abs(signature.accidentals);
    for (int index = 0; index < count; ++index) {
        if (signature.accidentals > 0) {
            alterations[SharpOrder[index]] = 1;
        } else {
            alterations[FlatOrder[index]] = -1;
        }
    }
    return alterations;
}

/** The spelling of a letter and an accidental, in the octave that letter is in. */
Key::Spelling spelled(int step, int alteration, int midi)
{
    Key::Spelling spelling;
    spelling.step = step;
    spelling.alteration = alteration;
    // The octave belongs to the letter, so it is read off the note the
    // accidental was applied to rather than off the pitch that came out: a B
    // sharp is in the octave of its B, and the C it sounds is in the next one.
    const int natural = midi - alteration;
    spelling.octave = (natural - pitchClassOf(natural)) / 12 - 1;
    return spelling;
}

/** Whether an accidental is written the way the key is already written. */
bool againstTheKey(int alteration, const Key::Signature &signature)
{
    const bool sharpKey = signature.accidentals >= 0;
    return (alteration > 0 && !sharpKey) || (alteration < 0 && sharpKey);
}
}

bool Key::operator==(const Signature &left, const Signature &right)
{
    return left.accidentals == right.accidentals && left.minor == right.minor;
}

bool Key::operator!=(const Signature &left, const Signature &right)
{
    return !(left == right);
}

bool Key::operator==(const Spelling &left, const Spelling &right)
{
    return left.step == right.step && left.alteration == right.alteration
        && left.octave == right.octave;
}

bool Key::operator!=(const Spelling &left, const Spelling &right)
{
    return !(left == right);
}

bool Key::isValid(const Signature &signature)
{
    return signature.accidentals >= -MostAccidentals && signature.accidentals <= MostAccidentals;
}

int Key::midiOf(const Spelling &spelling)
{
    return (spelling.octave + 1) * 12 + NaturalPitchClass[spelling.step] + spelling.alteration;
}

Key::Spelling Key::spell(int midi, const Signature &signature)
{
    const int wanted = pitchClassOf(midi);
    const std::array<int, 7> alterations = alterationsOf(signature);

    // In the key, and so already spelled: the signature said how, which is what
    // a signature is for.
    for (int step = 0; step < 7; ++step) {
        if (pitchClassOf(NaturalPitchClass[step] + alterations[step]) == wanted) {
            return spelled(step, alterations[step], midi);
        }
    }

    // Outside it, and so a choice. The smallest accidental that reaches the
    // note wins, because a plain letter is easier to read than a double
    // anything; where a sharp and a flat are equally small, the key breaks the
    // tie in favour of what it is already written in.
    int bestStep = -1;
    int bestAlteration = 0;
    int bestRank = 0;
    for (int step = 0; step < 7; ++step) {
        for (int alteration = -2; alteration <= 2; ++alteration) {
            if (pitchClassOf(NaturalPitchClass[step] + alteration) != wanted) {
                continue;
            }
            const int rank = std::abs(alteration) * 10 + (againstTheKey(alteration, signature) ? 1 : 0);
            if (bestStep < 0 || rank < bestRank) {
                bestStep = step;
                bestAlteration = alteration;
                bestRank = rank;
            }
        }
    }
    return spelled(bestStep, bestAlteration, midi);
}

bool Key::isDiatonic(int midi, const Signature &signature)
{
    const int wanted = pitchClassOf(midi);
    const std::array<int, 7> alterations = alterationsOf(signature);
    for (int step = 0; step < 7; ++step) {
        if (pitchClassOf(NaturalPitchClass[step] + alterations[step]) == wanted) {
            return true;
        }
    }
    return false;
}

QString Key::nameOf(const Spelling &spelling)
{
    static const QStringList letters = {
        QStringLiteral("C"), QStringLiteral("D"), QStringLiteral("E"), QStringLiteral("F"),
        QStringLiteral("G"), QStringLiteral("A"), QStringLiteral("B"),
    };
    QString name = letters.at(spelling.step);
    const QString sign = spelling.alteration > 0 ? QStringLiteral("♯") : QStringLiteral("♭");
    for (int count = 0; count < std::abs(spelling.alteration); ++count) {
        name += sign;
    }
    return name;
}

QString Key::withOctave(const Spelling &spelling)
{
    return nameOf(spelling) + QString::number(spelling.octave);
}

Key::Spelling Key::tonicOf(const Signature &signature)
{
    Spelling tonic;
    if (!isValid(signature)) {
        return tonic;
    }
    const Tonic named = signature.minor ? MinorTonics[signature.accidentals + MostAccidentals]
                                        : MajorTonics[signature.accidentals + MostAccidentals];
    tonic.step = named.step;
    tonic.alteration = named.alteration;
    // A key is not in an octave. Four is the one a Spelling starts in and it
    // means nothing here.
    return tonic;
}

QString Key::nameOf(const Signature &signature)
{
    const QString tonic = nameOf(tonicOf(signature));
    return signature.minor ? i18nc("a key signature, as in \"B♭ minor\"", "%1 minor", tonic)
                           : i18nc("a key signature, as in \"B major\"", "%1 major", tonic);
}
