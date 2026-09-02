// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#pragma once

#include "fretboard.h"
#include "rational.h"
#include "score.h"

#include <QList>
#include <QSet>

/**
 * Notes played against the transport, turned into a bar as they land.
 *
 * Step entry writes a chord at the caret and moves on when the hand comes
 * off, which needs no clock. This is the other way in: the transport is
 * rolling, a key goes down at some moment, and the moment has to become a
 * place in a bar. That is quantisation, and quantisation is a policy that
 * every notation program argues about, so this one is written down here and
 * tested rather than discovered.
 *
 * **The policy.**
 *
 * 1. *The grid* is a note value somebody chose -- a semiquaver by default.
 *    Straight; a swung bar is placed on the straight grid, which is stated
 *    as a limitation rather than hidden as a feature.
 * 2. *An onset* is rounded to the nearest line of the grid, measured in
 *    quarters from the start of the performance, and the bar is worked out
 *    from the rounded position. So a note forty milliseconds early for a
 *    downbeat is on the downbeat, in the bar the player meant.
 * 3. *A chord* is whatever rounds to the same line.
 * 4. *A note lasts* until its release rounded to the grid, or until the next
 *    onset, or until the bar line, whichever is first -- and never less than
 *    one cell. A release at least a cell before the next onset leaves a rest
 *    behind it; a shorter gap is the hand moving, not a rest.
 * 5. *The bar adds up.* What was not played is a rest, before the first note
 *    and after the last, so the page never marks a bar being recorded as
 *    incomplete.
 * 6. *A length that is not a written value* -- five semiquavers -- is split
 *    into the largest written values that fit, tied where it is a note and
 *    left as consecutive rests where it is not.
 * 7. *The bar line cuts.* A note held across it ends at it; the next bar
 *    starts silent unless the key is struck again.
 * 8. *A repeat* is the same bar however many times it is heard, and the last
 *    time through is what stays.
 * 9. *Which string* is the solver's decision, as it is when typing from the
 *    keys, with the hand carried from beat to beat. A pitch the instrument
 *    cannot reach is left out of the beat and named, not silently dropped.
 *
 * **What this keeps, and for how long.** The onsets of the bar being played
 * into, and nothing before it: the moment the transport moves on, they go,
 * because the score already has the bar. That is the whole of the state, and
 * it is the same shape as the list of keys held down that step entry keeps.
 * There is no take here to play back, no track to keep beside the score, and
 * no time in it that is not a place in a bar. That is the line the roadmap
 * drew and this is written to stay on the right side of it.
 *
 * Pure: a score, an order and a stream of moments in, a bar's worth of beats
 * out. Nothing here knows about a keyboard, a clock or an undo stack.
 */
class Recorder
{
public:
    struct Options {
        /** One cell of the grid, in quarters. A semiquaver by default. */
        Rational grid = Rational(1, 4);
        Fretboard::Instrument instrument;
    };

    /** One beat of the bar as it will be written. An empty shape is a rest. */
    struct Beat {
        Rational duration;
        QList<Fretboard::Position> shape;
        /** Whether the next beat is the rest of this note rather than a new one. */
        bool tiedToNext = false;
    };

    /** A bar as it stands after the latest note, or nothing where nothing changed. */
    struct Take {
        int pass = -1;      //< index into the played order
        int bar = -1;       //< index into Score::masterBars
        QList<Beat> beats;

        bool isValid() const
        {
            return bar >= 0;
        }
    };

    Recorder(const Score &score, const QList<int> &order, const Options &options);

    /**
     * A key went down at `quarters` into the performance.
     *
     * Returns the bar it landed in, rebuilt. Nothing where the moment is past
     * the end of the piece.
     */
    Take noteOn(int pitch, double quarters);

    /** The same key came up. Nothing where no such note is held in this bar. */
    Take noteOff(int pitch, double quarters);

    /**
     * The transport stopped, or left the bar for good.
     *
     * Forgets the bar's onsets, which the score already has, and carries the
     * hand into the next bar. Every note still held is cut at the bar line,
     * which is where it was already going to end.
     */
    void leave();

    /** Which pass is being played into, or -1 between bars. */
    int pass() const;

    /**
     * Pitches the instrument could not play, once each, since last asked.
     *
     * Reported rather than swallowed: a key that does nothing is a key
     * somebody presses again harder.
     */
    QList<int> unplayable();

    /** `quarters` rounded to the nearest line of the grid. */
    Rational snapped(double quarters) const;

    /**
     * A duration as the written values that add up to it, largest first.
     *
     * A crotchet is one; five semiquavers are a crotchet and a semiquaver.
     * Public because the rule is worth testing on its own.
     */
    static QList<Rational> written(const Rational &duration);

private:
    /** One key, as it was played into the current bar. */
    struct Played {
        Rational onset;         //< from the start of the bar, on the grid
        double off = -1;        //< from the start of the bar, as played; negative while held
        int pitch = 0;
    };

    bool enter(const Rational &at);
    Take rebuild();

    const Score &m_score;
    const QList<int> &m_order;
    Options m_options;

    int m_pass = -1;
    int m_bar = -1;
    Rational m_barStart;
    Rational m_barLength;
    QList<Played> m_played;
    QSet<int> m_reported;
    QList<int> m_unplayable;

    /** Where the hand was when the bar began, and where it is now. */
    Fretboard::Hand m_handAtStart;
    Fretboard::Hand m_hand;
};
