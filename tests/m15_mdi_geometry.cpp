// SPDX-FileCopyrightText: 2026 the router-console authors
// SPDX-License-Identifier: AGPL-3.0-or-later

// MDI geometry: the cascade/tile math, the on-screen clamp, and the QSettings
// store that makes a window's place survive a restart. No router involved.

#include "ui/MdiGeometry.h"

#include <QCoreApplication>
#include <QDebug>
#include <QRect>
#include <QTemporaryDir>

#include <memory>

namespace {
int failures = 0;

void check(bool condition, const char *what, int line)
{
    if (condition)
        return;
    qWarning().noquote() << QStringLiteral("FAIL line %1: %2").arg(line).arg(QString::fromLatin1(what));
    ++failures;
}

#define CHECK(condition) check((condition), #condition, __LINE__)

bool within(const QRect &rect, const QRect &area)
{
    return area.contains(rect);
}

bool overlaps(const QRect &a, const QRect &b)
{
    return a.intersects(b);
}
} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("open-broadcast"));
    QCoreApplication::setApplicationName(QStringLiteral("router-console-m15-test"));

    QTemporaryDir dir;
    if (!dir.isValid())
        return 1;
    const QString settingsFile = dir.filePath(QStringLiteral("mdi.ini"));

    const QRect area(0, 0, 1000, 700);
    const QSize minimum(360, 240);

    // --- cascade -----------------------------------------------------------
    const QVector<QRect> cascade = MdiGeometryStore::cascadeRects(4, area, minimum);
    CHECK(cascade.size() == 4);
    for (const QRect &rect : cascade) {
        CHECK(within(rect, area));
        CHECK(rect.width() >= minimum.width());
        CHECK(rect.height() >= minimum.height());
    }
    CHECK(cascade.size() == 4 && cascade[1].x() > cascade[0].x());
    CHECK(cascade.size() == 4 && cascade[1].y() > cascade[0].y());
    CHECK(cascade.size() == 4 && cascade[3].x() == cascade[0].x() + 3 * 28);

    // --- tile --------------------------------------------------------------
    const QVector<QRect> tile = MdiGeometryStore::tileRects(4, area);
    CHECK(tile.size() == 4);
    for (const QRect &rect : tile)
        CHECK(within(rect, area));
    if (tile.size() == 4) {
        CHECK(!overlaps(tile[0], tile[1]));
        CHECK(!overlaps(tile[0], tile[2]));
        CHECK(!overlaps(tile[0], tile[3]));
        CHECK(tile[1].right() > tile[0].right());
        CHECK(tile[2].bottom() > tile[0].bottom());
    }

    const QVector<QRect> three = MdiGeometryStore::tileRects(3, area);
    CHECK(three.size() == 3);
    for (const QRect &rect : three)
        CHECK(within(rect, area));
    CHECK(MdiGeometryStore::tileRects(0, area).isEmpty());
    CHECK(MdiGeometryStore::cascadeRects(0, area, minimum).isEmpty());

    // --- clamp -------------------------------------------------------------
    const QRect offscreen(-400, -300, 900, 620);
    const QRect clamped = MdiGeometryStore::clampToArea(offscreen, minimum, area);
    CHECK(within(clamped, area));
    CHECK(clamped.width() >= minimum.width());

    const QRect tiny(40, 40, 20, 20);
    const QRect grown = MdiGeometryStore::clampToArea(tiny, minimum, area);
    CHECK(grown.width() >= minimum.width());
    CHECK(grown.height() >= minimum.height());
    CHECK(within(grown, area));

    const QRect huge(10, 10, 3000, 2000);
    const QRect shrunk = MdiGeometryStore::clampToArea(huge, minimum, area);
    CHECK(shrunk.width() <= area.width());
    CHECK(shrunk.height() <= area.height());

    // --- persistence -------------------------------------------------------
    const QRect stored(120, 90, 640, 420);
    {
        MdiGeometryStore store(settingsFile);
        store.save(QStringLiteral("tool/log"), stored);
        store.save(QStringLiteral("interface/br-lan"), QRect(10, 12, 500, 300));
        CHECK(store.load(QStringLiteral("tool/log"), QRect(0, 0, 1, 1)) == QRectF(stored));
    }

    // A second instance on the same file is the restart: the geometry is there.
    {
        MdiGeometryStore store(settingsFile);
        CHECK(store.load(QStringLiteral("tool/log"), QRect(0, 0, 1, 1)) == QRectF(stored));
        CHECK(store.load(QStringLiteral("interface/br-lan"), QRect(0, 0, 1, 1))
              == QRectF(10, 12, 500, 300));
        CHECK(store.load(QStringLiteral("tool/never-opened"), QRect(7, 8, 9, 10))
              == QRectF(7, 8, 9, 10));

        store.forget(QStringLiteral("tool/log"));
        CHECK(store.load(QStringLiteral("tool/log"), QRect(1, 2, 3, 4)) == QRectF(1, 2, 3, 4));

        // The QML-facing helpers return plain rect lists the frames consume.
        const QVariantList cascadeList = store.cascade(3, QRectF(area), QSizeF(minimum));
        CHECK(cascadeList.size() == 3);
        const QVariantList tileList = store.tile(2, QRectF(area));
        CHECK(tileList.size() == 2);
        CHECK(tileList.value(0).toRect().width() > 0);
    }

    if (failures > 0) {
        qWarning() << "m15-mdi-geometry:" << failures << "check(s) failed";
        return 1;
    }
    qInfo() << "m15-mdi-geometry: cascade, tile, clamp and persisted geometry all hold";
    return 0;
}
