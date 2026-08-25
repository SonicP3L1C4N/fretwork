// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#pragma once

#include "score.h"

#include <QList>
#include <QString>

/**
 * Where everything goes on the page, decided without drawing any of it.
 *
 * Kept apart from the painting on purpose. Layout is the part with the
 * judgement in it -- how wide a bar should be, where a line should break, how
 * to spread the slack when it does -- and it is the part that has to be right
 * before anything else can be. Separating it means it can be tested by reading
 * numbers rather than by looking at pictures, and it means the same layout can
 * be painted to a screen, an image and a PDF without three chances to disagree.
 *
 * This lays out the score **as notated**: repeats are drawn as the signs they
 * are, not expanded into the bars they stand for. Expanding them is what
 * `Timeline` does for playback, and a reader wants to see the shorthand they
 * would see on paper.
 */
namespace Tab
{
/** Everything adjustable about the look, in points. */
struct Style {
    qreal pageWidth = 595;      //< A4 at 72dpi
    qreal pageHeight = 842;
    qreal margin = 40;

    qreal stringSpacing = 11;   //< between the lines of the tablature
    qreal systemSpacing = 44;   //< between one line of music and the next,
                                //  and the room the labels above it need
    qreal titleHeight = 64;     //< room at the top of the first page

    qreal minimumBarWidth = 46;
    qreal beatSpacing = 15;     //< the width a quarter note asks for
    qreal barPadding = 8;       //< inside a barline, before the first note

    qreal fretSize = 8;         //< the type size of a fret number
    qreal labelSize = 8;
    qreal titleSize = 17;
};

/** A fret number, on a string. */
struct LaidNote {
    qreal x = 0;
    int string = 0;             //< 0 is the lowest, as everywhere else
    QString text;               //< usually a fret number; "x" for a dead note
    bool bend = false;
    bool palmMuted = false;
    bool hammer = false;
    bool slide = false;
    bool letRing = false;
};

struct LaidBeat {
    qreal x = 0;
    QList<LaidNote> notes;      //< empty is a rest
};

struct LaidBar {
    qreal x = 0;
    qreal width = 0;
    int index = 0;              //< into Score::masterBars
    QString section;            //< printed above the bar where the score names it
    QString timeSignature;      //< only where it changes
    bool repeatStart = false;
    bool repeatEnd = false;
    int repeatCount = 0;
    QList<LaidBeat> beats;
};

/** One line of music across the page. */
struct System {
    qreal y = 0;
    QList<LaidBar> bars;
};

struct Page {
    QList<System> systems;
};

struct Layout {
    Style style;
    QString title;
    QString artist;
    QString trackName;
    int strings = 6;
    QList<int> tuning;
    QList<Page> pages;

    bool isEmpty() const
    {
        return pages.isEmpty();
    }

    /** The height of one system's string lines, which the painter needs too. */
    qreal staffHeight() const
    {
        return style.stringSpacing * (strings - 1);
    }
};

/** Lays one track out across as many pages as it takes. */
Layout layOut(const Score &score, int trackIndex, const Style &style = Style());
}
