// SPDX-FileCopyrightText: 2026 the router-console authors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "MdiGeometry.h"

#include <QtMath>

namespace {
constexpr int kMinWidth = 240;
constexpr int kMinHeight = 160;
constexpr int kCascadeMargin = 16;
constexpr int kCascadeStep = 28;
constexpr int kCascadeSlots = 8;

QString settingsKey(const QString &key)
{
    return QStringLiteral("mdi/%1/geometry").arg(key);
}
} // namespace

MdiGeometryStore::MdiGeometryStore(QObject *parent)
    : QObject(parent)
    , m_settings(std::make_unique<QSettings>())
{
}

MdiGeometryStore::MdiGeometryStore(const QString &settingsFile, QObject *parent)
    : QObject(parent)
    , m_settings(std::make_unique<QSettings>(settingsFile, QSettings::IniFormat))
{
}

MdiGeometryStore::~MdiGeometryStore() = default;

QRectF MdiGeometryStore::load(const QString &key, const QRectF &fallback) const
{
    const QVariant stored = m_settings->value(settingsKey(key));
    if (!stored.isValid())
        return fallback;
    const QRect rect = stored.toRect();
    if (rect.width() <= 0 || rect.height() <= 0)
        return fallback;
    return QRectF(rect);
}

void MdiGeometryStore::save(const QString &key, const QRectF &rect)
{
    m_settings->setValue(settingsKey(key), rect.toRect());
    m_settings->sync();
}

void MdiGeometryStore::forget(const QString &key)
{
    m_settings->remove(settingsKey(key));
}

QVariantList MdiGeometryStore::cascade(int count, const QRectF &area,
                                       const QSizeF &minimumSize) const
{
    QVariantList out;
    const QVector<QRect> rects = cascadeRects(count, area.toRect(), minimumSize.toSize());
    out.reserve(rects.size());
    for (const QRect &rect : rects)
        out.append(QVariant::fromValue(rect));
    return out;
}

QVariantList MdiGeometryStore::tile(int count, const QRectF &area) const
{
    QVariantList out;
    const QVector<QRect> rects = tileRects(count, area.toRect());
    out.reserve(rects.size());
    for (const QRect &rect : rects)
        out.append(QVariant::fromValue(rect));
    return out;
}

// A remembered rect from a smaller screen must still be reachable: shrink to the
// area and pull the top-left corner back inside.
QRect MdiGeometryStore::clampToArea(const QRect &rect, const QSize &minimumSize, const QRect &area)
{
    if (area.width() <= 0 || area.height() <= 0)
        return rect;

    const int minWidth = qMin(qMax(minimumSize.width(), kMinWidth), area.width());
    const int minHeight = qMin(qMax(minimumSize.height(), kMinHeight), area.height());

    QRect out = rect;
    out.setWidth(qBound(minWidth, out.width(), area.width()));
    out.setHeight(qBound(minHeight, out.height(), area.height()));
    out.moveLeft(qBound(area.left(), out.left(), area.right() - out.width() + 1));
    out.moveTop(qBound(area.top() + kCascadeMargin, out.top(),
                       area.bottom() - out.height() + 1));
    return out;
}

QVector<QRect> MdiGeometryStore::cascadeRects(int count, const QRect &area,
                                              const QSize &minimumSize)
{
    QVector<QRect> out;
    if (count <= 0 || area.width() <= 0 || area.height() <= 0)
        return out;

    const int width = qMax(minimumSize.width(), int(area.width() * 0.62));
    const int height = qMax(minimumSize.height(), int(area.height() * 0.66));
    QRect base(area.left() + kCascadeMargin, area.top() + kCascadeMargin, width, height);
    base = clampToArea(base, minimumSize, area);

    for (int i = 0; i < count; ++i) {
        const int slot = i % kCascadeSlots;
        QRect rect = base;
        rect.moveLeft(base.left() + slot * kCascadeStep);
        rect.moveTop(base.top() + slot * kCascadeStep);
        out.append(clampToArea(rect, minimumSize, area));
    }
    return out;
}

QVector<QRect> MdiGeometryStore::tileRects(int count, const QRect &area)
{
    QVector<QRect> out;
    if (count <= 0 || area.width() <= 0 || area.height() <= 0)
        return out;

    const int columns = qMax(1, int(std::ceil(std::sqrt(double(count)))));
    const int rows = (count + columns - 1) / columns;
    const int gap = 3;

    for (int i = 0; i < count; ++i) {
        const int column = i % columns;
        const int row = i / columns;

        // Cell boundaries derived from the index so cells stay exact even when
        // the area does not divide evenly.
        const int left = area.left() + area.width() * column / columns;
        const int right = area.left() + area.width() * (column + 1) / columns;
        const int top = area.top() + area.height() * row / rows;
        const int bottom = area.top() + area.height() * (row + 1) / rows;

        QRect cell(left, top, right - left, bottom - top);
        cell.adjust(gap, gap, -gap, -gap);
        if (cell.width() > 0 && cell.height() > 0)
            out.append(cell);
    }
    return out;
}
