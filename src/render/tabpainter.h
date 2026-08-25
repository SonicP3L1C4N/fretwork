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
struct Palette {
    QColor paper = QColor(0xFF, 0xFF, 0xFF);
    QColor ink = QColor(0x1B, 0x1B, 0x1B);
    QColor staff = QColor(0x88, 0x88, 0x88);
    QColor faint = QColor(0xAA, 0xAA, 0xAA);    //< bar numbers, tuning, section names
    QColor accent = QColor(0x2E, 0x6D, 0xB4);   //< where a technique is marked
};

/** Paints one page onto whatever the painter is pointed at. */
void paintPage(QPainter &painter, const Layout &layout, int pageIndex,
               const Palette &palette = Palette());

/** One page as an image, at `scale` times the layout's own units. */
QImage toImage(const Layout &layout, int pageIndex, qreal scale = 2.0,
               const Palette &palette = Palette());

/** Every page, into one PDF. */
bool toPdf(const Layout &layout, const QString &path, QString *error = nullptr,
           const Palette &palette = Palette());
}
