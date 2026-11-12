// SPDX-FileCopyrightText: 2026 the router-console authors
// SPDX-License-Identifier: AGPL-3.0-or-later

// Aim-view math without a router: bearing/distance on a known pair, the site
// aggregation from the ACMA extract's per-band rows, the peak-hold arithmetic
// against synthetic series, and the manual-mark round trip through the model
// API the plugin views call.

#include <QCoreApplication>
#include <QTextStream>
#include <QVariantList>
#include <QVariantMap>

#include "models/AimModel.h"

namespace {

int failures = 0;

void check(bool condition, const QString &what)
{
    QTextStream out(stdout);
    if (condition) {
        out << "  ok   " << what << Qt::endl;
        return;
    }
    ++failures;
    out << "  FAIL " << what << Qt::endl;
}

void checkNear(double actual, double expected, double tolerance, const QString &what)
{
    check(qAbs(actual - expected) <= tolerance,
          QStringLiteral("%1 (%2, expected %3 ± %4)")
              .arg(what)
              .arg(actual, 0, 'f', 4)
              .arg(expected, 0, 'f', 4)
              .arg(tolerance, 0, 'f', 4));
}

QVariantMap towerRow(const QString &site, double lat, double lon, const QString &band,
                     double mhz, const QVariant &height = 30)
{
    QVariantMap row;
    row.insert(QStringLiteral("site"), site);
    row.insert(QStringLiteral("lat"), lat);
    row.insert(QStringLiteral("lon"), lon);
    row.insert(QStringLiteral("band"), band);
    row.insert(QStringLiteral("mhz"), mhz);
    row.insert(QStringLiteral("h"), height);
    row.insert(QStringLiteral("eirp"), 12000.0);
    row.insert(QStringLiteral("hbw"), 60.0);
    row.insert(QStringLiteral("azs"), QVariantList{0.0, 120.0});
    row.insert(QStringLiteral("carrier"), QStringLiteral("Test carrier"));
    return row;
}

QVariantMap towersOf(const QVariantList &rows)
{
    QVariantMap towers;
    towers.insert(QStringLiteral("data_date"), QStringLiteral("2026-11-07T09:06:09Z"));
    towers.insert(QStringLiteral("towers"), rows);
    return towers;
}

void testGeometry()
{
    QTextStream(stdout) << "geometry" << Qt::endl;

    checkNear(Aim::distanceKm(-33.0, 151.6, -33.1, 151.6), 11.1195, 0.01,
              QStringLiteral("0.1 deg of latitude"));
    checkNear(Aim::initialBearingDeg(-33.0, 151.6, -33.1, 151.6), 180.0, 0.1,
              QStringLiteral("due south bearing"));
    checkNear(Aim::distanceKm(-33.0, 151.6, -33.0, 151.7), 9.3256, 0.02,
              QStringLiteral("0.1 deg of longitude at 33 S"));
    checkNear(Aim::initialBearingDeg(-33.0, 151.6, -33.0, 151.7), 90.0, 0.3,
              QStringLiteral("due east bearing"));

    // A pair whose great-circle distance and initial bearing are published:
    // Sydney to Melbourne, 713 km on a 230 deg initial heading.
    checkNear(Aim::distanceKm(-33.8688, 151.2093, -37.8136, 144.9631), 713.4, 0.5,
              QStringLiteral("Sydney to Melbourne distance"));
    checkNear(Aim::initialBearingDeg(-33.8688, 151.2093, -37.8136, 144.9631), 230.3, 0.3,
              QStringLiteral("Sydney to Melbourne bearing"));
}

void testSiteAggregation()
{
    QTextStream(stdout) << "site aggregation" << Qt::endl;

    QVariantList rows;
    rows << towerRow(QStringLiteral("1001"), -33.1, 151.6, QStringLiteral("B28"), 763.0,
                     QStringLiteral("40"))
         << towerRow(QStringLiteral("1001"), -33.1, 151.6, QStringLiteral("B8"), 947.5)
         << towerRow(QStringLiteral("1002"), -33.0, 151.7, QStringLiteral("B3"), 1800.0);
    // A row with no coordinates must not become a site.
    QVariantMap broken = towerRow(QStringLiteral("1003"), 0, 0, QStringLiteral("B1"), 2100.0);
    broken.remove(QStringLiteral("lat"));
    rows << broken;

    Aim::Model model;
    model.setTowers(towersOf(rows), -33.0, 151.6);

    check(model.siteCount() == 2, QStringLiteral("two sites from four licence rows (%1)")
                                      .arg(model.siteCount()));
    const Aim::Site &first = model.siteList().at(0);
    check(first.id == QLatin1String("1002"), QStringLiteral("nearest site first (%1)").arg(first.id));
    checkNear(first.distanceKm, 9.3256, 0.02, QStringLiteral("site 1002 distance"));
    checkNear(first.bearingDeg, 90.0, 0.3, QStringLiteral("site 1002 bearing"));

    const Aim::Site &merged = model.siteList().at(1);
    check(merged.id == QLatin1String("1001") && merged.bands.size() == 2,
          QStringLiteral("site 1001 holds both bands (%1)").arg(merged.bands.join(QLatin1Char('/'))));
    check(merged.mhz.size() == 2 && merged.mhz.at(0) == 763.0 && merged.mhz.at(1) == 947.5,
          QStringLiteral("site 1001 frequencies sorted (763, 947.5)"));
    checkNear(merged.heightM, 40.0, 0.001, QStringLiteral("string height parsed from the extract"));
    check(merged.azimuths.size() == 2 && merged.halfBeamwidth > 0,
          QStringLiteral("licensed azimuths kept"));
    check(model.dataDate() == QLatin1String("2026-11-07T09:06:09Z"),
          QStringLiteral("extract date exposed"));

    // Sites map to QML records with the fields the views bind to.
    const QVariantMap record = model.sites().at(1).toMap();
    check(record.value(QStringLiteral("site")).toString() == QLatin1String("1001")
              && record.value(QStringLiteral("km")).toDouble() > 11.0
              && record.value(QStringLiteral("bands")).toList().size() == 2,
          QStringLiteral("site record carries id, distance and bands"));
}

void testPeakHold()
{
    QTextStream(stdout) << "peak hold" << Qt::endl;

    Aim::PeakHold hold;
    check(hold.isEmpty(), QStringLiteral("starts empty"));

    const auto reading = [](qint64 at, double rsrp, double rsrq, double sinr, bool hasRsrp,
                            bool hasRsrq, bool hasSinr) {
        Aim::Reading r;
        r.atMs = at;
        r.rsrp = rsrp;
        r.hasRsrp = hasRsrp;
        r.rsrq = rsrq;
        r.hasRsrq = hasRsrq;
        r.sinr = sinr;
        r.hasSinr = hasSinr;
        return r;
    };

    hold.append(reading(1000, -101, -15, 3, true, true, true));
    hold.append(reading(2000, -95, -9, 12, true, true, true));
    hold.append(reading(3000, -108, -12, 4, true, true, true));
    // A poll with no metrics at all is not a reading: it must not count, and a
    // poll missing only RSRP must not move the RSRP peak.
    hold.append(reading(4000, 0, 0, 0, false, false, false));
    hold.append(reading(5000, 0, -14, 5, false, true, true));

    check(hold.rsrp().seen && hold.rsrp().value == -95 && hold.rsrp().atMs == 2000,
          QStringLiteral("RSRP peak -95 dBm at the second sample"));
    check(hold.sinr().seen && hold.sinr().value == 12 && hold.sinr().atMs == 2000,
          QStringLiteral("SINR peak 12 dB"));
    check(hold.rsrq().seen && hold.rsrq().value == -9 && hold.rsrq().atMs == 2000,
          QStringLiteral("RSRQ peak -9 dB (least negative)"));
    check(hold.readings() == 4, QStringLiteral("only polls with a metric count (%1 readings)")
                                    .arg(hold.readings()));
    check(hold.rsrp().value == -95 && hold.rsrq().value == -9,
          QStringLiteral("a poll without RSRP leaves the RSRP peak alone"));

    hold.reset();
    check(hold.isEmpty() && !hold.rsrp().seen, QStringLiteral("reset clears the peaks"));

    // The same arithmetic through the model, fed the strings rpcd returns.
    Aim::Model model;
    const auto signal = [](const QString &rsrp, const QString &rsrq, const QString &sinr) {
        QVariantMap map;
        map.insert(QStringLiteral("rsrp"), rsrp);
        map.insert(QStringLiteral("rsrq"), rsrq);
        map.insert(QStringLiteral("sinr"), sinr);
        return map;
    };
    model.applySignal(signal(QStringLiteral("-101"), QStringLiteral("-15"), QStringLiteral("3")), 1000);
    model.applySignal(signal(QStringLiteral("-97"), QStringLiteral("-11"), QStringLiteral("9")), 2000);
    model.applySignal(signal(QStringLiteral("-99"), QStringLiteral("-13"), QStringLiteral("2")), 3000);
    model.applySignal(signal(QString(), QString(), QString()), 4000);

    const QVariantMap peaks = model.peaks();
    check(peaks.value(QStringLiteral("readings")).toInt() == 3,
          QStringLiteral("model counted three readings"));
    check(peaks.value(QStringLiteral("rsrp")).toDouble() == -97.0
              && peaks.value(QStringLiteral("sinr")).toDouble() == 9.0,
          QStringLiteral("model peaks: RSRP -97, SINR 9 (%1 / %2)")
              .arg(peaks.value(QStringLiteral("rsrp")).toDouble())
              .arg(peaks.value(QStringLiteral("sinr")).toDouble()));
    check(!peaks.value(QStringLiteral("rsrpAt")).toString().isEmpty(),
          QStringLiteral("peak carries the time it was measured"));
    check(model.live().value(QStringLiteral("rsrp")).toDouble() == -99.0,
          QStringLiteral("live figure stays the latest sample, not the peak"));
}

void testMarks()
{
    QTextStream(stdout) << "manual marks" << Qt::endl;

    Aim::Model model;
    QVariantMap signal;
    signal.insert(QStringLiteral("rsrp"), QStringLiteral("-92"));
    signal.insert(QStringLiteral("rsrq"), QStringLiteral("-8"));
    signal.insert(QStringLiteral("sinr"), QStringLiteral("14"));
    model.applySignal(signal, 5000);

    const int row = model.addMark(QStringLiteral("045"), QStringLiteral("mast past the water tank"));
    check(row == 0 && model.markCount() == 1, QStringLiteral("mark added at row 0"));

    const QVariantMap mark = model.markAt(0);
    check(mark.value(QStringLiteral("bearing")).toString() == QLatin1String("045")
              && mark.value(QStringLiteral("note")).toString()
                     == QLatin1String("mast past the water tank"),
          QStringLiteral("bearing and note round-trip"));
    check(mark.value(QStringLiteral("rsrp")).toDouble() == -92.0
              && mark.value(QStringLiteral("sinr")).toDouble() == 14.0,
          QStringLiteral("mark snapshots the reading live at that moment"));
    check(mark.value(QStringLiteral("peakRsrp")).toDouble() == -92.0
              && !mark.value(QStringLiteral("at")).toString().isEmpty(),
          QStringLiteral("mark carries the peaks and a timestamp"));

    // The snapshot is frozen: a later, stronger sample does not rewrite it.
    QVariantMap stronger;
    stronger.insert(QStringLiteral("rsrp"), QStringLiteral("-80"));
    stronger.insert(QStringLiteral("sinr"), QStringLiteral("20"));
    model.applySignal(stronger, 6000);
    check(model.markAt(0).value(QStringLiteral("rsrp")).toDouble() == -92.0,
          QStringLiteral("existing mark keeps its own figures"));

    const int second = model.addMark(QStringLiteral("NE"), QStringLiteral(""));
    check(second == 1 && model.marks().size() == 2, QStringLiteral("second mark appended"));

    model.clearMarks();
    check(model.markCount() == 0 && model.marks().isEmpty(), QStringLiteral("clearMarks empties the list"));
    check(model.markAt(0).isEmpty(), QStringLiteral("markAt on an empty list answers {}"));
}

void testIngest()
{
    QTextStream(stdout) << "sample ingestion" << Qt::endl;

    QVariantList rows;
    rows << towerRow(QStringLiteral("2001"), -33.1, 151.6, QStringLiteral("B28"), 763.0);
    QVariantMap origin;
    origin.insert(QStringLiteral("lat"), -33.0);
    origin.insert(QStringLiteral("lon"), 151.6);

    QVariantMap samples;
    samples.insert(QStringLiteral("origin"),
                   QVariantMap{{QStringLiteral("ok"), true},
                               {QStringLiteral("at"), QStringLiteral("2026-11-13T01:00:00Z")},
                               {QStringLiteral("fields"), origin}});
    samples.insert(QStringLiteral("towers"),
                   QVariantMap{{QStringLiteral("ok"), true},
                               {QStringLiteral("at"), QStringLiteral("2026-11-13T01:00:00Z")},
                               {QStringLiteral("fields"), towersOf(rows)}});
    QVariantMap signalFields;
    signalFields.insert(QStringLiteral("rsrp"), QStringLiteral("-94"));
    signalFields.insert(QStringLiteral("sinr"), QStringLiteral("11"));
    const QVariantMap signalSample{
        {QStringLiteral("ok"), true},
        {QStringLiteral("at"), QStringLiteral("2026-11-13T01:00:01Z")},
        {QStringLiteral("fields"), signalFields}};
    samples.insert(QStringLiteral("signal"), signalSample);

    Aim::Model model;
    model.ingest(samples);
    check(model.siteCount() == 1 && model.hasOrigin(), QStringLiteral("one site, origin known"));
    check(model.peakHold().readings() == 1, QStringLiteral("one reading after the first ingest"));

    // The same sample delivered again is the same poll, not a second reading.
    model.ingest(samples);
    check(model.peakHold().readings() == 1,
          QStringLiteral("repeat of the same timestamp does not count twice (%1)")
              .arg(model.peakHold().readings()));

    // A failed source must not wipe the last good sites.
    QVariantMap failed = samples;
    failed.insert(QStringLiteral("towers"),
                  QVariantMap{{QStringLiteral("ok"), false},
                              {QStringLiteral("error"), QStringLiteral("ubus status 4")}});
    model.ingest(failed);
    check(model.siteCount() == 1, QStringLiteral("failed towers call keeps the last extract"));
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    testGeometry();
    testSiteAggregation();
    testPeakHold();
    testMarks();
    testIngest();

    QTextStream(stdout) << (failures == 0 ? "PASS" : "FAIL") << " · " << failures
                        << " failure(s)" << Qt::endl;
    return failures == 0 ? 0 : 1;
}
