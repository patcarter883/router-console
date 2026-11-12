// SPDX-FileCopyrightText: 2026 the router-console authors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "TrafficHistory.h"

#include <algorithm>
#include <cmath>

namespace {

qint64 floorTo(qint64 value, qint64 width)
{
    const qint64 remainder = value % width;
    return value - (remainder < 0 ? remainder + width : remainder);
}

} // namespace

void TrafficHistory::append(const QString &ifName, qint64 atMs, double rxBytesPerSec,
                            double txBytesPerSec)
{
    Series &series = m_series[ifName];
    series.recent.append(Sample{atMs, std::max(0.0, rxBytesPerSec), std::max(0.0, txBytesPerSec)});
    series.newest = atMs;
    compact(series, atMs);
}

void TrafficHistory::clear()
{
    m_series.clear();
}

void TrafficHistory::clear(const QString &ifName)
{
    m_series.remove(ifName);
}

int TrafficHistory::recentCount(const QString &ifName) const
{
    return m_series.value(ifName).recent.size();
}

int TrafficHistory::coarseCount(const QString &ifName) const
{
    return m_series.value(ifName).coarse.size();
}

qint64 TrafficHistory::oldestMs(const QString &ifName) const
{
    const auto it = m_series.constFind(ifName);
    if (it == m_series.constEnd())
        return -1;
    if (!it->coarse.isEmpty())
        return it->coarse.first().startMs;
    if (!it->recent.isEmpty())
        return it->recent.first().atMs;
    return -1;
}

qint64 TrafficHistory::newestMs(const QString &ifName) const
{
    return m_series.value(ifName).newest;
}

qint64 TrafficHistory::retainedSpanMs(const QString &ifName) const
{
    const qint64 oldest = oldestMs(ifName);
    const qint64 newest = newestMs(ifName);
    if (oldest < 0 || newest < 0)
        return 0;
    return newest - oldest;
}

void TrafficHistory::compact(Series &series, qint64 nowMs)
{
    const qint64 rawCutoff = nowMs - recentSpanMs;

    qsizetype drop = 0;
    while (drop < series.recent.size() && series.recent.at(drop).atMs < rawCutoff)
        ++drop;

    if (drop > 0) {
        for (qsizetype i = 0; i < drop; ++i) {
            const Sample &sample = series.recent.at(i);
            const qint64 start = floorTo(sample.atMs, coarseBucketMs);
            if (!series.coarse.isEmpty() && series.coarse.last().startMs == start) {
                Bucket &bucket = series.coarse.last();
                // Weighted mean: every coarse bucket carries its sample count.
                const double weight = bucket.samples;
                bucket.rx = (bucket.rx * weight + sample.rx) / (weight + 1.0);
                bucket.tx = (bucket.tx * weight + sample.tx) / (weight + 1.0);
                bucket.samples += 1;
            } else {
                series.coarse.append(Bucket{start, sample.rx, sample.tx, 1});
            }
        }
        series.recent.remove(0, drop);
    }

    const qint64 coarseCutoff = nowMs - retentionMs - coarseBucketMs;
    qsizetype stale = 0;
    while (stale < series.coarse.size() && series.coarse.at(stale).startMs < coarseCutoff)
        ++stale;
    if (stale > 0)
        series.coarse.remove(0, stale);
}

qint64 TrafficHistory::bucketWidthFor(qint64 windowMs, int targetPoints)
{
    // Multiples of the coarse tier's 10 s so merged buckets never straddle a slot.
    static const qint64 ladder[] = {1000,   2000,   5000,    10000,   20000,
                                    30000,  60000,  120000,  300000,  600000,
                                    1800000};
    const qint64 points = std::max(1, targetPoints);
    qint64 target = std::max<qint64>(1000, (windowMs + points - 1) / points);
    // The coarse tier stores 10 s buckets; finer slots could not be filled from it.
    if (windowMs > recentSpanMs)
        target = std::max<qint64>(target, coarseBucketMs);
    for (const qint64 width : ladder) {
        if (width >= target)
            return width;
    }
    return ladder[std::size(ladder) - 1];
}

void TrafficHistory::addMeasurement(QVector<Bucket> &grid, qint64 gridStart, qint64 width,
                                    qint64 atMs, double rx, double tx, int count)
{
    if (atMs < gridStart)
        return;
    const qint64 slotIndex = (atMs - gridStart) / width;
    if (slotIndex < 0 || slotIndex >= grid.size())
        return;
    Bucket &slot = grid[static_cast<qsizetype>(slotIndex)];
    slot.rx += rx * count;
    slot.tx += tx * count;
    slot.samples += count;
}

TrafficHistory::Trace TrafficHistory::trace(const QString &ifName, qint64 nowMs, qint64 windowMs,
                                            qint64 bucketMsOverride) const
{
    Trace result;
    result.windowMs = std::max<qint64>(1000, windowMs);
    result.toMs = nowMs;
    result.fromMs = nowMs - result.windowMs;
    result.bucketMs = bucketMsOverride > 0 ? bucketMsOverride : bucketWidthFor(result.windowMs);

    const qint64 width = result.bucketMs;
    const qint64 gridStart = floorTo(result.fromMs, width);
    const qint64 slotCount = (nowMs - gridStart) / width + 1;
    result.points.resize(static_cast<qsizetype>(slotCount));
    for (qsizetype i = 0; i < slotCount; ++i)
        result.points[i].startMs = gridStart + static_cast<qint64>(i) * width;

    const auto it = m_series.constFind(ifName);
    if (it == m_series.constEnd()) {
        for (Bucket &slot : result.points) {
            slot.rx = -1;
            slot.tx = -1;
        }
        return result;
    }

    // Weighted accumulators, then a single mean pass so both tiers merge by
    // sample count instead of by whichever tier happens to be newer.
    for (const Bucket &bucket : it->coarse) {
        if (bucket.startMs < result.fromMs || bucket.startMs > nowMs)
            continue;
        addMeasurement(result.points, gridStart, width, bucket.startMs, bucket.rx, bucket.tx,
                       bucket.samples);
    }
    for (const Sample &sample : it->recent) {
        if (sample.atMs < result.fromMs || sample.atMs > nowMs)
            continue;
        ++result.rawSamples;
        addMeasurement(result.points, gridStart, width, sample.atMs, sample.rx, sample.tx, 1);
    }

    for (Bucket &slot : result.points) {
        if (slot.samples == 0) {
            slot.rx = -1;
            slot.tx = -1;
            continue;
        }
        slot.rx /= slot.samples;
        slot.tx /= slot.samples;
    }

    result.coveredFromMs = oldestMs(ifName);
    return result;
}
