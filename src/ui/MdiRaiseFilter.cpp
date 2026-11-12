// SPDX-FileCopyrightText: 2026 the router-console authors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "MdiRaiseFilter.h"

#include <QMouseEvent>
#include <QQuickItem>
#include <QQuickWindow>

namespace {
constexpr auto kAreaName = "mdiArea";
constexpr auto kFrameProperty = "windowKey";

QQuickItem *findByName(QQuickItem *root, const char *name)
{
    if (!root)
        return nullptr;
    if (root->objectName() == QLatin1String(name))
        return root;
    const QList<QQuickItem *> children = root->childItems();
    for (QQuickItem *child : children) {
        if (QQuickItem *found = findByName(child, name))
            return found;
    }
    return nullptr;
}
} // namespace

MdiRaiseFilter::MdiRaiseFilter(QQuickWindow *window, QObject *parent)
    : QObject(parent)
    , m_window(window)
{
    if (m_window)
        m_window->installEventFilter(this);
}

QQuickItem *MdiRaiseFilter::mdiArea() const
{
    if (!m_window)
        return nullptr;
    return findByName(m_window->contentItem(), kAreaName);
}

QQuickItem *MdiRaiseFilter::frameAt(QQuickItem *area, const QPointF &scenePosition) const
{
    QQuickItem *best = nullptr;
    double bestZ = 0;
    const QList<QQuickItem *> children = area->childItems();
    for (QQuickItem *child : children) {
        if (child->property(kFrameProperty).toString().isEmpty())
            continue;
        if (!child->isVisible())
            continue;
        const QPointF local = child->mapFromScene(scenePosition);
        if (!child->contains(local))
            continue;
        if (!best || child->z() > bestZ) {
            best = child;
            bestZ = child->z();
        }
    }
    return best;
}

bool MdiRaiseFilter::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() != QEvent::MouseButtonPress || watched != m_window)
        return QObject::eventFilter(watched, event);

    auto *mouse = static_cast<QMouseEvent *>(event);
    QQuickItem *area = mdiArea();
    if (!area)
        return false;

    QQuickItem *frame = frameAt(area, mouse->position());
    if (frame)
        QMetaObject::invokeMethod(frame, "raise");

    return false;
}
