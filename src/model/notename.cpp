// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "notename.h"

#include <QHash>
#include <QRegularExpression>
#include <QStringList>

namespace
{
/**
 * Modulo that does not go negative.
 *
 * MIDI pitches below C-1 do not occur in a score, but they do occur in
 * arithmetic: a transposition that has not been range-checked yet, or a note
 * the importer could not give a pitch to and left at -1.
 */
int pitchClassOf(int midi)
{
    return ((midi % 12) + 12) % 12;
}
}

QString NoteName::pitchClass(int midi)
{
    static const QStringList names = {
        QStringLiteral("C"),  QStringLiteral("C#"), QStringLiteral("D"),
        QStringLiteral("D#"), QStringLiteral("E"),  QStringLiteral("F"),
        QStringLiteral("F#"), QStringLiteral("G"),  QStringLiteral("G#"),
        QStringLiteral("A"),  QStringLiteral("A#"), QStringLiteral("B"),
    };
    return names.at(pitchClassOf(midi));
}

QString NoteName::of(int midi)
{
    // Floor division, so that C-1 is octave -1 rather than octave 0: the
    // integer division C++ does truncates towards zero and would put the
    // bottom octave of MIDI in the same one as the second.
    const int octave = (midi - pitchClassOf(midi)) / 12 - 1;
    return pitchClass(midi) + QString::number(octave);
}

int NoteName::parse(const QString &name)
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty()) {
        return -1;
    }

    bool number = false;
    const int plain = trimmed.toInt(&number);
    if (number) {
        return plain >= 0 && plain <= 127 ? plain : -1;
    }

    static const QRegularExpression written(
        QStringLiteral("^([A-Ga-g])([#b]?)(-?[0-9]{1,2})$"));
    const QRegularExpressionMatch match = written.match(trimmed);
    if (!match.hasMatch()) {
        return -1;
    }

    // Semitones above C, for the seven letters. Not a lookup by pitch class
    // name, because C# and Db are the same pitch and only one of them is in
    // the table this file prints from.
    static const QHash<QChar, int> letters = {
        {QLatin1Char('C'), 0}, {QLatin1Char('D'), 2}, {QLatin1Char('E'), 4},
        {QLatin1Char('F'), 5}, {QLatin1Char('G'), 7}, {QLatin1Char('A'), 9},
        {QLatin1Char('B'), 11},
    };
    int pitch = letters.value(match.captured(1).at(0).toUpper());
    if (match.captured(2) == QLatin1String("#")) {
        ++pitch;
    } else if (match.captured(2) == QLatin1String("b")) {
        --pitch;
    }

    // The inverse of `of`: C4 is middle C and MIDI 60, so the octave counts
    // from C-1 at zero.
    const int midi = (match.captured(3).toInt() + 1) * 12 + pitch;
    return midi >= 0 && midi <= 127 ? midi : -1;
}
