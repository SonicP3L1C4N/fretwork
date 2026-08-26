// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "cursor.h"

#include <algorithm>

int Editing::voiceIdAt(const Score &score, const Cursor &cursor)
{
    if (cursor.bar < 0 || cursor.bar >= score.masterBars.size()) {
        return -1;
    }
    const MasterBar &master = score.masterBars.at(cursor.bar);
    if (cursor.track < 0 || cursor.track >= master.bars.size()) {
        return -1;
    }
    const Bar bar = score.bars.value(master.bars.at(cursor.track));
    if (cursor.voice < 0 || cursor.voice >= bar.voices.size()) {
        return -1;
    }
    return bar.voices.at(cursor.voice);
}

int Editing::beatCount(const Score &score, const Cursor &cursor)
{
    const int voice = voiceIdAt(score, cursor);
    return voice < 0 ? 0 : int(score.voices.value(voice).beats.size());
}

int Editing::beatIdAt(const Score &score, const Cursor &cursor)
{
    const int voice = voiceIdAt(score, cursor);
    if (voice < 0) {
        return -1;
    }
    const QList<int> beats = score.voices.value(voice).beats;
    if (cursor.beat < 0 || cursor.beat >= beats.size()) {
        return -1;
    }
    return beats.at(cursor.beat);
}

int Editing::noteIdAt(const Score &score, const Cursor &cursor)
{
    const int beat = beatIdAt(score, cursor);
    if (beat < 0) {
        return -1;
    }
    for (const int noteId : score.beats.value(beat).notes) {
        if (score.notes.value(noteId).string == cursor.string) {
            return noteId;
        }
    }
    return -1;
}

bool Editing::before(const Cursor &a, const Cursor &b)
{
    return a.bar != b.bar ? a.bar < b.bar : a.beat < b.beat;
}

Editing::Range Editing::ordered(const Cursor &a, const Cursor &b)
{
    return before(b, a) ? Range{b, a} : Range{a, b};
}

Rational Editing::durationAt(const Score &score, const Cursor &cursor)
{
    const int beat = beatIdAt(score, cursor);
    if (beat < 0) {
        return Rational(1);
    }
    return score.rhythms.value(score.beats.value(beat).rhythm, Rational(1));
}

int Editing::stringCount(const Score &score, const Cursor &cursor)
{
    if (cursor.track < 0 || cursor.track >= score.tracks.size()) {
        return 6;
    }
    const int strings = score.tracks.at(cursor.track).stringCount();
    // A drum kit has no strings, and its staff still has lines to sit on.
    return strings > 0 ? strings : 5;
}

Cursor Editing::clamped(const Score &score, Cursor cursor)
{
    cursor.track = std::clamp(cursor.track, 0, std::max(0, int(score.tracks.size()) - 1));
    cursor.bar = std::clamp(cursor.bar, 0, std::max(0, int(score.masterBars.size()) - 1));
    cursor.voice = std::clamp(cursor.voice, 0, 3);
    cursor.string = std::clamp(cursor.string, 0, stringCount(score, cursor) - 1);

    // One past the last beat is a real place to be: it is where a new beat
    // would go, and where typing at the end of a bar puts you.
    cursor.beat = std::clamp(cursor.beat, 0, std::max(0, beatCount(score, cursor)));
    return cursor;
}

Cursor Editing::moved(const Score &score, const Cursor &cursor, Move move)
{
    Cursor out = clamped(score, cursor);

    switch (move) {
    case Move::Left:
        if (out.beat > 0) {
            --out.beat;
        } else if (out.bar > 0) {
            // Off the front of a bar steps back into the last beat of the one
            // before, which is what a reader means by pressing left.
            --out.bar;
            out.beat = std::max(0, beatCount(score, out) - 1);
        }
        break;

    case Move::Right:
        if (out.beat + 1 < beatCount(score, out)) {
            ++out.beat;
        } else if (out.bar + 1 < score.masterBars.size()) {
            ++out.bar;
            out.beat = 0;
        } else {
            // The last bar of the piece: one past the end, ready to add.
            out.beat = beatCount(score, out);
        }
        break;

    case Move::Up:
        // Up the page is up in pitch: string 0 is drawn at the bottom.
        out.string = std::min(out.string + 1, stringCount(score, out) - 1);
        break;

    case Move::Down:
        out.string = std::max(0, out.string - 1);
        break;

    case Move::BarBack:
        out.bar = std::max(0, out.bar - 1);
        out.beat = 0;
        break;

    case Move::BarForward:
        out.bar = std::min(int(score.masterBars.size()) - 1, out.bar + 1);
        out.beat = 0;
        break;

    case Move::Start:
        out.bar = 0;
        out.beat = 0;
        break;

    case Move::End:
        out.bar = std::max(0, int(score.masterBars.size()) - 1);
        out.beat = std::max(0, beatCount(score, out) - 1);
        break;
    }

    return clamped(score, out);
}
