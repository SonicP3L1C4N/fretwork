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

    /**
     * A zero denominator is nought, and defined to be, rather than asserted
     * against. It used to be an assertion, which is right for a programming
     * error and useless against a file: a crafted score with two rhythms of
     * thirty-two dots multiplied their denominators to exactly 2^64, which
     * is zero in sixty-four bits, and the assertion was not compiled into
     * the build that opened it, so the next thing to ask how long a bar was
     * divided by zero in hardware. The readers now clamp what a file may
     * say and the arithmetic below no longer wraps, so nothing reaches this
     * -- and if something does, a duration of nought is a wrong note where
     * a fault was a lost session.
     */
    Rational(qint64 n, qint64 d = 1)
        : numerator(n)
        , denominator(d)
    {
        if (denominator == 0) {
            numerator = 0;
            denominator = 1;
            return;
        }
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

    /**
     * The nearest fraction on a fine grid, for when exact arithmetic has
     * run out of bits.
     *
     * Only a hostile file gets here: real music has a few dozen distinct
     * durations and their denominators never leave the hundreds. When an
     * exact product would overflow, the result is taken in floating point
     * and put back on a grid of 2^16 to the quarter -- finer than anything a
     * page can draw, and finite, so it cannot wrap to zero.
     */
    static Rational nearest(double value)
    {
        constexpr qint64 Grid = qint64(1) << 16;
        constexpr double Largest = 9.0e18 / double(Grid);
        if (value != value) {   // NaN, without <cmath>
            return {};
        }
        value = value > Largest ? Largest : (value < -Largest ? -Largest : value);
        const double scaled = value * double(Grid);
        return Rational(qint64(scaled + (scaled < 0 ? -0.5 : 0.5)), Grid);
    }

    Rational operator+(const Rational &other) const
    {
        // Over the least common denominator rather than the product, which
        // is what a person does and keeps the numbers small; and where even
        // that overflows, on the grid.
        const qint64 common = std::gcd(denominator, other.denominator);
        const qint64 mine = other.denominator / common;
        const qint64 theirs = denominator / common;
        qint64 left = 0;
        qint64 right = 0;
        qint64 top = 0;
        qint64 bottom = 0;
        if (__builtin_mul_overflow(numerator, mine, &left)
            || __builtin_mul_overflow(other.numerator, theirs, &right)
            || __builtin_add_overflow(left, right, &top)
            || __builtin_mul_overflow(denominator, mine, &bottom)) {
            return nearest(toDouble() + other.toDouble());
        }
        return {top, bottom};
    }

    Rational operator-(const Rational &other) const
    {
        return *this + Rational(-other.numerator, other.denominator);
    }

    Rational operator*(const Rational &other) const
    {
        qint64 top = 0;
        qint64 bottom = 0;
        if (__builtin_mul_overflow(numerator, other.numerator, &top)
            || __builtin_mul_overflow(denominator, other.denominator, &bottom)) {
            return nearest(toDouble() * other.toDouble());
        }
        return {top, bottom};
    }

    Rational operator/(const Rational &other) const
    {
        if (other.numerator == 0) {
            return {};
        }
        qint64 top = 0;
        qint64 bottom = 0;
        if (__builtin_mul_overflow(numerator, other.denominator, &top)
            || __builtin_mul_overflow(denominator, other.numerator, &bottom)) {
            return nearest(toDouble() / other.toDouble());
        }
        return {top, bottom};
    }

    /** How many whole `other`s fit, which for a positive value is the floor. */
    qint64 dividedBy(const Rational &other) const
    {
        const Rational quotient = *this / other;
        return quotient.numerator / quotient.denominator;
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
        qint64 left = 0;
        qint64 right = 0;
        if (__builtin_mul_overflow(numerator, other.denominator, &left)
            || __builtin_mul_overflow(other.numerator, denominator, &right)) {
            return toDouble() < other.toDouble();
        }
        return left < right;
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
