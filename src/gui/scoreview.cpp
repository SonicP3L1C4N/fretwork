// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "scoreview.h"

#include <QGuiApplication>
#include <QMouseEvent>
#include <QPainter>
#include <QPalette>

#include <algorithm>

namespace
{
/**
 * The colours of the page on screen.
 *
 * The application's own rather than the desktop's, and deliberately: a page is
 * paper, and it does not become dark because the desktop is. What the window
 * changes is the paper itself, to the off-white the rest of the window is
 * built around -- a sheet of pure white inside ink-coloured chrome reads as a
 * hole rather than as a page.
 */
Tab::Palette paletteOfThePage()
{
    Tab::Palette palette;
    palette.paper = QColor(0xF3, 0xF2, 0xF2);
    return palette;
}
}

ScoreView::ScoreView(QQuickItem *parent)
    : QQuickPaintedItem(parent)
{
    setFlag(ItemHasContents, true);
    setAcceptedMouseButtons(Qt::LeftButton);
    setFlag(ItemIsFocusScope, true);
    setActiveFocusOnTab(true);
    // Painted into an image and uploaded, which is what a mostly-static page of
    // marks wants: redrawn only when the music, the size or the playhead moves.
    setRenderTarget(QQuickPaintedItem::FramebufferObject);
}

Session *ScoreView::session() const
{
    return m_session;
}

void ScoreView::setSession(Session *session)
{
    if (m_session == session) {
        return;
    }
    if (m_session) {
        disconnect(m_session, nullptr, this, nullptr);
    }
    m_session = session;
    if (m_session) {
        connect(m_session, &Session::layoutChanged, this, &ScoreView::onLayoutChanged);
        connect(m_session, &Session::positionChanged, this, &ScoreView::onPositionChanged);
        connect(m_session, &Session::cursorMoved, this, [this] {
            scrollCursorIntoView();
            update();
        });
        m_session->relayout(width());
    }
    onLayoutChanged();
    Q_EMIT sessionChanged();
}

qreal ScoreView::scrollY() const
{
    return m_scrollY;
}

void ScoreView::setScrollY(qreal scrollY)
{
    const qreal clamped = std::clamp(scrollY, 0.0, std::max(0.0, m_contentHeight - height()));
    if (qFuzzyCompare(clamped + 1, m_scrollY + 1)) {
        return;
    }
    m_scrollY = clamped;
    Q_EMIT scrollYChanged();
    update();
}

qreal ScoreView::contentHeight() const
{
    return m_contentHeight;
}

bool ScoreView::followPlayhead() const
{
    return m_followPlayhead;
}

void ScoreView::setFollowPlayhead(bool follow)
{
    if (m_followPlayhead == follow) {
        return;
    }
    m_followPlayhead = follow;
    Q_EMIT followPlayheadChanged();
}

qreal ScoreView::heightOfContent() const
{
    if (!m_session || m_session->layout().isEmpty()) {
        return 0;
    }
    const Tab::Layout &layout = m_session->layout();
    const Tab::Page &page = layout.pages.constFirst();
    if (page.systems.isEmpty()) {
        return 0;
    }
    return page.systems.constLast().y + layout.systemHeight() + layout.style.systemSpacing;
}

void ScoreView::onLayoutChanged()
{
    const qreal height = heightOfContent();
    if (!qFuzzyCompare(height + 1, m_contentHeight + 1)) {
        m_contentHeight = height;
        Q_EMIT contentHeightChanged();
    }
    setScrollY(m_scrollY);
    update();
}

void ScoreView::onPositionChanged()
{
    if (!m_session) {
        return;
    }
    const int bar = m_session->currentBar();
    if (bar == m_highlighted) {
        return;
    }
    m_highlighted = bar;

    if (m_followPlayhead && bar >= 0 && !m_session->layout().isEmpty()) {
        // Scroll only when the bar being played has left the view, so that a
        // reader is not chased down the page by something moving every bar.
        for (const Tab::System &system : m_session->layout().pages.constFirst().systems) {
            const bool holds = std::any_of(system.bars.begin(), system.bars.end(),
                                           [bar](const Tab::LaidBar &laid) {
                                               return laid.index == bar;
                                           });
            if (!holds) {
                continue;
            }
            const qreal top = system.y - m_session->layout().style.systemSpacing;
            const qreal bottom = system.y + m_session->layout().systemHeight();
            if (top < m_scrollY || bottom > m_scrollY + height()) {
                setScrollY(top - height() / 3);
            }
            break;
        }
    }
    update();
}

void ScoreView::mousePressEvent(QMouseEvent *event)
{
    if (!m_session) {
        QQuickPaintedItem::mousePressEvent(event);
        return;
    }
    // The view scrolls by drawing elsewhere, so what was clicked is further
    // down the page than where the pointer is.
    m_session->placeCursorAt(event->position().x(), event->position().y() + m_scrollY,
                             event->modifiers() & Qt::ShiftModifier);
    forceActiveFocus();
    event->accept();
}

/** Dragging selects, which is the gesture everybody tries first. */
void ScoreView::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_session || !(event->buttons() & Qt::LeftButton)) {
        QQuickPaintedItem::mouseMoveEvent(event);
        return;
    }
    m_session->placeCursorAt(event->position().x(), event->position().y() + m_scrollY, true);
    event->accept();
}

/** Keeps the caret on screen when it is moved by the keyboard. */
void ScoreView::scrollCursorIntoView()
{
    if (!m_session || m_session->layout().isEmpty()) {
        return;
    }
    const Cursor cursor = m_session->cursor();
    qreal x = 0;
    qreal y = 0;
    if (!Tab::positionOf(m_session->layout(), cursor.bar, cursor.voice, cursor.beat,
                         &x, &y, nullptr)) {
        return;
    }
    const qreal system = m_session->layout().systemHeight();
    if (y < m_scrollY) {
        setScrollY(y - m_session->layout().style.systemSpacing);
    } else if (y + system > m_scrollY + height()) {
        setScrollY(y + system + m_session->layout().style.systemSpacing - height());
    }
}

void ScoreView::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickPaintedItem::geometryChange(newGeometry, oldGeometry);
    if (m_session && !qFuzzyCompare(newGeometry.width() + 1, oldGeometry.width() + 1)) {
        m_session->relayout(newGeometry.width());
    }
}

void ScoreView::paint(QPainter *painter)
{
    const Tab::Palette palette = paletteOfThePage();
    painter->fillRect(QRectF(0, 0, width(), height()), palette.paper);

    if (!m_session || m_session->layout().isEmpty()) {
        return;
    }
    const Tab::Layout &layout = m_session->layout();

    painter->save();
    painter->translate(0, -m_scrollY);

    if (m_session->hasSelection()) {
        // Behind the music, like the played bar, and in the same colour the
        // caret uses -- it is the caret, widened.
        const Editing::Range range = m_session->selection();
        QColor wash = palette.accent;
        wash.setAlphaF(0.18);
        const qreal padding = layout.style.stringSpacing * 0.6;

        for (const Tab::System &system : layout.pages.constFirst().systems) {
            for (const Tab::LaidBar &bar : system.bars) {
                if (bar.index < range.from.bar || bar.index > range.to.bar) {
                    continue;
                }
                const qreal barLeft = layout.style.margin + bar.x;
                qreal first = 0;
                qreal last = 0;
                bool any = false;
                for (const Tab::LaidBeat &beat : bar.beats) {
                    if (beat.voice != range.from.voice
                        || !range.holds(bar.index, beat.index)) {
                        continue;
                    }
                    const qreal x = barLeft + beat.x;
                    first = any ? std::min(first, x) : x;
                    last = any ? std::max(last, x) : x;
                    any = true;
                }
                if (!any) {
                    continue;
                }
                // Where the selection carries on past this bar it is drawn to
                // the barline, so a run of bars reads as one block rather than
                // as a row of separate patches.
                const qreal from = bar.index > range.from.bar ? barLeft : first - padding;
                const qreal to =
                    bar.index < range.to.bar ? barLeft + bar.width : last + padding;
                painter->fillRect(QRectF(from, system.y - padding, to - from,
                                         layout.systemHeight() + padding),
                                  wash);
            }
        }
    }

    // The bar being played is lit by the painting rather than here: it is the
    // page that knows where a bar is, and the numbers inside it are drawn in a
    // colour of their own.
    Tab::paintPage(*painter, layout, 0, palette, m_highlighted);

    // The caret, drawn last so it is never behind a fret number.
    const Cursor cursor = m_session->cursor();
    qreal caretX = 0;
    qreal caretY = 0;
    if (Tab::positionOf(layout, cursor.bar, cursor.voice, cursor.beat, &caretX, &caretY,
                        nullptr)) {
        const int fromTop =
            layout.strings - 1 - std::clamp(cursor.string, 0, layout.strings - 1);
        const qreal centre = caretY + fromTop * layout.style.stringSpacing;

        QColor caret = palette.accent;
        painter->setPen(QPen(caret, hasActiveFocus() ? 1.6 : 0.8));
        caret.setAlphaF(hasActiveFocus() ? 0.22 : 0.10);
        painter->setBrush(caret);
        painter->drawRoundedRect(
            QRectF(caretX - 7, centre - layout.style.stringSpacing / 2 - 1, 14,
                   layout.style.stringSpacing + 2),
            2.5, 2.5);
        painter->setBrush(Qt::NoBrush);
    }

    painter->restore();
}
