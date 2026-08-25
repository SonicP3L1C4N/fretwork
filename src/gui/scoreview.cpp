// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "scoreview.h"

#include <QGuiApplication>
#include <QPainter>
#include <QPalette>

#include <algorithm>

namespace
{
/** The colours of the page, taken from the desktop rather than assumed. */
Tab::Palette paletteFor(const QPalette &desktop)
{
    Tab::Palette palette;
    palette.paper = desktop.color(QPalette::Base);
    palette.ink = desktop.color(QPalette::Text);
    palette.staff = desktop.color(QPalette::Text);
    palette.staff.setAlphaF(0.45);
    palette.faint = desktop.color(QPalette::Text);
    palette.faint.setAlphaF(0.5);
    palette.accent = desktop.color(QPalette::Link);
    return palette;
}
}

ScoreView::ScoreView(QQuickItem *parent)
    : QQuickPaintedItem(parent)
{
    setFlag(ItemHasContents, true);
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
    return page.systems.constLast().y + layout.staffHeight() + layout.style.systemSpacing;
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
            const qreal bottom = system.y + m_session->layout().staffHeight();
            if (top < m_scrollY || bottom > m_scrollY + height()) {
                setScrollY(top - height() / 3);
            }
            break;
        }
    }
    update();
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
    const Tab::Palette palette = paletteFor(QGuiApplication::palette());
    painter->fillRect(QRectF(0, 0, width(), height()), palette.paper);

    if (!m_session || m_session->layout().isEmpty()) {
        return;
    }
    const Tab::Layout &layout = m_session->layout();

    painter->save();
    painter->translate(0, -m_scrollY);

    if (m_highlighted >= 0) {
        // The bar being played, marked behind the music rather than over it.
        QColor wash = palette.accent;
        wash.setAlphaF(0.13);
        for (const Tab::System &system : layout.pages.constFirst().systems) {
            for (const Tab::LaidBar &bar : system.bars) {
                if (bar.index != m_highlighted) {
                    continue;
                }
                painter->fillRect(QRectF(layout.style.margin + bar.x,
                                         system.y - layout.style.stringSpacing,
                                         bar.width,
                                         layout.staffHeight() + layout.style.stringSpacing * 2),
                                  wash);
            }
        }
    }

    Tab::paintPage(*painter, layout, 0, palette);
    painter->restore();
}
