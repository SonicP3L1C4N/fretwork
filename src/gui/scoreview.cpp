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

/**
 * The desk the pages are lying on.
 *
 * Darker than the paper and lighter than the chrome, which is the whole of its
 * job: a page has to read as a thing with edges, and it cannot do that against
 * a background of its own colour. It is not the ink the toolbars are made of
 * either -- a sheet of paper on a black desk is a photograph, and this is a
 * document.
 */
const QColor Desk = QColor(0xC6, 0xC1, 0xC1);

/** The least desk to leave down either side of a page. */
constexpr qreal DeskMargin = 18;

/**
 * A sheet, and the shadow that says it is one.
 *
 * Three passes of a rounded rectangle rather than a blur: the shadow is four
 * pixels of gradient that nobody will look at directly, and a real blur is a
 * pixmap and a compositing pass per page per frame to draw something this
 * cheap to fake.
 */
void drawSheet(QPainter &painter, const QRectF &rect, const QColor &paper)
{
    painter.setPen(Qt::NoPen);
    for (int step = 3; step >= 1; --step) {
        QColor shadow(0, 0, 0);
        shadow.setAlphaF(0.05);
        painter.setBrush(shadow);
        painter.drawRect(rect.adjusted(-step, step * 0.5, step, step * 1.5));
    }
    painter.setBrush(paper);
    painter.drawRect(rect);
    painter.setBrush(Qt::NoBrush);
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
    // Every sheet and the desk between them, at the size they are being shown
    // at. The scrollbar measures the document and not the music.
    return m_session->layout().documentHeight() * m_zoom;
}

qreal ScoreView::zoom() const
{
    return m_zoom;
}

void ScoreView::setZoom(qreal zoom)
{
    // Far enough out to see a page whole on a laptop, far enough in to read a
    // fret number across a room. Outside that it stops being a document.
    const qreal clamped = std::clamp(zoom, 0.25, 4.0);
    if (qFuzzyCompare(clamped + 1, m_zoom + 1)) {
        return;
    }
    // The middle of the view stays where it is, because zooming is a thing
    // done to look closer at what is already being looked at -- anchoring at
    // the top instead sends the reader back up the piece every time.
    const qreal middle = (m_scrollY + height() / 2) / m_zoom;
    m_zoom = clamped;
    m_contentHeight = heightOfContent();
    Q_EMIT contentHeightChanged();
    setScrollY(middle * m_zoom - height() / 2);
    Q_EMIT zoomChanged();
    update();
}

int ScoreView::pageCount() const
{
    return m_session ? int(m_session->layout().pages.size()) : 0;
}

int ScoreView::currentPage() const
{
    if (!m_session || m_session->layout().isEmpty()) {
        return 0;
    }
    const Tab::Layout &layout = m_session->layout();
    // The page under the middle of the view, which is the one being read
    // rather than the one that happens to touch the top edge.
    const qreal at = (m_scrollY + height() / 2) / m_zoom;
    const qreal stride = layout.style.pageHeight + layout.style.pageGap;
    return std::clamp(int(at / stride), 0, int(layout.pages.size()) - 1);
}

qreal ScoreView::zoomToFit() const
{
    if (!m_session || m_session->layout().isEmpty() || width() <= 0) {
        return 1.0;
    }
    return std::clamp((width() - DeskMargin * 2) / m_session->layout().style.pageWidth, 0.25, 4.0);
}

qreal ScoreView::pageLeft() const
{
    if (!m_session) {
        return DeskMargin;
    }
    const qreal drawn = m_session->layout().style.pageWidth * m_zoom;
    // Centred where there is room, and pinned to the desk margin where there
    // is not, so that a page wider than the window scrolls off the right
    // rather than off both sides at once.
    return std::max(DeskMargin, (width() - drawn) / 2);
}

QRectF ScoreView::pageRect(int page) const
{
    if (!m_session) {
        return {};
    }
    const Tab::Style &style = m_session->layout().style;
    return QRectF(pageLeft(), m_session->layout().pageTop(page) * m_zoom - m_scrollY,
                  style.pageWidth * m_zoom, style.pageHeight * m_zoom);
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

    if (m_followPlayhead && bar >= 0 && height() > 0 && !m_session->layout().isEmpty()) {
        // Scroll only when the bar being played has left the view, so that a
        // reader is not chased down the page by something moving every bar.
        const Tab::Layout &layout = m_session->layout();
        for (const Tab::Page &sheet : layout.pages) {
            bool found = false;
            for (const Tab::System &system : sheet.systems) {
                const bool holds = std::any_of(system.bars.begin(), system.bars.end(),
                                               [bar](const Tab::LaidBar &laid) {
                                                   return laid.index == bar;
                                               });
                if (!holds) {
                    continue;
                }
                const qreal at = layout.pageTop(system.page) + system.y;
                const qreal top = (at - layout.style.systemSpacing) * m_zoom;
                const qreal bottom = (at + layout.systemHeight()) * m_zoom;
                if (top < m_scrollY || bottom > m_scrollY + height()) {
                    setScrollY(top - height() / 3);
                }
                found = true;
                break;
            }
            if (found) {
                break;
            }
        }
    }
    update();
}

/**
 * A point in the window read as a place in the document.
 *
 * Two things sit between them: the desk the page is centred on, and the size
 * it is being shown at. Everything the layout knows is in a page's own units,
 * so a click has to come back through both before it means anything.
 */
qreal ScoreView::documentX(qreal x) const
{
    return (x - pageLeft()) / m_zoom;
}

qreal ScoreView::documentY(qreal y) const
{
    return (y + m_scrollY) / m_zoom;
}

void ScoreView::mousePressEvent(QMouseEvent *event)
{
    if (!m_session) {
        QQuickPaintedItem::mousePressEvent(event);
        return;
    }
    m_session->placeCursorAt(documentX(event->position().x()),
                             documentY(event->position().y()),
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
    m_session->placeCursorAt(documentX(event->position().x()), documentY(event->position().y()),
                             true);
    event->accept();
}

/** Keeps the caret on screen when it is moved by the keyboard. */
void ScoreView::scrollCursorIntoView()
{
    if (!m_session || m_session->layout().isEmpty()) {
        return;
    }
    // Before the item has been given a size, everything is off the bottom of a
    // window nought pixels tall, and scrolling to show it lands somewhere in
    // the middle of page one. A document opens at the top of page one.
    if (height() <= 0) {
        return;
    }
    const Cursor cursor = m_session->cursor();
    qreal x = 0;
    qreal y = 0;
    if (!Tab::positionOf(m_session->layout(), cursor.bar, cursor.voice, cursor.beat,
                         &x, &y, nullptr)) {
        return;
    }
    const Tab::Layout &layout = m_session->layout();
    const qreal top = y * m_zoom;
    const qreal system = layout.systemHeight() * m_zoom;
    const qreal spacing = layout.style.systemSpacing * m_zoom;
    if (top < m_scrollY) {
        setScrollY(top - spacing);
    } else if (top + system > m_scrollY + height()) {
        setScrollY(top + system + spacing - height());
    }
}

void ScoreView::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickPaintedItem::geometryChange(newGeometry, oldGeometry);
    // The music no longer re-breaks when the window does -- a page is a fixed
    // width now. What moves is where the page sits on the desk, and how much
    // of it the scrollbar can reach.
    if (!qFuzzyCompare(newGeometry.width() + 1, oldGeometry.width() + 1)) {
        update();
    }
    if (!qFuzzyCompare(newGeometry.height() + 1, oldGeometry.height() + 1)) {
        setScrollY(m_scrollY);
    }
}

void ScoreView::paint(QPainter *painter)
{
    const Tab::Palette palette = paletteOfThePage();
    painter->fillRect(QRectF(0, 0, width(), height()), Desk);

    if (!m_session || m_session->layout().isEmpty()) {
        return;
    }
    const Tab::Layout &layout = m_session->layout();
    painter->setRenderHint(QPainter::Antialiasing, true);

    for (int index = 0; index < layout.pages.size(); ++index) {
        const QRectF sheet = pageRect(index);
        // Only the sheets the window is actually showing. A long score is
        // eighty of them and drawing the seventy-nine nobody is looking at is
        // the difference between scrolling smoothly and not.
        if (sheet.bottom() < -4 || sheet.top() > height() + 4) {
            continue;
        }
        drawSheet(*painter, sheet, palette.paper);

        painter->save();
        painter->translate(sheet.left(), sheet.top());
        painter->scale(m_zoom, m_zoom);
        // Nothing may run off its own page onto the desk or onto the next one.
        painter->setClipRect(QRectF(0, 0, layout.style.pageWidth, layout.style.pageHeight));

        paintSelection(*painter, index, palette);
        // The bar being played is lit by the painting rather than here: it is
        // the page that knows where a bar is, and the numbers inside it are
        // drawn in a colour of their own.
        Tab::paintPage(*painter, layout, index, palette, m_highlighted);
        paintCaret(*painter, index, palette);

        painter->restore();
    }
}

/** The wash behind a selection, in the coordinates of the page holding it. */
void ScoreView::paintSelection(QPainter &painter, int page, const Tab::Palette &palette)
{
    if (!m_session->hasSelection()) {
        return;
    }
    const Tab::Layout &layout = m_session->layout();
    // Behind the music, like the played bar, and in the same colour the caret
    // uses -- it is the caret, widened.
    const Editing::Range range = m_session->selection();
    QColor wash = palette.accent;
    wash.setAlphaF(0.18);
    const qreal padding = layout.style.stringSpacing * 0.6;

    for (const Tab::System &system : layout.pages.at(page).systems) {
        for (const Tab::LaidBar &bar : system.bars) {
            if (bar.index < range.from.bar || bar.index > range.to.bar) {
                continue;
            }
            const qreal barLeft = layout.style.margin + bar.x;
            qreal first = 0;
            qreal last = 0;
            bool any = false;
            for (const Tab::LaidBeat &beat : bar.beats) {
                if (beat.voice != range.from.voice || !range.holds(bar.index, beat.index)) {
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
            // Where the selection carries on past this bar it is drawn to the
            // barline, so a run of bars reads as one block rather than as a row
            // of separate patches.
            const qreal from = bar.index > range.from.bar ? barLeft : first - padding;
            const qreal to = bar.index < range.to.bar ? barLeft + bar.width : last + padding;
            painter.fillRect(
                QRectF(from, system.y - padding, to - from, layout.systemHeight() + padding),
                wash);
        }
    }
}

/** The caret, drawn last so it is never behind a fret number. */
void ScoreView::paintCaret(QPainter &painter, int page, const Tab::Palette &palette)
{
    const Tab::Layout &layout = m_session->layout();
    const Cursor cursor = m_session->cursor();
    qreal caretX = 0;
    qreal caretY = 0;
    if (!Tab::positionOf(layout, cursor.bar, cursor.voice, cursor.beat, &caretX, &caretY,
                         nullptr)) {
        return;
    }
    // positionOf answers in document coordinates, and this is drawing inside
    // one page: a caret two pages down would otherwise be drawn on this one.
    const qreal top = caretY - layout.pageTop(page);
    if (top < 0 || top > layout.style.pageHeight) {
        return;
    }

    const int fromTop = layout.strings - 1 - std::clamp(cursor.string, 0, layout.strings - 1);
    const qreal centre = top + fromTop * layout.style.stringSpacing;

    QColor caret = palette.accent;
    painter.setPen(QPen(caret, hasActiveFocus() ? 1.6 : 0.8));
    caret.setAlphaF(hasActiveFocus() ? 0.22 : 0.10);
    painter.setBrush(caret);
    painter.drawRoundedRect(QRectF(caretX - 7, centre - layout.style.stringSpacing / 2 - 1, 14,
                                   layout.style.stringSpacing + 2),
                            2.5, 2.5);
    painter.setBrush(Qt::NoBrush);
}
