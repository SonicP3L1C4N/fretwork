// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "notename.h"

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
