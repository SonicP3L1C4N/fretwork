// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "tabpainter.h"

#include <QFont>
#include <QPainter>
#include <QPdfWriter>

namespace
{
QFont sansOf(qreal size, bool bold = false)
{
    QFont font;
    font.setPointSizeF(size);
    font.setBold(bold);
    return font;
}

/** The vertical middle of a string's line, counting from the top of the staff. */
qreal lineY(const Tab::Layout &layout, qreal top, int string)
{
    // String 0 is the lowest in pitch and sits at the bottom of the tablature.
    const int fromTop = layout.strings - 1 - std::clamp(string, 0, layout.strings - 1);
    return top + fromTop * layout.style.stringSpacing;
}

void paintSystem(QPainter &painter, const Tab::Layout &layout, const Tab::System &system,
                 const Tab::Palette &palette)
{
    const Tab::Style &style = layout.style;
    const qreal left = style.margin;
    const qreal top = system.y;
    const qreal staff = layout.staffHeight();

    qreal width = 0;
    for (const Tab::LaidBar &bar : system.bars) {
        width = std::max(width, bar.x + bar.width);
    }

    painter.setPen(QPen(palette.staff, 0.6));
    for (int string = 0; string < layout.strings; ++string) {
        const qreal y = lineY(layout, top, string);
        painter.drawLine(QPointF(left, y), QPointF(left + width, y));
    }

    for (const Tab::LaidBar &bar : system.bars) {
        const qreal x = left + bar.x;

        painter.setPen(QPen(palette.staff, bar.repeatStart ? 1.6 : 0.6));
        painter.drawLine(QPointF(x, top), QPointF(x, top + staff));
        if (bar.repeatStart) {
            painter.drawLine(QPointF(x + 3, top), QPointF(x + 3, top + staff));
        }
        if (bar.repeatEnd) {
            const qreal end = left + bar.x + bar.width;
            painter.setPen(QPen(palette.staff, 1.6));
            painter.drawLine(QPointF(end - 3, top), QPointF(end - 3, top + staff));
        }

        // Clear of the top string: a fret number there is centred on the line
        // itself, so anything sitting a line-height above it collides.
        painter.setPen(palette.faint);
        painter.setFont(sansOf(style.labelSize * 0.8));
        painter.drawText(QRectF(x + 1, top - style.labelSize * 2.4, 40, style.labelSize * 1.4),
                         Qt::AlignLeft | Qt::AlignBottom, QString::number(bar.index + 1));

        if (!bar.section.isEmpty()) {
            painter.setPen(palette.ink);
            painter.setFont(sansOf(style.labelSize, true));
            painter.drawText(QRectF(x + 1, top - style.labelSize * 4.2, 200,
                                    style.labelSize * 1.6),
                             Qt::AlignLeft | Qt::AlignBottom, bar.section);
        }

        if (!bar.timeSignature.isEmpty()) {
            painter.setPen(palette.ink);
            painter.setFont(sansOf(style.labelSize, true));
            painter.drawText(QRectF(x + 3, top, 30, staff),
                             Qt::AlignLeft | Qt::AlignVCenter, bar.timeSignature);
        }

        if (bar.repeatEnd && bar.repeatCount > 2) {
            painter.setPen(palette.faint);
            painter.setFont(sansOf(style.labelSize * 0.8));
            painter.drawText(QRectF(left + bar.x + bar.width - 40,
                                    top - style.labelSize * 2.4, 38, style.labelSize * 1.4),
                             Qt::AlignRight | Qt::AlignBottom,
                             QStringLiteral("×%1").arg(bar.repeatCount));
        }

        painter.setFont(sansOf(style.fretSize));
        const QFontMetricsF metrics(painter.font());
        for (const Tab::LaidBeat &beat : bar.beats) {
            for (const Tab::LaidNote &note : beat.notes) {
                const qreal centre = lineY(layout, top, note.string);
                const qreal noteX = x + note.x;
                const QRectF box(noteX - 6, centre - metrics.height() / 2, 12,
                                 metrics.height());

                // The string line is cleared behind the number rather than
                // drawn through it, which is how tablature has always been set.
                painter.fillRect(box.adjusted(2.5, 1, -2.5, -1), palette.paper);
                painter.setPen(note.bend || note.slide || note.hammer ? palette.accent
                                                                      : palette.ink);
                painter.drawText(box, Qt::AlignCenter, note.text);
            }
        }
    }

    // The closing barline of the system.
    painter.setPen(QPen(palette.staff, 0.6));
    painter.drawLine(QPointF(left + width, top), QPointF(left + width, top + staff));
}
}

void Tab::paintPage(QPainter &painter, const Layout &layout, int pageIndex,
                    const Palette &palette)
{
    if (pageIndex < 0 || pageIndex >= layout.pages.size()) {
        return;
    }
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);

    const Style &style = layout.style;
    if (pageIndex == 0 && style.showTitle) {
        painter.setPen(palette.ink);
        painter.setFont(sansOf(style.titleSize, true));
        painter.drawText(QRectF(style.margin, style.margin,
                                style.pageWidth - style.margin * 2, style.titleSize * 1.6),
                         Qt::AlignLeft | Qt::AlignTop, layout.title);

        painter.setFont(sansOf(style.labelSize));
        painter.setPen(palette.faint);
        QStringList tuning;
        for (auto pitch = layout.tuning.crbegin(); pitch != layout.tuning.crend(); ++pitch) {
            static const QStringList names = {
                QStringLiteral("C"),  QStringLiteral("C#"), QStringLiteral("D"),
                QStringLiteral("D#"), QStringLiteral("E"),  QStringLiteral("F"),
                QStringLiteral("F#"), QStringLiteral("G"),  QStringLiteral("G#"),
                QStringLiteral("A"),  QStringLiteral("A#"), QStringLiteral("B"),
            };
            tuning.append(names.at(((*pitch % 12) + 12) % 12));
        }
        const QString subtitle =
            tuning.isEmpty()
                ? QStringLiteral("%1 — %2").arg(layout.artist, layout.trackName)
                : QStringLiteral("%1 — %2 — tuning %3")
                      .arg(layout.artist, layout.trackName, tuning.join(QLatin1Char(' ')));
        painter.drawText(QRectF(style.margin, style.margin + style.titleSize * 1.7,
                                style.pageWidth - style.margin * 2, style.labelSize * 2),
                         Qt::AlignLeft | Qt::AlignTop, subtitle);
    }

    for (const System &system : layout.pages.at(pageIndex).systems) {
        paintSystem(painter, layout, system, palette);
    }
}

QImage Tab::toImage(const Layout &layout, int pageIndex, qreal scale,
                    const Palette &palette)
{
    if (layout.isEmpty()) {
        return {};
    }
    QImage image(int(layout.style.pageWidth * scale), int(layout.style.pageHeight * scale),
                 QImage::Format_RGB32);
    image.fill(palette.paper);

    QPainter painter(&image);
    painter.scale(scale, scale);
    paintPage(painter, layout, pageIndex, palette);
    return image;
}

bool Tab::toPdf(const Layout &layout, const QString &path, QString *error,
                const Palette &palette)
{
    if (layout.isEmpty()) {
        if (error) {
            *error = QStringLiteral("there is nothing to draw");
        }
        return false;
    }

    QPdfWriter writer(path);
    writer.setResolution(72);       // so a layout point is a PDF point
    writer.setPageSize(QPageSize(QSizeF(layout.style.pageWidth, layout.style.pageHeight),
                                 QPageSize::Point));
    writer.setPageMargins(QMarginsF(0, 0, 0, 0));
    writer.setTitle(layout.title);
    writer.setCreator(QStringLiteral("Fretwork"));

    QPainter painter(&writer);
    if (!painter.isActive()) {
        if (error) {
            *error = QStringLiteral("cannot write a PDF to %1").arg(path);
        }
        return false;
    }
    for (int page = 0; page < layout.pages.size(); ++page) {
        if (page > 0 && !writer.newPage()) {
            if (error) {
                *error = QStringLiteral("cannot add a page");
            }
            return false;
        }
        paintPage(painter, layout, page, palette);
    }
    return true;
}
