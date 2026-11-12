// SPDX-FileCopyrightText: 2026 the router-console authors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <QHash>
#include <QString>
#include <QVector>

// Per-interface RX/TX rate history behind the interface graph. Two tiers keep
// 24 h in bounded memory: 1 Hz samples for the last `recentSpanMs`, and 10 s
// buckets behind them up to `retentionMs`. Queries re-bucket either tier onto a
// fixed grid whose width the requested window selects, so a 1 min view and a
// 24 h view draw the same number of points.
//
// No I/O and no QObject: the bucketing and retention math is exercised without
// a router (tests/m16_traffic_history.cpp).
class TrafficHistory
{
public:
    struct Sample {
        qint64 atMs = 0;
        double rx = 0;
        double tx = 0;
    };

    // One grid slot. `samples` is the number of 1 Hz measurements behind it, 0
    // when the slot falls in a gap (the graph breaks its line there).
    struct Bucket {
        qint64 startMs = 0;
        double rx = 0;
        double tx = 0;
        int samples = 0;
    };

    struct Trace {
        qint64 windowMs = 0;
        qint64 bucketMs = 0;
        qint64 fromMs = 0;   // window start requested
        qint64 toMs = 0;     // window end (the sample clock "now")
        qint64 coveredFromMs = -1; // oldest retained measurement, -1 when empty
        int rawSamples = 0;  // 1 Hz measurements inside the window
        QVector<Bucket> points; // grid-aligned, ascending, gaps included
    };

    static constexpr qint64 recentSpanMs = 10 * 60 * 1000;
    static constexpr qint64 coarseBucketMs = 10 * 1000;
    static constexpr qint64 retentionMs = 24LL * 60 * 60 * 1000;
    static constexpr int maxPoints = 600;

    void append(const QString &ifName, qint64 atMs, double rxBytesPerSec, double txBytesPerSec);
    void clear();
    void clear(const QString &ifName);

    bool isEmpty() const { return m_series.isEmpty(); }
    int recentCount(const QString &ifName) const;
    int coarseCount(const QString &ifName) const;
    qint64 oldestMs(const QString &ifName) const;
    qint64 newestMs(const QString &ifName) const;
    qint64 retainedSpanMs(const QString &ifName) const;

    // Bucket width for one window: the smallest ladder step that keeps the
    // point count at or under `maxPoints` and, once the coarse tier is in play,
    // a multiple of `coarseBucketMs`.
    static qint64 bucketWidthFor(qint64 windowMs, int targetPoints = maxPoints);

    // `bucketMsOverride` forces a width (tests compare the same window at two
    // widths); 0 selects `bucketWidthFor(windowMs)`.
    Trace trace(const QString &ifName, qint64 nowMs, qint64 windowMs,
                qint64 bucketMsOverride = 0) const;

private:
    struct Series {
        QVector<Sample> recent;
        QVector<Bucket> coarse;
        qint64 newest = -1;
    };

    void compact(Series &series, qint64 nowMs);
    static void addMeasurement(QVector<Bucket> &grid, qint64 gridStart, qint64 width,
                               qint64 atMs, double rx, double tx, int count);

    QHash<QString, Series> m_series;
};
