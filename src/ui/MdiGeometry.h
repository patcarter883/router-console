// SPDX-FileCopyrightText: 2026 the router-console authors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <QObject>
#include <QRect>
#include <QSettings>
#include <QSize>
#include <QString>
#include <QVariantList>
#include <QVector>

#include <memory>

// Window geometry for the MDI layer: the pure layout math (cascade/tile/clamp)
// plus the QSettings-backed store that makes each window remember its place.
// The QML frames own live geometry; this class only computes and persists it.
class MdiGeometryStore : public QObject
{
    Q_OBJECT

public:
    explicit MdiGeometryStore(QObject *parent = nullptr);
    // Test seam: an explicit settings file instead of the application's.
    explicit MdiGeometryStore(const QString &settingsFile, QObject *parent = nullptr);
    ~MdiGeometryStore() override;

    Q_INVOKABLE QRectF load(const QString &key, const QRectF &fallback) const;
    Q_INVOKABLE void save(const QString &key, const QRectF &rect);
    Q_INVOKABLE void forget(const QString &key);
    Q_INVOKABLE QVariantList cascade(int count, const QRectF &area,
                                     const QSizeF &minimumSize) const;
    Q_INVOKABLE QVariantList tile(int count, const QRectF &area) const;

    static QRect clampToArea(const QRect &rect, const QSize &minimumSize, const QRect &area);
    static QVector<QRect> cascadeRects(int count, const QRect &area, const QSize &minimumSize);
    static QVector<QRect> tileRects(int count, const QRect &area);

private:
    std::unique_ptr<QSettings> m_settings;
};
