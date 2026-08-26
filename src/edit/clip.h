// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#pragma once

#include "rational.h"
#include "score.h"

#include <QList>

/**
 * Music lifted out of a score, and no longer part of one.
 *
 * By value rather than by id, which is the whole point: the ids in a score
 * belong to that score, and a clipboard holding them would be a clipboard that
 * stops meaning anything the moment the beat it names is deleted. What is kept
 * is what the music *is* -- how long each beat lasts and what is on the strings
 * -- so pasting it makes new beats and new notes rather than aliases of old
 * ones.
 *
 * Bar boundaries are kept because they are part of what was copied. Four bars
 * of a riff pasted as one long bar would be the same notes and not the same
 * music, and re-barring somebody's phrase for them is not a decision a paste
 * gets to make.
 *
 * This clipboard is Fretwork's own and does not reach the desktop's. Copying
 * between two windows of the program means serialising the music to a MIME
 * type, which is a format decision and belongs with the ones in `fwformat`
 * rather than being invented here in passing.
 */
struct Clip {
    /** One beat, with the two things that are ids held by value instead. */
    struct Item {
        /**
         * Everything else about the beat -- its dynamic, its tremolo, its
         * brush -- carried whole, so that a field added to `Beat` later is
         * copied without anybody having to remember to come back here.
         * `beat.rhythm` and `beat.notes` are the ids, and mean nothing here.
         */
        Beat beat;
        Rational duration = Rational(1);
        QList<Note> notes;
    };

    /** The beats taken from one bar, in order. A bar may contribute none. */
    QList<QList<Item>> bars;

    bool isEmpty() const
    {
        for (const QList<Item> &bar : bars) {
            if (!bar.isEmpty()) {
                return false;
            }
        }
        return true;
    }

    int beatCount() const
    {
        int count = 0;
        for (const QList<Item> &bar : bars) {
            count += int(bar.size());
        }
        return count;
    }
};
