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
 * writes the PDF, so a window and a printout cannot disagree. They now do not
 * disagree about the page either: the same A4 sheets, broken in the same
 * places, stacked down the window on a desk with a gap between them. Which is
 * why there is a zoom -- a page is a fixed width, so how big it is on screen
 * has to be something a reader can say.
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
    Q_PROPERTY(qreal zoom READ zoom WRITE setZoom NOTIFY zoomChanged)
    Q_PROPERTY(int pageCount READ pageCount NOTIFY contentHeightChanged)
    Q_PROPERTY(int currentPage READ currentPage NOTIFY scrollYChanged)

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

    qreal zoom() const;
    void setZoom(qreal zoom);

    /** How many sheets there are, and which one the reader is looking at. */
    int pageCount() const;
    int currentPage() const;

    /** The width a page has to be drawn at to fill the window, less its desk. */
    Q_INVOKABLE qreal zoomToFit() const;

Q_SIGNALS:
    void sessionChanged();
    void scrollYChanged();
    void contentHeightChanged();
    void followPlayheadChanged();
    void zoomChanged();

protected:
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;

private:
    void onLayoutChanged();
    void onPositionChanged();
    void scrollCursorIntoView();
    qreal heightOfContent() const;

    void paintSelection(QPainter &painter, int page, const Tab::Palette &palette);
    void paintCaret(QPainter &painter, int page, const Tab::Palette &palette);

    /** A point in the window, read as a place in the document. */
    qreal documentX(qreal x) const;
    qreal documentY(qreal y) const;

    /** Where a page sits in the window, and how big it is there. */
    QRectF pageRect(int page) const;
    /** The desk either side of a page, which is where it is centred. */
    qreal pageLeft() const;

    Session *m_session = nullptr;
    qreal m_zoom = 1.0;
    qreal m_scrollY = 0;
    qreal m_contentHeight = 0;
    bool m_followPlayhead = true;
    int m_highlighted = -1;
};
