// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#pragma once

#include "score.h"

/**
 * Where the caret is, and how it moves.
 *
 * A tablature caret is on a **string within a beat**, not on a note: there may
 * be nothing there yet, and typing a number is how something arrives. That is
 * the whole difference between editing tab and editing text, and getting it
 * wrong makes an editor that can only change notes that already exist.
 *
 * It addresses the score by position rather than by id -- bar, voice, beat
 * index -- because an id is a thing that can be deleted out from under a caret
 * and a position is not. Positions are clamped rather than allowed to be
 * invalid, so a cursor always points somewhere a note could go.
 */
struct Cursor {
    int track = 0;
    int bar = 0;        //< index into Score::masterBars
    int voice = 0;      //< 0 to 3
    int beat = 0;       //< how many beats into that voice
    int string = 0;     //< 0 is the lowest, as everywhere else

    bool operator==(const Cursor &other) const
    {
        return track == other.track && bar == other.bar && voice == other.voice
            && beat == other.beat && string == other.string;
    }

    bool operator!=(const Cursor &other) const
    {
        return !(*this == other);
    }
};

namespace Editing
{
/** Which way a key press moves the caret. */
enum class Move {
    Left, Right,
    Up, Down,           //< between strings, since the lowest string is at the bottom
    BarBack, BarForward,
    Start, End,
};

/** The voice id at a cursor, or -1 where that voice is empty. */
int voiceIdAt(const Score &score, const Cursor &cursor);

/** The beat id under the cursor, or -1 if the cursor is past the last beat. */
int beatIdAt(const Score &score, const Cursor &cursor);

/** The note under the cursor -- its string as well as its beat -- or -1. */
int noteIdAt(const Score &score, const Cursor &cursor);

/** How many beats the cursor's voice holds. */
int beatCount(const Score &score, const Cursor &cursor);

/** How many strings the cursor's track has. */
int stringCount(const Score &score, const Cursor &cursor);

/**
 * The cursor after a move, clamped to somewhere real.
 *
 * Moving right off the end of a bar steps into the next one, and left off the
 * front steps back -- which is what a reader expects and what makes a keyboard
 * usable without the mouse.
 */
Cursor moved(const Score &score, const Cursor &cursor, Move move);

/** The same cursor, dragged back inside whatever the score actually has. */
Cursor clamped(const Score &score, Cursor cursor);
}
