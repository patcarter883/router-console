// SPDX-FileCopyrightText: 2026 the router-console authors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

// Aiming support for the modemaim plugin views: ACMA sites around the router,
// a running peak per signal metric, and the candidate marks an operator drops
// while turning the antenna by hand. No I/O and no timers here — the plugin
// session polls, the views hand the samples in, and the math is testable
// without a router (tests/m17_aim_model.cpp).
namespace Aim {

// Great-circle distance in km and initial bearing in degrees true (0 = north),
// both from the router's origin to a site.
double distanceKm(double lat1, double lon1, double lat2, double lon2);
double initialBearingDeg(double lat1, double lon1, double lat2, double lon2);

// One ACMA site, aggregated from the extract's per-band licence rows.
struct Site {
    QString id;
    QString carrier;
    QStringList bands;
    QList<double> mhz;
    double lat = 0;
    double lon = 0;
    double heightM = 0;
    double eirp = 0;
    double halfBeamwidth = 0;
    QList<double> azimuths; // licensed panel directions, degrees true
    double distanceKm = 0;
    double bearingDeg = 0;
};

// One poll of the modem's signal (modemaim `signal`, or `status` for the slower
// call). Empty fields are absent readings, never zeroes.
struct Reading {
    qint64 atMs = 0;
    double rsrp = 0;
    double rsrq = 0;
    double sinr = 0;
    bool hasRsrp = false;
    bool hasRsrq = false;
    bool hasSinr = false;

    bool any() const { return hasRsrp || hasRsrq || hasSinr; }
};

struct Peak {
    double value = 0;
    qint64 atMs = -1;
    bool seen = false;
};

// Running maximum per metric. Higher is better for all three — RSRQ included,
// it is reported as a negative dB figure — so the peak is a max.
class PeakHold
{
public:
    void reset();
    void append(const Reading &reading);

    const Peak &rsrp() const { return m_rsrp; }
    const Peak &rsrq() const { return m_rsrq; }
    const Peak &sinr() const { return m_sinr; }
    int readings() const { return m_readings; }
    bool isEmpty() const { return m_readings == 0; }

private:
    Peak m_rsrp;
    Peak m_rsrq;
    Peak m_sinr;
    int m_readings = 0;
};

// A mark the operator drops while hand-rotating: the bearing they guess they
// are on, a free-text note, and the metrics as they stood at that moment.
struct Mark {
    qint64 atMs = 0;
    QString bearing; // operator-entered, free text ("045", "NE", "just past the gum tree")
    QString note;
    Reading reading;
    Peak rsrpPeak;
    Peak rsrqPeak;
    Peak sinrPeak;
};

// View model shared by the map and aim plugin windows.
class Model : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QVariantList sites READ sites NOTIFY updated)
    Q_PROPERTY(QVariantList marks READ marks NOTIFY updated)
    Q_PROPERTY(QVariantMap live READ live NOTIFY updated)
    Q_PROPERTY(QVariantMap peaks READ peaks NOTIFY updated)
    Q_PROPERTY(QVariantMap serving READ serving NOTIFY updated)
    Q_PROPERTY(QVariantMap origin READ origin NOTIFY updated)
    Q_PROPERTY(bool hasOrigin READ hasOrigin NOTIFY updated)
    Q_PROPERTY(bool hasSites READ hasSites NOTIFY updated)
    Q_PROPERTY(QString dataDate READ dataDate NOTIFY updated)
    Q_PROPERTY(int siteCount READ siteCount NOTIFY updated)
    Q_PROPERTY(int markCount READ markCount NOTIFY updated)
    Q_PROPERTY(int readingCount READ readingCount NOTIFY updated)
    Q_PROPERTY(QString errorText READ errorText NOTIFY updated)

public:
    explicit Model(QObject *parent = nullptr);

    QVariantList sites() const;
    QVariantList marks() const;
    QVariantMap live() const;
    QVariantMap peaks() const;
    QVariantMap serving() const;
    QVariantMap origin() const;
    bool hasOrigin() const { return m_hasOrigin; }
    bool hasSites() const { return !m_sites.isEmpty(); }
    QString dataDate() const { return m_dataDate; }
    int siteCount() const { return m_sites.size(); }
    int markCount() const { return m_marks.size(); }
    int readingCount() const { return m_peaks.readings(); }
    QString errorText() const { return m_errorText; }

    // `samples` is Plugin::Session::samples: source id -> {ok, at, error, fields}.
    Q_INVOKABLE void ingest(const QVariantMap &samples);

    // Primitives behind ingest, also used by the tests.
    void setTowers(const QVariantMap &towers, double lat, double lon);
    void setOrigin(double lat, double lon);
    void applySignal(const QVariantMap &signal, qint64 atMs);
    void applyStatus(const QVariantMap &status);

    // Drops a candidate mark: the operator's bearing guess and note plus the
    // current reading and peaks. Returns the new row, -1 when there is nothing
    // measured to mark yet.
    Q_INVOKABLE int addMark(const QString &bearing, const QString &note);
    Q_INVOKABLE void clearMarks();
    Q_INVOKABLE void clearPeaks();
    Q_INVOKABLE QVariantMap markAt(int row) const;

    // Typed stores the tests assert on.
    const QList<Site> &siteList() const { return m_sites; }
    const QList<Mark> &markList() const { return m_marks; }
    const PeakHold &peakHold() const { return m_peaks; }
    const Reading &lastReading() const { return m_lastReading; }

signals:
    void updated();

private:
    void rebuildSites();
    void recomputeRanges();
    void readStatusServing(const QVariantMap &status);

    QList<Site> m_sites;
    QList<Mark> m_marks;
    PeakHold m_peaks;
    Reading m_lastReading;
    QVariantMap m_serving;
    QVariantMap m_lastTowers;
    QVariantMap m_lastStatus;
    QString m_lastSignalKey;
    QString m_dataDate;
    QString m_errorText;
    double m_originLat = 0;
    double m_originLon = 0;
    bool m_hasOrigin = false;
};

} // namespace Aim
