// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "tabpainter.h"

#include <QFont>
#include <QPainter>
#include <QPdfWriter>

#include <algorithm>

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

/**
 * The stem under one column, saying how long it lasts.
 *
 * Only ink: which beams, which head and how many dots were all decided in the
 * layout, where they can be tested by reading numbers rather than by looking
 * at a picture. Beams stack upwards from the foot of the stem so that the row
 * is the same height however many there are -- a bar of demisemiquavers must
 * not push the next line of music down the page.
 *
 * A rest is drawn here too, as a bar in the middle of the staff with its
 * duration on the stem below it. The proper glyph needs a music font, which
 * this project does not vendor until standard notation arrives; a plain bar
 * cannot say how long the silence is, and the stem already does.
 */
void paintRhythm(QPainter &painter, const Tab::Layout &layout, const Tab::LaidBeat &beat,
                 qreal x, qreal nextX, qreal top, const Tab::Palette &palette)
{
    const Tab::Style &style = layout.style;
    const Tab::LaidRhythm &rhythm = beat.rhythm;

    const qreal stemTop = top + layout.staffHeight() + style.rhythmGap;
    const qreal foot = stemTop + style.rhythmStem;

    if (rhythm.rest) {
        const qreal middle = top + layout.staffHeight() / 2;
        painter.fillRect(QRectF(x - style.restWidth / 2, middle - 1.1, style.restWidth, 2.2),
                         palette.staff);
    }

    painter.setPen(QPen(palette.staff, 0.9));
    painter.setBrush(Qt::NoBrush);
    if (rhythm.stem) {
        painter.drawLine(QPointF(x, stemTop), QPointF(x, foot));
    }

    // A minim and a semibreve are told from a crotchet by an open head, which
    // is the whole of the note-head shape tablature has ever needed.
    if (rhythm.hollow) {
        painter.drawEllipse(QPointF(x, rhythm.stem ? foot : (stemTop + foot) / 2), 1.9, 1.6);
    }

    const auto beamAt = [&](int level) {
        return foot - level * style.beamSpacing;
    };
    const int shared = std::max(rhythm.beamLeft, rhythm.beamRight);
    painter.setPen(QPen(palette.staff, style.beamThickness));

    for (int level = 0; level < rhythm.beamRight; ++level) {
        painter.drawLine(QPointF(x, beamAt(level)), QPointF(nextX, beamAt(level)));
    }
    for (int index = 0; index < rhythm.stubs; ++index) {
        // The short beam on the semiquaver of a dotted pair: it points at the
        // note it belongs with rather than into the space beside it.
        const qreal reach = style.beamSpacing * (rhythm.stubRight ? 1.5 : -1.5);
        painter.drawLine(QPointF(x, beamAt(shared + index)),
                         QPointF(x + reach, beamAt(shared + index)));
    }
    for (int level = 0; level < rhythm.flags; ++level) {
        // A flag, not a beam: it runs to nothing, so it lifts away from the
        // stem instead of pointing level at a neighbour that is not there.
        painter.drawLine(QPointF(x, beamAt(level)),
                         QPointF(x + style.beamSpacing * 1.6,
                                 beamAt(level) - style.beamSpacing));
    }

    if (rhythm.dots > 0) {
        // Above whatever else is down here, so a dotted quaver in a beamed run
        // does not put its dot through the beam.
        const qreal dotY = beamAt(std::max({rhythm.flags, shared + rhythm.stubs}));
        painter.setPen(Qt::NoPen);
        painter.setBrush(palette.staff);
        for (int index = 0; index < rhythm.dots; ++index) {
            painter.drawEllipse(QPointF(x + 2.6 + index * 2.2, dotY), 0.7, 0.7);
        }
        painter.setBrush(Qt::NoBrush);
    }
}

/**
 * The marks that carry over a run of notes: "P.M." and "let ring".
 *
 * Drawn in the row above the staff, with a dashed line saying how far each one
 * lasts -- which is how tablature has always written the things a hand keeps
 * doing rather than the things a note is. A run that is one column wide gets
 * the label and no line: a dash to nowhere reads as a mistake.
 *
 * Where the runs are was decided in the layout. This only puts ink on them.
 */
void paintRuns(QPainter &painter, const Tab::Layout &layout, const Tab::System &system,
               const Tab::Palette &palette)
{
    if (system.runs.isEmpty()) {
        return;
    }
    const Tab::Style &style = layout.style;
    const qreal left = style.margin;
    // Just clear of the fret numbers on the top string, which are centred on
    // the line itself and reach half a line-height above it.
    const qreal baseline = system.y - style.markGap * 0.35;

    painter.setFont(sansOf(style.labelSize * 0.8));
    const QFontMetricsF metrics(painter.font());
    painter.setPen(palette.accent);

    for (const Tab::LaidRun &run : system.runs) {
        const QString label = run.mark == Tab::Mark::PalmMute ? QStringLiteral("P.M.")
                                                              : QStringLiteral("let ring");
        const qreal from = left + run.from;
        painter.drawText(QPointF(from - 3, baseline), label);
        if (run.to <= run.from) {
            continue;
        }
        // From the end of the label to the last column it covers, level with
        // the middle of the type rather than its feet.
        const qreal start = from - 3 + metrics.horizontalAdvance(label) + 2;
        const qreal end = left + run.to;
        if (end <= start) {
            continue;
        }
        QPen pen(palette.accent, 0.6);
        pen.setStyle(Qt::DashLine);
        painter.setPen(pen);
        const qreal y = baseline - metrics.xHeight() / 2;
        painter.drawLine(QPointF(start, y), QPointF(end, y));
        painter.setPen(palette.accent);
    }
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
        // itself, so anything sitting a line-height above it collides. A bar
        // whose beats do not come to its time signature says so on its number,
        // which is the only label every bar already has.
        painter.setPen(bar.incomplete ? palette.warning : palette.faint);
        painter.setFont(sansOf(style.labelSize * 0.8));
        painter.drawText(QRectF(x + 1, top - style.markGap - style.labelSize * 2.4, 40,
                                style.labelSize * 1.4),
                         Qt::AlignLeft | Qt::AlignBottom, QString::number(bar.index + 1));

        if (!bar.section.isEmpty()) {
            painter.setPen(palette.ink);
            painter.setFont(sansOf(style.labelSize, true));
            painter.drawText(QRectF(x + 1, top - style.markGap - style.labelSize * 4.2, 200,
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
            // A row higher than the bar numbers, and in the colour used for
            // things done to the music rather than the music itself: level
            // with them, "×4" and the next bar's "3" read as one label.
            painter.setPen(palette.accent);
            painter.setFont(sansOf(style.labelSize * 0.85, true));
            painter.drawText(QRectF(left + bar.x + bar.width - 42,
                                    top - style.markGap - style.labelSize * 4.0, 38,
                                    style.labelSize * 1.5),
                             Qt::AlignRight | Qt::AlignBottom,
                             QStringLiteral("×%1").arg(bar.repeatCount));
        }

        for (int index = 0; index < bar.beats.size(); ++index) {
            const Tab::LaidBeat &beat = bar.beats.at(index);
            // A beam runs to the next column along, which is the only place
            // the layout ever joins one to.
            const qreal nextX =
                x + (index + 1 < bar.beats.size() ? bar.beats.at(index + 1).x : beat.x);
            paintRhythm(painter, layout, beat, x + beat.x, nextX, top, palette);
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

    paintRuns(painter, layout, system, palette);

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
