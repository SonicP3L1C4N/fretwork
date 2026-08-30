// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "fretboard.h"

#include <QMap>

#include <algorithm>

namespace
{
/**
 * What each thing costs, and why they are in this order.
 *
 * The numbers matter only for the order they put candidates in, so they are
 * spaced far enough apart that each one decides before the next is consulted,
 * and the argument is about the order rather than about the arithmetic:
 *
 * - **Moving the hand** dominates everything, because it is the expensive
 *   physical act. Everything below it is a choice between places the hand can
 *   already reach.
 * - **An unwanted open string** is worth moving the hand two frets to avoid
 *   and not three. It is a break in the sound rather than an effort, so it
 *   outweighs a shift a player would not notice and not one they would.
 * - **A crossed pair** -- the higher pitch on the lower string -- is worth
 *   avoiding where it is free to, and never worth moving the hand for. These
 *   voicings are unusual rather than wrong, so this is a preference and not a
 *   refusal.
 * - **Height up the neck** is a tie-break and nothing more. A neck is under a
 *   hundred frets, so it can never outweigh anything above it; what it does is
 *   settle the question when no hand has been established yet, and it settles
 *   it at the first position, which is where a tablature program writes a note
 *   it has been told nothing else about.
 */
constexpr int HandTravel = 100;
constexpr int OpenBreak = 250;
constexpr int Crossing = 60;
constexpr int NeckHeight = 1;

/** A shape that no one hand covers; not a cost, a refusal. */
constexpr int DoesNotFit = -1;

/** How far the hand must move to reach a fret, which is zero far more often
 *  than not: an open string needs no hand at all, and a hand that has not been
 *  anywhere yet is not far from anywhere. */
int travelTo(const Fretboard::Hand &hand, int fret)
{
    if (fret == 0 || hand.fret == Fretboard::Unknown) {
        return 0;
    }
    const int highest = hand.fret + hand.span - 1;
    if (fret < hand.fret) {
        return hand.fret - fret;
    }
    if (fret > highest) {
        return fret - highest;
    }
    return 0;
}

int noteCost(const Fretboard::Hand &hand, const Fretboard::Position &at)
{
    int cost = HandTravel * travelTo(hand, at.fret);
    if (at.fret == 0 && !hand.openStrings) {
        cost += OpenBreak;
    }
    return cost + NeckHeight * at.fret;
}

/**
 * What a whole shape costs, or DoesNotFit where its fretted notes are further
 * apart than one hand reaches.
 *
 * The hand is charged once for the shape rather than once for each note in it,
 * because the fingers arrive together: a barre at the fifth fret is one move
 * whether it is holding two strings or six.
 */
int shapeCost(const Fretboard::Hand &hand, const QList<int> &pitches, const QList<Fretboard::Position> &shape)
{
    int lowest = Fretboard::Unknown;
    int highest = Fretboard::Unknown;
    int opens = 0;
    for (const Fretboard::Position &at : shape) {
        if (at.fret == 0) {
            ++opens;
            continue;
        }
        if (lowest == Fretboard::Unknown || at.fret < lowest) {
            lowest = at.fret;
        }
        if (at.fret > highest) {
            highest = at.fret;
        }
    }
    const bool fretted = lowest != Fretboard::Unknown;
    if (fretted && highest - lowest + 1 > hand.span) {
        return DoesNotFit;
    }

    int cost = 0;
    if (fretted && hand.fret != Fretboard::Unknown) {
        // The nearest position the hand can stand in and still cover the shape.
        // At most one of these is positive, since the shape fits in the span.
        const int travel = std::max({0, hand.fret - lowest, highest - (hand.fret + hand.span - 1)});
        cost += HandTravel * travel;
    }
    if (!hand.openStrings) {
        cost += OpenBreak * opens;
    }
    if (fretted) {
        cost += NeckHeight * lowest;
    }

    for (qsizetype first = 0; first < shape.size(); ++first) {
        for (qsizetype second = first + 1; second < shape.size(); ++second) {
            if (pitches.at(first) == pitches.at(second)) {
                // A unison is on two strings at once by definition and is not
                // crossed whichever way round it is written.
                continue;
            }
            const bool risingPitch = pitches.at(first) < pitches.at(second);
            const bool risingString = shape.at(first).string < shape.at(second).string;
            if (risingPitch != risingString) {
                cost += Crossing;
            }
        }
    }
    return cost;
}

/**
 * The search behind chord(): every way of putting the pitches on distinct
 * strings, scored whole.
 *
 * Exhaustive, and it can afford to be. Strings are taken as they are used and
 * a partial shape already wider than the hand is abandoned where it stands, so
 * what looks like one candidate list multiplied by another collapses to
 * something nearer a permutation of the strings -- and an instrument has six
 * of those, or a few more.
 */
class ChordSearch
{
public:
    ChordSearch(const QList<int> &pitches, const QList<QList<Fretboard::Position>> &options, const Fretboard::Hand &hand)
        : m_pitches(pitches)
        , m_options(options)
        , m_hand(hand)
        , m_shape(pitches.size())
    {
    }

    QList<Fretboard::Position> run()
    {
        place(0);
        return m_best;
    }

private:
    void place(qsizetype index)
    {
        if (index == m_pitches.size()) {
            const int cost = shapeCost(m_hand, m_pitches, m_shape);
            if (cost != DoesNotFit && (m_best.isEmpty() || cost < m_bestCost)) {
                m_best = m_shape;
                m_bestCost = cost;
            }
            return;
        }
        for (const Fretboard::Position &at : m_options.at(index)) {
            if (m_taken.contains(at.string)) {
                continue;
            }
            if (!withinReach(at.fret, index)) {
                continue;
            }
            m_taken.append(at.string);
            m_shape[index] = at;
            place(index + 1);
            m_taken.removeLast();
        }
    }

    /** Whether the shape so far, plus this fret, is still one hand wide. */
    bool withinReach(int fret, qsizetype placed) const
    {
        if (fret == 0) {
            return true;
        }
        int lowest = fret;
        int highest = fret;
        for (qsizetype index = 0; index < placed; ++index) {
            const int other = m_shape.at(index).fret;
            if (other == 0) {
                continue;
            }
            lowest = std::min(lowest, other);
            highest = std::max(highest, other);
        }
        return highest - lowest + 1 <= m_hand.span;
    }

    const QList<int> &m_pitches;
    const QList<QList<Fretboard::Position>> &m_options;
    const Fretboard::Hand &m_hand;
    QList<Fretboard::Position> m_shape;
    QList<int> m_taken;
    QList<Fretboard::Position> m_best;
    int m_bestCost = 0;
};

/** Where a phrase can have got to: the hand it left behind, what it played to
 *  get there, and which state it came from. */
struct Step {
    int cost = 0;
    int from = Fretboard::Unknown;
    Fretboard::Position at;
};
}

bool Fretboard::operator==(const Position &left, const Position &right)
{
    return left.string == right.string && left.fret == right.fret;
}

bool Fretboard::operator!=(const Position &left, const Position &right)
{
    return !(left == right);
}

Fretboard::Hand Fretboard::Hand::after(const Position &played) const
{
    Hand next = *this;
    // An open string is played with no hand in it, so it says nothing about
    // where the hand is -- and a phrase that touches one has not lost its
    // position by doing so.
    if (!played.isValid() || played.fret == 0) {
        return next;
    }
    if (fret == Unknown) {
        next.fret = played.fret;
        return next;
    }
    // Otherwise it moves as little as it can: down to the note, or up until
    // the note is under the last finger.
    if (played.fret < fret) {
        next.fret = played.fret;
    } else if (played.fret > fret + span - 1) {
        next.fret = played.fret - span + 1;
    }
    return next;
}

int Fretboard::pitchAt(const Instrument &instrument, int string, int fret)
{
    if (string < 0 || string >= instrument.tuning.size()) {
        return Unknown;
    }
    return instrument.tuning.at(string) + instrument.capo + fret;
}

int Fretboard::fretFor(const Instrument &instrument, int string, int midi)
{
    if (string < 0 || string >= instrument.tuning.size()) {
        return Unknown;
    }
    return midi - instrument.tuning.at(string) - instrument.capo;
}

QList<Fretboard::Position> Fretboard::candidates(const Instrument &instrument, int midi)
{
    // Frets are counted from the capo, so a capo at the fifth fret does not
    // shorten the numbers a player reads -- it shortens the neck they are
    // written on.
    const int highest = instrument.frets - instrument.capo;

    QList<Position> found;
    for (qsizetype string = 0; string < instrument.tuning.size(); ++string) {
        const int fret = fretFor(instrument, int(string), midi);
        if (fret >= 0 && fret <= highest) {
            found.append(Position{int(string), fret});
        }
    }
    return found;
}

Fretboard::Position Fretboard::choose(const Instrument &instrument, int midi, const Hand &hand, const QList<int> &occupied)
{
    Position best;
    int bestCost = 0;
    for (const Position &at : candidates(instrument, midi)) {
        // Two notes on one string at one moment is not a chord, it is a
        // mistake -- the same refusal the editor makes when a note is moved
        // onto a string that is already sounding.
        if (occupied.contains(at.string)) {
            continue;
        }
        const int cost = noteCost(hand, at);
        if (!best.isValid() || cost < bestCost) {
            best = at;
            bestCost = cost;
        }
    }
    return best;
}

QList<Fretboard::Position> Fretboard::chord(const Instrument &instrument, const QList<int> &pitches, const Hand &hand)
{
    if (pitches.isEmpty() || pitches.size() > instrument.tuning.size()) {
        return {};
    }

    QList<QList<Position>> options;
    options.reserve(pitches.size());
    for (const int midi : pitches) {
        const QList<Position> here = candidates(instrument, midi);
        if (here.isEmpty()) {
            return {};
        }
        options.append(here);
    }

    return ChordSearch(pitches, options, hand).run();
}

QList<Fretboard::Position> Fretboard::phrase(const Instrument &instrument, const QList<int> &pitches, const Hand &hand)
{
    if (pitches.isEmpty()) {
        return {};
    }

    QList<QList<Position>> options;
    options.reserve(pitches.size());
    for (const int midi : pitches) {
        const QList<Position> here = candidates(instrument, midi);
        if (here.isEmpty()) {
            return {};
        }
        options.append(here);
    }

    // The state a phrase is in partway through is exactly the fret its hand is
    // at: two paths that arrive at the same note with the hand in the same
    // place are the same problem from there on, and only the cheaper of them
    // is worth carrying. That is what keeps this from being a walk of every
    // path through the line -- there are as many states as there are frets,
    // however long the phrase is.
    //
    // Ordered rather than hashed so that ties are settled the same way twice:
    // the lowest hand position, and then the lowest string, which is the order
    // candidates come in.
    QMap<int, Step> states;
    states.insert(hand.fret, Step{});
    QList<QMap<int, Step>> table;
    table.reserve(options.size());

    for (const QList<Position> &here : options) {
        QMap<int, Step> next;
        for (auto state = states.cbegin(); state != states.cend(); ++state) {
            Hand at = hand;
            at.fret = state.key();
            for (const Position &candidate : here) {
                const int cost = state.value().cost + noteCost(at, candidate);
                const int landing = at.after(candidate).fret;
                const auto found = next.constFind(landing);
                if (found == next.cend() || cost < found.value().cost) {
                    next.insert(landing, Step{cost, state.key(), candidate});
                }
            }
        }
        table.append(next);
        states = next;
    }

    // Not `landing == Unknown` as the test for having found one yet: a phrase
    // of nothing but open strings ends with the hand exactly where it started,
    // and Unknown is a hand position like any other.
    int landing = Unknown;
    int bestCost = 0;
    bool chosen = false;
    for (auto state = states.cbegin(); state != states.cend(); ++state) {
        if (!chosen || state.value().cost < bestCost) {
            landing = state.key();
            bestCost = state.value().cost;
            chosen = true;
        }
    }

    QList<Position> answer(pitches.size());
    for (qsizetype level = table.size() - 1; level >= 0; --level) {
        const Step step = table.at(level).value(landing);
        answer[level] = step.at;
        landing = step.from;
    }
    return answer;
}
