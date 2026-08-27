// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#pragma once

#include <QList>

/**
 * The sounds a guitar makes that are not notes, as note numbers.
 *
 * A sample library keeps its squeaks and clicks where it keeps everything
 * else: as regions, answering to keys, above the top of the instrument's
 * range. Emily maps a fingering squeak to 90, five dead-note variants to 91
 * to 95 and a pick coming to rest on the strings to 96; Growlybass maps four
 * pick scrapes to 81 to 84. **The keys differ per library**, which is the
 * whole reason this type exists: nothing may hardcode "90 is a squeak", so a
 * map is discovered from whatever library is loaded -- see `Sfz::noises` --
 * and handed to `Timeline`, which is what decides when to ask for one.
 *
 * Several keys per kind where a library records several, because that is what
 * five dead-note recordings are: not round-robins of one sound, which the
 * library already expresses with `lorand`, but the different thing a different
 * string does under the same hand.
 *
 * Empty is the ordinary case. A General MIDI programme has none of this, a
 * library may map none of it, and everything that reads this has to work
 * unchanged when it does.
 */
namespace Noises
{
struct Map {
    /** A hand sliding along a wound string: the noise a position shift makes. */
    QList<int> fingering;

    /** A string struck with the fretting hand off the note -- the dead note. */
    QList<int> muted;

    /** The pick coming to rest on the strings, which is where a part stops. */
    QList<int> pickRest;

    /**
     * The pick dragged along a wound string.
     *
     * Discovered because a bass library maps them and this map should say what
     * it found, and asked for by nothing: there is no notation for a scrape in
     * the model, so nothing in a score can mean one yet.
     */
    QList<int> scrape;

    bool isEmpty() const
    {
        return fingering.isEmpty() && muted.isEmpty() && pickRest.isEmpty()
            && scrape.isEmpty();
    }
};
}
