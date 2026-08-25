// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#pragma once

#include <QtGlobal>

#include <numeric>

/**
 * An exact musical duration, counted in quarter notes.
 *
 * Doubles are the obvious choice and the wrong one, though not for the reason
 * usually given. The error is far too small to hear: a score's worth of
 * accumulated tuplets lands a few parts in 10^16 away from where it should,
 * which is nothing next to a 960th of a quarter note.
 *
 * It matters because positions get compared for equality. A tie joins a note
 * to the one already ringing by asking whether one ends exactly where the
 * other starts, and two voices reach the same beat by adding up different
 * durations to get there. In doubles those two sums are equal most of the
 * time, and once in a while differ in the last bit -- and the tie silently
 * fails to join, so a held note restrikes in the middle of a bar. A bug that
 * appears in one piece in twenty and cannot be reproduced is worth more than
 * two long longs and a gcd, which is all this costs.
 */
struct Rational {
    qint64 numerator = 0;
    qint64 denominator = 1;

    constexpr Rational() = default;

    Rational(qint64 n, qint64 d = 1)
        : numerator(n)
        , denominator(d)
    {
        Q_ASSERT(d != 0);
        if (denominator < 0) {
            numerator = -numerator;
            denominator = -denominator;
        }
        const qint64 common = std::gcd(numerator < 0 ? -numerator : numerator, denominator);
        if (common > 1) {
            numerator /= common;
            denominator /= common;
        }
    }

    Rational operator+(const Rational &other) const
    {
        return {numerator * other.denominator + other.numerator * denominator,
                denominator * other.denominator};
    }

    Rational operator*(const Rational &other) const
    {
        return {numerator * other.numerator, denominator * other.denominator};
    }

    Rational &operator+=(const Rational &other)
    {
        return *this = *this + other;
    }

    bool operator==(const Rational &other) const
    {
        return numerator == other.numerator && denominator == other.denominator;
    }

    bool operator<(const Rational &other) const
    {
        return numerator * other.denominator < other.numerator * denominator;
    }

    bool isZero() const
    {
        return numerator == 0;
    }

    double toDouble() const
    {
        return double(numerator) / double(denominator);
    }

    /** Rounded to the nearest tick, which is where exactness finally stops. */
    qint64 toTicks(int perQuarter) const
    {
        return (numerator * perQuarter + denominator / 2) / denominator;
    }
};
