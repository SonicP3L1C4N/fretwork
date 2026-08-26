// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#pragma once

#include "tablayout.h"

#include <QColor>
#include <QImage>

class QPainter;

/**
 * Drawing a laid-out score.
 *
 * Deliberately dull: every decision worth arguing about was made in
 * `tablayout`, and this only puts ink where that said to. The same function
 * paints a window, an image and a PDF page, so those three cannot drift apart.
 */
namespace Tab
{
/**
 * The colours of the page.
 *
 * Four weights of grey and not one, because a page of tablature is read by
 * weight: the string lines have to sit behind the music, the bar lines have to
 * divide it, the stems have to be read without being counted, and the fret
 * numbers have to be the blackest thing on the paper. Drawing them all in one
 * colour is what makes a generated tab look like a spreadsheet.
 *
 * The defaults are the application's own, taken from its icon -- ink, paper
 * and one magenta -- rather than from the desktop's palette. A page is paper;
 * it does not become dark because the desktop is. What the window does is set
 * `paper` to its own off-white, which is the one colour a printed page wants
 * left alone.
 */
struct Palette {
    QColor paper = QColor(0xFF, 0xFF, 0xFF);
    QColor ink = QColor(0x20, 0x1E, 0x1D);      //< fret numbers, and the title
    QColor staff = QColor(0xBA, 0xB6, 0xB6);    //< the string lines
    QColor barline = QColor(0x60, 0x5D, 0x5D);  //< what divides the bars, and repeats
    QColor rhythm = QColor(0x44, 0x41, 0x41);   //< stems, beams and rests
    QColor faint = QColor(0x9B, 0x97, 0x97);    //< bar numbers, tuning, section names
    QColor accent = QColor(0xD6, 0x00, 0x6C);   //< where a technique is marked
    QColor playing = QColor(0xFF, 0xDE, 0xE6);  //< behind the bar being played
    QColor playingInk = QColor(0xAA, 0x0B, 0x56);   //< the fret numbers inside it
    QColor warning = QColor(0xB5, 0x4B, 0x2E);  //< where the music itself does not add up
};

/**
 * Paints one page onto whatever the painter is pointed at.
 *
 * `playingBar` is the master bar to light up, or -1 for none. It is painted
 * here rather than by the window because the page is what knows where a bar
 * is -- and because the numbers inside it are drawn in a colour of their own,
 * which nothing outside the painting could reach.
 */
void paintPage(QPainter &painter, const Layout &layout, int pageIndex,
               const Palette &palette = Palette(), int playingBar = -1);

/** One page as an image, at `scale` times the layout's own units. */
QImage toImage(const Layout &layout, int pageIndex, qreal scale = 2.0,
               const Palette &palette = Palette());

/** Every page, into one PDF. */
bool toPdf(const Layout &layout, const QString &path, QString *error = nullptr,
           const Palette &palette = Palette());
}
