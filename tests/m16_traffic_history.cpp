// SPDX-FileCopyrightText: 2026 the router-console authors
// SPDX-License-Identifier: AGPL-3.0-or-later

// Traffic history store: two-tier retention (1 Hz recent, 10 s coarse to 24 h),
// grid bucketing per requested window, mean conservation when tiers merge, and
// the cost of keeping 24 h. No router involved.

#include "models/TrafficHistory.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QTextStream>

#include <cmath>
#include <cstdlib>

namespace {

int failures = 0;

void check(bool condition, const char *what, int line)
{
    if (condition)
        return;
    QTextStream(stderr) << "FAIL line " << line << ": " << what << Qt::endl;
    ++failures;
}

#define CHECK(condition) check((condition), #condition, __LINE__)

bool near(double a, double b, double tolerance = 1e-9)
{
    return std::fabs(a - b) <= tolerance;
}

// Samples present in the trace, weighted by their measurement count.
double weightedMean(const TrafficHistory::Trace &trace, bool rx)
{
    double sum = 0;
    double weight = 0;
    for (const TrafficHistory::Bucket &bucket : trace.points) {
        if (bucket.samples == 0)
            continue;
        sum += (rx ? bucket.rx : bucket.tx) * bucket.samples;
        weight += bucket.samples;
    }
    return weight > 0 ? sum / weight : 0;
}

int filledSlots(const TrafficHistory::Trace &trace)
{
    int count = 0;
    for (const TrafficHistory::Bucket &bucket : trace.points) {
        if (bucket.samples > 0)
            ++count;
    }
    return count;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTextStream out(stdout);

    // --- one sample per grid slot, values land where they were written ------
    {
        TrafficHistory history;
        for (int i = 0; i < 60; ++i)
            history.append(QStringLiteral("br-lan"), i * 1000, i * 10.0, i * 20.0);

        const TrafficHistory::Trace trace = history.trace(QStringLiteral("br-lan"), 59000, 60000);
        out << "1 min: bucketMs=" << trace.bucketMs << " slots=" << trace.points.size()
            << " rawSamples=" << trace.rawSamples << Qt::endl;
        CHECK(trace.bucketMs == 1000);
        CHECK(trace.points.size() >= 60 && trace.points.size() <= 61);
        CHECK(trace.rawSamples == 60);

        // Slot i covers [i, i+1) s and holds exactly the sample written at i s.
        int matched = 0;
        for (const TrafficHistory::Bucket &bucket : trace.points) {
            const int second = static_cast<int>(bucket.startMs / 1000);
            if (second < 0 || second > 59 || bucket.samples == 0)
                continue;
            if (near(bucket.rx, second * 10.0) && near(bucket.tx, second * 20.0))
                ++matched;
        }
        CHECK(matched == 60);
        CHECK(near(trace.points.first().startMs % 1000, 0));
    }

    // --- the recent tier is capped and folds into 10 s coarse buckets -------
    {
        TrafficHistory history;
        const qint64 hour = 60 * 60 * 1000;
        const int samples = 3 * 3600;
        for (int i = 0; i < samples; ++i)
            history.append(QStringLiteral("eth0"), i * 1000, 100.0, 50.0);

        const qint64 now = (samples - 1) * 1000;
        out << "3 h: recent=" << history.recentCount(QStringLiteral("eth0"))
            << " coarse=" << history.coarseCount(QStringLiteral("eth0"))
            << " retainedMs=" << history.retainedSpanMs(QStringLiteral("eth0")) << Qt::endl;

        CHECK(history.recentCount(QStringLiteral("eth0")) <= TrafficHistory::recentSpanMs / 1000 + 1);
        CHECK(history.coarseCount(QStringLiteral("eth0")) > 0);
        // Everything behind the 10 min recent tier, in 10 s buckets.
        CHECK(qAbs(history.coarseCount(QStringLiteral("eth0"))
                   - static_cast<int>((3 * hour - TrafficHistory::recentSpanMs)
                                      / TrafficHistory::coarseBucketMs)) <= 2);
        CHECK(history.retainedSpanMs(QStringLiteral("eth0")) >= 3 * hour - 1500);
        CHECK(history.oldestMs(QStringLiteral("eth0")) >= 0);
        CHECK(history.newestMs(QStringLiteral("eth0")) == now);

        // Retention: anything older than 24 h leaves memory.
        for (int i = samples; i < 29 * 3600; ++i)
            history.append(QStringLiteral("eth0"), i * 1000, 100.0, 50.0);
        const qint64 newest = 29LL * 3600 * 1000 - 1000;
        CHECK(history.retainedSpanMs(QStringLiteral("eth0"))
              <= TrafficHistory::retentionMs + 2 * TrafficHistory::coarseBucketMs);
        CHECK(newest - history.oldestMs(QStringLiteral("eth0"))
              <= TrafficHistory::retentionMs + 2 * TrafficHistory::coarseBucketMs);
        CHECK(history.coarseCount(QStringLiteral("eth0")) <= 24LL * 3600 * 1000
                                                                  / TrafficHistory::coarseBucketMs
                                                              + 2);
    }

    // --- coarse buckets keep the mean of the samples they absorbed ----------
    {
        TrafficHistory history;
        const qint64 span = 60 * 60 * 1000;
        double sum = 0;
        int count = 0;
        for (int i = 0; i < 3600; ++i) {
            // A ramp: every 10 s bucket has a different mean.
            const double rx = 1000.0 + i;
            history.append(QStringLiteral("br-lan"), i * 1000, rx, rx / 2);
            sum += rx;
            ++count;
        }

        const TrafficHistory::Trace trace = history.trace(QStringLiteral("br-lan"), 3599000, span);
        out << "1 h: bucketMs=" << trace.bucketMs << " slots=" << trace.points.size()
            << " filled=" << filledSlots(trace) << Qt::endl;
        CHECK(trace.bucketMs == 10000);
        CHECK(trace.points.size() >= 360 && trace.points.size() <= 362);
        CHECK(filledSlots(trace) >= 350);
        CHECK(near(weightedMean(trace, true), sum / count, 1e-6));
        CHECK(near(weightedMean(trace, false), sum / count / 2, 1e-6));

        // Every coarse bucket is the arithmetic mean of its ten 1 s samples.
        for (const TrafficHistory::Bucket &bucket : trace.points) {
            if (bucket.samples != 10)
                continue;
            double expected = 0;
            for (int k = 0; k < 10; ++k)
                expected += 1000.0 + static_cast<int>(bucket.startMs / 1000) + k;
            expected /= 10.0;
            CHECK(near(bucket.rx, expected, 1e-6));
            break;
        }
    }

    // --- the two tiers merge by sample count, not by tier -------------------
    {
        TrafficHistory history;
        const double early = 500.0;
        const double late = 1500.0;
        const qint64 jump = TrafficHistory::recentSpanMs + 30 * 1000;
        for (int ms = 0; ms < 10 * 1000; ms += 1000)
            history.append(QStringLiteral("br-lan"), ms, early, early);
        // Past the recent span: those samples are now 10 s buckets.
        for (int ms = static_cast<int>(jump); ms < static_cast<int>(jump) + 10000; ms += 1000)
            history.append(QStringLiteral("br-lan"), ms, late, late);

        CHECK(history.recentCount(QStringLiteral("br-lan")) == 10);
        CHECK(history.coarseCount(QStringLiteral("br-lan")) == 1);
        CHECK(!history.trace(QStringLiteral("br-lan"), jump + 9000, TrafficHistory::retentionMs)
                   .points.isEmpty());

        const TrafficHistory::Trace all =
            history.trace(QStringLiteral("br-lan"), jump + 9000, 60 * 60 * 1000);
        CHECK(near(weightedMean(all, true), (early + late) / 2, 1e-6));

        // The coarse bucket alone holds the early half.
        const TrafficHistory::Trace coarseOnly =
            history.trace(QStringLiteral("br-lan"), jump + 9000, 60 * 60 * 1000, 10000);
        CHECK(coarseOnly.points.size() >= 360 && coarseOnly.points.size() <= 362);
        CHECK(near(weightedMean(coarseOnly, true), (early + late) / 2, 1e-6));
    }

    // --- a window with no data is a gap, not a zero -------------------------
    {
        TrafficHistory history;
        const TrafficHistory::Trace empty =
            history.trace(QStringLiteral("nothing"), 500000, 60000);
        CHECK(!empty.points.isEmpty());
        CHECK(empty.rawSamples == 0);
        CHECK(empty.coveredFromMs == -1);
        for (const TrafficHistory::Bucket &bucket : empty.points)
            CHECK(bucket.rx < 0 && bucket.tx < 0 && bucket.samples == 0);

        history.append(QStringLiteral("br-lan"), 10000, 42.0, 7.0);
        const TrafficHistory::Trace partial =
            history.trace(QStringLiteral("br-lan"), 500000, 60000);
        // The sample is outside the 1 min window: all slots stay empty.
        CHECK(filledSlots(partial) == 0);
        CHECK(partial.coveredFromMs == 10000);
    }

    // --- bucket widths the range selector picks ----------------------------
    {
        const struct {
            qint64 windowMs;
            qint64 expected;
        } cases[] = {{60000, 1000}, {600000, 1000}, {3600000, 10000}, {86400000, 60000}};
        for (const auto &item : cases) {
            const qint64 width = TrafficHistory::bucketWidthFor(item.windowMs);
            const qint64 points = (item.windowMs + width - 1) / width + 1;
            out << "window " << item.windowMs / 1000 << " s -> bucket " << width / 1000
                << " s, " << points << " points" << Qt::endl;
            CHECK(width == item.expected);
            CHECK(points <= TrafficHistory::maxPoints + 1);
            if (item.windowMs > TrafficHistory::recentSpanMs)
                CHECK(width % TrafficHistory::coarseBucketMs == 0);
        }
    }

    // --- cost: a day of 1 Hz samples, then the four windows -----------------
    {
        TrafficHistory history;
        QElapsedTimer timer;
        timer.start();
        const int day = 24 * 3600;
        for (int i = 0; i < day; ++i)
            history.append(QStringLiteral("br-lan"), i * 1000, 1000.0 + (i % 97), 500.0);
        const qint64 appendMs = timer.elapsed();

        QElapsedTimer traceTimer;
        traceTimer.start();
        const int rounds = 200;
        qint64 totalNs = 0;
        for (int i = 0; i < rounds; ++i) {
            const TrafficHistory::Trace t =
                history.trace(QStringLiteral("br-lan"), day * 1000 - 1000, 86400000);
            totalNs += traceTimer.nsecsElapsed();
            traceTimer.restart();
            if (t.points.size() < 2) {
                CHECK(false);
                break;
            }
        }
        const double traceUs = totalNs / 1000.0 / rounds;
        out << "24 h retention: appends=" << day << " in " << appendMs << " ms; 24 h trace "
            << traceUs << " us; recent=" << history.recentCount(QStringLiteral("br-lan"))
            << " coarse=" << history.coarseCount(QStringLiteral("br-lan"))
            << " bytes~" << history.recentCount(QStringLiteral("br-lan")) * 24
                                  + history.coarseCount(QStringLiteral("br-lan")) * 32
            << Qt::endl;
        CHECK(traceUs < 5000.0);
        CHECK(history.recentCount(QStringLiteral("br-lan")) <= 601);
        CHECK(history.coarseCount(QStringLiteral("br-lan")) <= 8642);
    }

    if (failures > 0) {
        out << "m16-traffic-history: " << failures << " check(s) failed" << Qt::endl;
        return 1;
    }
    out << "m16-traffic-history: retention, bucketing and merge math hold" << Qt::endl;
    return 0;
}
