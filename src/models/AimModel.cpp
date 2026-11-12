// SPDX-FileCopyrightText: 2026 the router-console authors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "AimModel.h"

#include <QDateTime>
#include <QHash>
#include <QSet>
#include <QtMath>

#include <algorithm>

namespace Aim {

namespace {

constexpr double kEarthRadiusKm = 6371.0;
// Spelled out rather than M_PI: MSVC's <cmath> only defines that with
// _USE_MATH_DEFINES, and the client builds on Windows too.
constexpr double kPi = 3.14159265358979323846;

// rpcd hands the metrics over as strings ("" when the modem has no reading,
// "unknown" from some paths), the tower extract as numbers. One reader for both.
bool readNumber(const QVariant &value, double *out)
{
    if (!value.isValid() || value.isNull())
        return false;
    bool ok = false;
    double parsed = 0;
    if (value.typeId() == QMetaType::QString) {
        const QString text = value.toString().trimmed();
        if (text.isEmpty())
            return false;
        parsed = text.toDouble(&ok);
    } else {
        parsed = value.toDouble(&ok);
    }
    if (!ok)
        return false;
    if (out)
        *out = parsed;
    return true;
}

QString isoFromMs(qint64 ms)
{
    return QDateTime::fromMSecsSinceEpoch(ms).toLocalTime().toString(Qt::ISODate);
}

QVariant numberOrEmpty(bool present, double value)
{
    return present ? QVariant(value) : QVariant();
}

} // namespace

double distanceKm(double lat1, double lon1, double lat2, double lon2)
{
    const double rad = kPi / 180.0;
    const double dLat = (lat2 - lat1) * rad;
    const double dLon = (lon2 - lon1) * rad;
    const double a = qPow(qSin(dLat / 2), 2)
                     + qCos(lat1 * rad) * qCos(lat2 * rad) * qPow(qSin(dLon / 2), 2);
    return kEarthRadiusKm * 2.0 * qAtan2(qSqrt(a), qSqrt(1.0 - a));
}

double initialBearingDeg(double lat1, double lon1, double lat2, double lon2)
{
    const double rad = kPi / 180.0;
    const double y = qSin((lon2 - lon1) * rad) * qCos(lat2 * rad);
    const double x = qCos(lat1 * rad) * qSin(lat2 * rad)
                     - qSin(lat1 * rad) * qCos(lat2 * rad) * qCos((lon2 - lon1) * rad);
    return fmod(qRadiansToDegrees(qAtan2(y, x)) + 360.0, 360.0);
}

void PeakHold::reset()
{
    *this = PeakHold{};
}

void PeakHold::append(const Reading &reading)
{
    if (!reading.any())
        return;
    ++m_readings;

    if (reading.hasRsrp && (!m_rsrp.seen || reading.rsrp > m_rsrp.value)) {
        m_rsrp.value = reading.rsrp;
        m_rsrp.atMs = reading.atMs;
        m_rsrp.seen = true;
    }
    if (reading.hasRsrq && (!m_rsrq.seen || reading.rsrq > m_rsrq.value)) {
        m_rsrq.value = reading.rsrq;
        m_rsrq.atMs = reading.atMs;
        m_rsrq.seen = true;
    }
    if (reading.hasSinr && (!m_sinr.seen || reading.sinr > m_sinr.value)) {
        m_sinr.value = reading.sinr;
        m_sinr.atMs = reading.atMs;
        m_sinr.seen = true;
    }
}

Model::Model(QObject *parent)
    : QObject(parent)
{
}

QVariantList Model::sites() const
{
    QVariantList out;
    out.reserve(m_sites.size());
    for (const Site &site : m_sites) {
        QVariantList bands;
        for (const QString &band : site.bands)
            bands.append(band);
        QVariantList mhz;
        for (double value : site.mhz)
            mhz.append(value);
        QVariantList azimuths;
        for (double value : site.azimuths)
            azimuths.append(value);

        QVariantMap map;
        map.insert(QStringLiteral("site"), site.id);
        map.insert(QStringLiteral("carrier"), site.carrier);
        map.insert(QStringLiteral("bands"), bands);
        map.insert(QStringLiteral("mhz"), mhz);
        map.insert(QStringLiteral("lat"), site.lat);
        map.insert(QStringLiteral("lon"), site.lon);
        map.insert(QStringLiteral("km"), site.distanceKm);
        map.insert(QStringLiteral("bearing"), site.bearingDeg);
        map.insert(QStringLiteral("h"), site.heightM);
        map.insert(QStringLiteral("eirp"), site.eirp);
        map.insert(QStringLiteral("azs"), azimuths);
        map.insert(QStringLiteral("hbw"), site.halfBeamwidth);
        map.insert(QStringLiteral("served"),
                   m_serving.value(QStringLiteral("matched")).toBool()
                       && m_serving.value(QStringLiteral("site")).toString() == site.id);
        out.append(map);
    }
    return out;
}

QVariantList Model::marks() const
{
    QVariantList out;
    out.reserve(m_marks.size());
    for (int i = 0; i < m_marks.size(); ++i)
        out.append(markAt(i));
    return out;
}

QVariantMap Model::live() const
{
    QVariantMap map;
    map.insert(QStringLiteral("rsrp"), numberOrEmpty(m_lastReading.hasRsrp, m_lastReading.rsrp));
    map.insert(QStringLiteral("rsrq"), numberOrEmpty(m_lastReading.hasRsrq, m_lastReading.rsrq));
    map.insert(QStringLiteral("sinr"), numberOrEmpty(m_lastReading.hasSinr, m_lastReading.sinr));
    map.insert(QStringLiteral("at"), m_lastReading.atMs > 0 ? isoFromMs(m_lastReading.atMs) : QString());
    return map;
}

QVariantMap Model::peaks() const
{
    const auto peakMap = [](const Peak &peak, const QString &key, QVariantMap *target) {
        target->insert(key, numberOrEmpty(peak.seen, peak.value));
        target->insert(key + QStringLiteral("At"),
                       peak.seen && peak.atMs > 0 ? isoFromMs(peak.atMs) : QString());
    };

    QVariantMap map;
    peakMap(m_peaks.rsrp(), QStringLiteral("rsrp"), &map);
    peakMap(m_peaks.rsrq(), QStringLiteral("rsrq"), &map);
    peakMap(m_peaks.sinr(), QStringLiteral("sinr"), &map);
    map.insert(QStringLiteral("readings"), m_peaks.readings());
    return map;
}

QVariantMap Model::serving() const
{
    return m_serving;
}

QVariantMap Model::origin() const
{
    QVariantMap map;
    map.insert(QStringLiteral("lat"), m_hasOrigin ? QVariant(m_originLat) : QVariant());
    map.insert(QStringLiteral("lon"), m_hasOrigin ? QVariant(m_originLon) : QVariant());
    map.insert(QStringLiteral("set"), m_hasOrigin);
    return map;
}

void Model::ingest(const QVariantMap &samples)
{
    const auto fields = [&samples](const QString &id) {
        const QVariantMap entry = samples.value(id).toMap();
        if (!entry.value(QStringLiteral("ok")).toBool())
            return QVariantMap{};
        return entry.value(QStringLiteral("fields")).toMap();
    };
    const auto stamp = [&samples](const QString &id) {
        const QDateTime at = QDateTime::fromString(
            samples.value(id).toMap().value(QStringLiteral("at")).toString(), Qt::ISODate);
        return at.isValid() ? at.toMSecsSinceEpoch() : QDateTime::currentMSecsSinceEpoch();
    };

    QStringList problems;
    for (auto it = samples.begin(); it != samples.end(); ++it) {
        const QVariantMap entry = it.value().toMap();
        if (!entry.value(QStringLiteral("ok")).toBool()) {
            const QString error = entry.value(QStringLiteral("error")).toString();
            problems.append(error.isEmpty() ? it.key() : QStringLiteral("%1: %2").arg(it.key(), error));
        }
    }
    if (problems != m_errorText.split(QStringLiteral("   "), Qt::SkipEmptyParts))
        m_errorText = problems.join(QStringLiteral("   "));

    const QVariantMap originFields = fields(QStringLiteral("origin"));
    if (!originFields.isEmpty()) {
        double lat = 0;
        double lon = 0;
        const bool hasLat = readNumber(originFields.value(QStringLiteral("lat")), &lat);
        const bool hasLon = readNumber(originFields.value(QStringLiteral("lon")), &lon);
        if (hasLat && hasLon)
            setOrigin(lat, lon);
    }

    const QVariantMap towerFields = fields(QStringLiteral("towers"));
    if (!towerFields.isEmpty())
        setTowers(towerFields, m_originLat, m_originLon);

    // The signal lane is the 1 Hz one, so its timestamp is the dedupe key: an
    // identical sample must not count twice toward the peak or the log.
    const QVariantMap signalFields = fields(QStringLiteral("signal"));
    if (!signalFields.isEmpty()) {
        const QVariantMap entry = samples.value(QStringLiteral("signal")).toMap();
        const QString key = entry.contains(QStringLiteral("revision"))
                                ? entry.value(QStringLiteral("revision")).toString()
                                : entry.value(QStringLiteral("at")).toString();
        if (key != m_lastSignalKey && !key.isEmpty()) {
            m_lastSignalKey = key;
            applySignal(signalFields, stamp(QStringLiteral("signal")));
        }
    }

    const QVariantMap statusFields = fields(QStringLiteral("status"));
    if (!statusFields.isEmpty() && statusFields != m_lastStatus) {
        m_lastStatus = statusFields;
        applyStatus(statusFields);
        // Without a `signal` source (the map window declares only status), the
        // status figures are still a real reading.
        if (!samples.contains(QStringLiteral("signal"))) {
            Reading reading;
            reading.atMs = stamp(QStringLiteral("status"));
            reading.hasRsrp = readNumber(statusFields.value(QStringLiteral("rsrp")), &reading.rsrp);
            reading.hasRsrq = readNumber(statusFields.value(QStringLiteral("rsrq")), &reading.rsrq);
            reading.hasSinr = readNumber(statusFields.value(QStringLiteral("sinr")), &reading.sinr);
            if (reading.any()) {
                m_lastReading = reading;
                m_peaks.append(reading);
            }
        }
    }

    emit updated();
}

void Model::setOrigin(double lat, double lon)
{
    const bool set = qAbs(lat) > 0.0001 || qAbs(lon) > 0.0001;
    if (set == m_hasOrigin && (!set || (qFuzzyCompare(lat + 1, m_originLat + 1)
                                        && qFuzzyCompare(lon + 1, m_originLon + 1))))
        return;

    m_hasOrigin = set;
    m_originLat = lat;
    m_originLon = lon;
    recomputeRanges();
}

void Model::setTowers(const QVariantMap &towers, double lat, double lon)
{
    if (towers.isEmpty())
        return;
    if (towers == m_lastTowers && lat == m_originLat && lon == m_originLon)
        return;
    m_lastTowers = towers;
    m_originLat = lat;
    m_originLon = lon;
    // An origin of (0, 0) is the app's "not set" value, not the Gulf of Guinea.
    m_hasOrigin = qAbs(lat) > 0.0001 || qAbs(lon) > 0.0001;
    m_dataDate = towers.value(QStringLiteral("data_date")).toString();
    rebuildSites();
}

void Model::rebuildSites()
{
    // The extract carries one row per site per licensed band; the aim view
    // needs one point per site, with the bands it holds.
    QHash<QString, Site> byId;
    QStringList order;
    const QVariantList rows = m_lastTowers.value(QStringLiteral("towers")).toList();
    for (const QVariant &row : rows) {
        const QVariantMap fields = row.toMap();
        const QString id = fields.value(QStringLiteral("site")).toString();
        double siteLat = 0;
        double siteLon = 0;
        if (id.isEmpty() || !readNumber(fields.value(QStringLiteral("lat")), &siteLat)
            || !readNumber(fields.value(QStringLiteral("lon")), &siteLon)) {
            continue;
        }

        auto it = byId.find(id);
        if (it == byId.end()) {
            Site site;
            site.id = id;
            site.lat = siteLat;
            site.lon = siteLon;
            site.carrier = fields.value(QStringLiteral("carrier")).toString();
            readNumber(fields.value(QStringLiteral("h")), &site.heightM);
            readNumber(fields.value(QStringLiteral("hbw")), &site.halfBeamwidth);
            it = byId.insert(id, site);
            order.append(id);
        }

        Site &site = *it;
        const QString band = fields.value(QStringLiteral("band")).toString();
        if (!band.isEmpty() && !site.bands.contains(band))
            site.bands.append(band);
        double mhz = 0;
        if (readNumber(fields.value(QStringLiteral("mhz")), &mhz) && !site.mhz.contains(mhz))
            site.mhz.append(mhz);
        double eirp = 0;
        if (readNumber(fields.value(QStringLiteral("eirp")), &eirp) && eirp > site.eirp)
            site.eirp = eirp;
        const QVariantList azimuths = fields.value(QStringLiteral("azs")).toList();
        for (const QVariant &value : azimuths) {
            double azimuth = 0;
            if (readNumber(value, &azimuth) && !site.azimuths.contains(azimuth))
                site.azimuths.append(azimuth);
        }
        if (site.carrier.isEmpty())
            site.carrier = fields.value(QStringLiteral("carrier")).toString();
    }

    m_sites.clear();
    m_sites.reserve(order.size());
    for (const QString &id : order) {
        Site site = byId.value(id);
        std::sort(site.bands.begin(), site.bands.end());
        std::sort(site.mhz.begin(), site.mhz.end());
        std::sort(site.azimuths.begin(), site.azimuths.end());
        m_sites.append(site);
    }
    recomputeRanges();

    // A serving site that the old extract did not hold can become known now.
    if (!m_lastStatus.isEmpty())
        readStatusServing(m_lastStatus);
}

void Model::recomputeRanges()
{
    for (Site &site : m_sites) {
        if (m_hasOrigin) {
            site.distanceKm = distanceKm(m_originLat, m_originLon, site.lat, site.lon);
            site.bearingDeg = initialBearingDeg(m_originLat, m_originLon, site.lat, site.lon);
        } else {
            site.distanceKm = 0;
            site.bearingDeg = 0;
        }
    }
    std::sort(m_sites.begin(), m_sites.end(), [](const Site &a, const Site &b) {
        if (a.distanceKm != b.distanceKm)
            return a.distanceKm < b.distanceKm;
        return a.id < b.id;
    });
}

void Model::applySignal(const QVariantMap &signal, qint64 atMs)
{
    if (signal.value(QStringLiteral("calibrating")).toBool()) {
        m_errorText = QStringLiteral("modemaim is paused while rist2rist calibrates");
        return;
    }

    Reading reading;
    reading.atMs = atMs;
    reading.hasRsrp = readNumber(signal.value(QStringLiteral("rsrp")), &reading.rsrp);
    reading.hasRsrq = readNumber(signal.value(QStringLiteral("rsrq")), &reading.rsrq);
    reading.hasSinr = readNumber(signal.value(QStringLiteral("sinr")), &reading.sinr);
    if (!reading.any())
        return;

    m_lastReading = reading;
    m_peaks.append(reading);
    if (m_errorText.contains(QStringLiteral("calibrates")))
        m_errorText.clear();
}

void Model::applyStatus(const QVariantMap &status)
{
    readStatusServing(status);
}

void Model::readStatusServing(const QVariantMap &status)
{
    const QVariantMap serving = status.value(QStringLiteral("serving")).toMap();
    const QVariantMap mapped = status.value(QStringLiteral("mapped")).toMap();
    const QString mappedSite = mapped.value(QStringLiteral("site")).toString();

    QVariantMap out;
    out.insert(QStringLiteral("present"), !serving.isEmpty());
    out.insert(QStringLiteral("pci"), serving.value(QStringLiteral("pci")));
    out.insert(QStringLiteral("eci"), serving.value(QStringLiteral("eci")));
    out.insert(QStringLiteral("earfcn"), serving.value(QStringLiteral("earfcn")));
    out.insert(QStringLiteral("ta"), serving.value(QStringLiteral("ta")));
    out.insert(QStringLiteral("operator"), status.value(QStringLiteral("operator")));
    out.insert(QStringLiteral("mappedSite"), mappedSite);
    out.insert(QStringLiteral("mappedConf"), mapped.value(QStringLiteral("conf")));

    bool matched = false;
    if (!mappedSite.isEmpty()) {
        for (const Site &site : m_sites) {
            if (site.id != mappedSite)
                continue;
            matched = true;
            out.insert(QStringLiteral("bearing"), site.bearingDeg);
            out.insert(QStringLiteral("km"), site.distanceKm);
            out.insert(QStringLiteral("site"), site.id);
            out.insert(QStringLiteral("carrier"), site.carrier);
            out.insert(QStringLiteral("bands"), QVariant(site.bands));
            break;
        }
        if (!matched)
            out.insert(QStringLiteral("site"), mappedSite);
    }
    out.insert(QStringLiteral("matched"), matched);
    m_serving = out;
}

int Model::addMark(const QString &bearing, const QString &note)
{
    Mark mark;
    mark.atMs = QDateTime::currentMSecsSinceEpoch();
    mark.bearing = bearing.trimmed();
    mark.note = note.trimmed();
    mark.reading = m_lastReading;
    mark.rsrpPeak = m_peaks.rsrp();
    mark.rsrqPeak = m_peaks.rsrq();
    mark.sinrPeak = m_peaks.sinr();
    m_marks.append(mark);
    emit updated();
    return m_marks.size() - 1;
}

void Model::clearMarks()
{
    if (m_marks.isEmpty())
        return;
    m_marks.clear();
    emit updated();
}

void Model::clearPeaks()
{
    m_peaks.reset();
    emit updated();
}

QVariantMap Model::markAt(int row) const
{
    if (row < 0 || row >= m_marks.size())
        return {};
    const Mark &mark = m_marks.at(row);

    QVariantMap map;
    map.insert(QStringLiteral("row"), row);
    map.insert(QStringLiteral("at"), isoFromMs(mark.atMs));
    map.insert(QStringLiteral("bearing"), mark.bearing);
    map.insert(QStringLiteral("note"), mark.note);
    map.insert(QStringLiteral("rsrp"), numberOrEmpty(mark.reading.hasRsrp, mark.reading.rsrp));
    map.insert(QStringLiteral("rsrq"), numberOrEmpty(mark.reading.hasRsrq, mark.reading.rsrq));
    map.insert(QStringLiteral("sinr"), numberOrEmpty(mark.reading.hasSinr, mark.reading.sinr));
    map.insert(QStringLiteral("peakRsrp"), numberOrEmpty(mark.rsrpPeak.seen, mark.rsrpPeak.value));
    map.insert(QStringLiteral("peakRsrq"), numberOrEmpty(mark.rsrqPeak.seen, mark.rsrqPeak.value));
    map.insert(QStringLiteral("peakSinr"), numberOrEmpty(mark.sinrPeak.seen, mark.sinrPeak.value));
    return map;
}

} // namespace Aim
