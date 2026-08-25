// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#pragma once

#include "session.h"
#include "tabpainter.h"

#include <QQmlEngine>
#include <QQuickPaintedItem>

/**
 * The score on screen.
 *
 * A painted item rather than a tree of QML elements, because a page of
 * tablature is a few thousand small marks and QML would make an object of each
 * one. It paints only what is visible: the item is the size of the viewport and
 * scrolls by moving what it draws rather than by being enormous and clipped,
 * which is the difference between scrolling a 400-bar score smoothly and not.
 *
 * Everything it draws comes from `Tab::paintPage`, the same function that
 * writes the PDF, so a window and a printout cannot disagree.
 */
class ScoreView : public QQuickPaintedItem
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(Session *session READ session WRITE setSession NOTIFY sessionChanged)
    Q_PROPERTY(qreal scrollY READ scrollY WRITE setScrollY NOTIFY scrollYChanged)
    Q_PROPERTY(qreal contentHeight READ contentHeight NOTIFY contentHeightChanged)
    Q_PROPERTY(bool followPlayhead READ followPlayhead WRITE setFollowPlayhead
                   NOTIFY followPlayheadChanged)

public:
    explicit ScoreView(QQuickItem *parent = nullptr);

    void paint(QPainter *painter) override;

    Session *session() const;
    void setSession(Session *session);

    qreal scrollY() const;
    void setScrollY(qreal scrollY);

    qreal contentHeight() const;

    bool followPlayhead() const;
    void setFollowPlayhead(bool follow);

Q_SIGNALS:
    void sessionChanged();
    void scrollYChanged();
    void contentHeightChanged();
    void followPlayheadChanged();

protected:
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;
    void mousePressEvent(QMouseEvent *event) override;

private:
    void onLayoutChanged();
    void onPositionChanged();
    void scrollCursorIntoView();
    qreal heightOfContent() const;

    Session *m_session = nullptr;
    qreal m_scrollY = 0;
    qreal m_contentHeight = 0;
    bool m_followPlayhead = true;
    int m_highlighted = -1;
};
