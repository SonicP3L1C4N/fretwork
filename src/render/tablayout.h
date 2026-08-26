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

    /**
     * Whether to draw the title and artist at the top.
     *
     * A printed page wants them; a window already says so in its title bar,
     * and drawing them again costs a system's worth of the score.
     */
    bool showTitle = true;

    qreal minimumBarWidth = 46;
    qreal beatSpacing = 15;     //< the width a quarter note asks for
    qreal barPadding = 8;       //< inside a barline, before the first note

    qreal fretSize = 8;         //< the type size of a fret number
    qreal labelSize = 8;
    qreal titleSize = 17;

    qreal rhythmGap = 9;        //< between the lowest string and the top of a stem
    qreal rhythmStem = 10;      //< how far a stem hangs below that
    qreal beamSpacing = 2.8;    //< between one beam and the next, stacked upwards
    qreal beamThickness = 1.3;
    qreal restWidth = 6;        //< the bar drawn in the staff where nothing sounds
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

/**
 * How long a column lasts, drawn as a stem under the staff.
 *
 * Tablature without this is a fingering chart: the numbers say where to put
 * your hands and nothing whatever about when. It is a row of its own below the
 * strings because that is where every tablature since the lute books has put
 * it -- the staff says which note, the stem says how long.
 *
 * The written symbol is worked out here rather than stored in the document,
 * because the document does not have it: `Score::rhythms` holds a duration
 * with its dots and tuplets already multiplied in, which is what playback
 * needs and what a page cannot draw. Asking which note value, dotted how many
 * times, comes to five-eighths of a quarter is how the symbol comes back.
 */
struct LaidRhythm {
    int beams = 0;              //< 0 crotchet or longer, 1 quaver, 2 semiquaver, ...
    int dots = 0;
    bool stem = true;           //< a semibreve has none
    bool hollow = false;        //< a minim or longer: an open head at the foot
    bool rest = false;          //< nothing sounds here

    /**
     * Beams shared with the neighbouring columns, and flags where there are
     * none. A run of quavers is beamed together within one beat of the bar and
     * never across one, which is the rule that makes a bar readable at a
     * glance rather than merely correct.
     */
    int beamLeft = 0;
    int beamRight = 0;
    int flags = 0;              //< drawn only where nothing is beamed to it

    /**
     * Beams this column has that its neighbours do not -- the short stub on
     * the semiquaver of a dotted-quaver pair. It points towards the note it
     * belongs with, which is the side that is already beamed.
     */
    int stubs = 0;
    bool stubRight = false;
};

struct LaidBeat {
    qreal x = 0;
    QList<LaidNote> notes;      //< empty is a rest
    LaidRhythm rhythm;

    /**
     * Which beat of which voice this column came from.
     *
     * A column is where several voices land at the same moment, so it is not
     * the same thing as one voice's beat -- and a caret addresses a voice and
     * a position within it. These carry the first voice that contributed, so
     * clicking a column puts the caret somewhere that exists.
     */
    int voice = 0;
    int index = 0;
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

    /**
     * True where the beats do not add up to the time signature.
     *
     * Marked rather than corrected. Editing a duration is the quickest way in
     * the world to make a bar that does not add up, and a program that
     * silently took the difference out of the neighbouring note would be
     * rewriting music nobody asked it to touch. A bar with nothing in it is
     * not marked: an empty bar is empty, not wrong.
     */
    bool incomplete = false;
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

    /**
     * The whole of one system: the string lines and the rhythm under them.
     *
     * Kept apart from `staffHeight` because they answer different questions --
     * where a string is drawn, and how much of the page a line of music
     * occupies. Confusing the two is what puts the next system's bar numbers
     * through this one's stems.
     */
    qreal systemHeight() const
    {
        return staffHeight() + style.rhythmGap + style.rhythmStem;
    }
};

/** Lays one track out across as many pages as it takes. */
Layout layOut(const Score &score, int trackIndex, const Style &style = Style());

/**
 * What is at a point on the page, in layout coordinates.
 *
 * Returns false where the point is nowhere in particular. The string is
 * whichever line is nearest rather than only an exact hit, because a caret
 * placed by clicking should land where it was aimed and not refuse.
 */
bool hitTest(const Layout &layout, qreal x, qreal y, int *bar, int *voice, int *beat,
             int *string);

/** Where a beat of a voice was drawn, or false if it is not on the page. */
bool positionOf(const Layout &layout, int bar, int voice, int beat, qreal *x, qreal *y,
                qreal *width);
}
