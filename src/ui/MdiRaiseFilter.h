// SPDX-FileCopyrightText: 2026 the router-console authors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <QObject>
#include <QPointer>

class QQuickItem;
class QQuickWindow;

// Click-to-front for the MDI desktop. A view's own mouse areas and item views
// consume presses before any ancestor handler sees them, so the raise is done on
// the way in: the filter finds the top-most frame under the press and calls its
// QML raise(), then lets the event continue to the view untouched.
class MdiRaiseFilter : public QObject
{
    Q_OBJECT

public:
    explicit MdiRaiseFilter(QQuickWindow *window, QObject *parent = nullptr);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QQuickItem *mdiArea() const;
    QQuickItem *frameAt(QQuickItem *area, const QPointF &scenePosition) const;

    QPointer<QQuickWindow> m_window;
};
