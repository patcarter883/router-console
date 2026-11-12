// SPDX-FileCopyrightText: 2026 the router-console authors
// SPDX-License-Identifier: AGPL-3.0-or-later

// Star check for the aim view against the lab VM: the modemaim plugin session
// polls the fast `signal` lane twice a second, the model holds a running peak per
// metric, and the tower extract lands as aim-able sites around the router's
// origin. After the sample window the test asserts the peak really is the best
// figure seen, drops a manual mark through the same API the view's Mark button
// calls, and reads it back.
//
// The VM has no MBIM modem; scripts/vm-aim-fixtures.sh installs a stand-in
// mbimcli (and the eNB -> ACMA row) so the rpcd surface answers with real-shaped
// JSON. Without it there are no readings and this test skips, like every other
// VM test without its router.

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDateTime>
#include <QElapsedTimer>
#include <QTextStream>
#include <QTimer>

#include "AppEngine.h"
#include "models/AimModel.h"
#include "plugins/PluginHub.h"
#include "plugins/PluginSession.h"
#include "vm_probe.h"

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

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    app.setOrganizationName("open-broadcast");
    app.setApplicationName("router-console");

    QCommandLineParser parser;
    parser.addHelpOption();
    const QCommandLineOption hostOption("host", "host[:port]", "host", "127.0.0.1:10443");
    const QCommandLineOption userOption("user", "username", "user", "root");
    const QCommandLineOption passwordOption("password", "password", "password", "root");
    const QCommandLineOption trustOption("accept-new-cert", "accept an unseen certificate");
    const QCommandLineOption secondsOption("seconds", "poll for this long", "s", "32");
    const QCommandLineOption sitesOption("min-sites", "minimum sites in the extract", "n", "200");
    parser.addOptions({hostOption, userOption, passwordOption, trustOption, secondsOption,
                       sitesOption});
    parser.process(app);

    QTextStream out(stdout);
    if (const int code = skipWithoutDevVm(out, parser.value(hostOption)))
        return code;

    AppEngine engine;
    Aim::Model aim;
    Plugin::Session *session = nullptr;
    bool trusted = false;
    bool finished = false;
    int readings = 0;
    int updates = 0;
    double bestRsrp = -1000;
    double bestSinr = -1000;
    QElapsedTimer clock;

    const auto finish = [&](int code) {
        if (finished)
            return;
        finished = true;
        if (code == 0)
            out << Qt::endl << "PASS" << Qt::endl;
        else
            out << Qt::endl << "FAIL (" << failures << " failing checks)" << Qt::endl;
        out.flush();
        app.exit(code);
    };

    QObject::connect(&engine, &AppEngine::stateChanged, &engine, [&] {
        if (engine.trustPending() && parser.isSet(trustOption) && !trusted) {
            trusted = true;
            engine.trustCertificate();
            return;
        }
        if (!engine.isConnected() || finished || session)
            return;

        session = qobject_cast<Plugin::Session *>(engine.openPlugin(QStringLiteral("modemaim-aim")));
        if (!session) {
            out << "FAILED: no session for modemaim-aim (manifest rejected?)" << Qt::endl;
            finish(1);
            return;
        }
        out << "plugin " << session->pluginId() << " (" << session->title() << ")" << Qt::endl
            << "  view: " << session->viewUrl() << Qt::endl
            << "  sources: " << session->sourceIds().join(", ") << Qt::endl;

        QObject::connect(session, &Plugin::Session::updated, &aim, [&] {
            if (finished)
                return;
            aim.ingest(session->allSamples());
            ++updates;
            const Aim::Reading &reading = aim.lastReading();
            if (reading.hasRsrp && reading.rsrp > bestRsrp)
                bestRsrp = reading.rsrp;
            if (reading.hasSinr && reading.sinr > bestSinr)
                bestSinr = reading.sinr;
            readings = aim.peakHold().readings();
        });

        clock.start();
        QTimer::singleShot(parser.value(secondsOption).toInt() * 1000, &app, [&] {
            if (finished)
                return;

            out << "after " << clock.elapsed() / 1000 << " s: " << readings << " readings in "
                << updates << " session updates" << Qt::endl;

            if (readings == 0) {
                out << "SKIPPED: the router reports no signal (no modem, and no aim fixture)"
                    << Qt::endl;
                out.flush();
                finished = true;
                app.exit(3);
                return;
            }

            const int minSites = parser.value(sitesOption).toInt();
            check(aim.siteCount() >= minSites,
                  QStringLiteral("tower extract parsed: %1 sites (>= %2)")
                      .arg(aim.siteCount())
                      .arg(minSites));
            check(aim.hasOrigin(), QStringLiteral("origin known: %1, %2")
                                       .arg(aim.origin().value(QStringLiteral("lat")).toDouble(), 0,
                                            'f', 5)
                                       .arg(aim.origin().value(QStringLiteral("lon")).toDouble(), 0,
                                            'f', 5));

            int validCoordinates = 0;
            double nearest = -1;
            for (const Aim::Site &site : aim.siteList()) {
                if (qAbs(site.lat) > 0.01 && qAbs(site.lon) > 0.01
                    && site.bearingDeg >= 0 && site.bearingDeg < 360) {
                    ++validCoordinates;
                }
                if (nearest < 0 || site.distanceKm < nearest)
                    nearest = site.distanceKm;
            }
            check(validCoordinates == aim.siteCount(),
                  QStringLiteral("every site has a valid position and bearing (%1)")
                      .arg(validCoordinates));
            check(nearest > 0.1 && nearest < 60,
                  QStringLiteral("nearest site %1 km from the router").arg(nearest, 0, 'f', 2));

            check(readings >= 20, QStringLiteral("%1 signal readings over the window (500 ms poll)")
                             .arg(readings));
            check(aim.peakHold().rsrp().seen && aim.peakHold().sinr().seen,
                  QStringLiteral("peak-hold saw RSRP and SINR"));
            check(aim.peakHold().rsrp().value >= bestRsrp - 0.001,
                  QStringLiteral("RSRP peak %1 dBm is the best of the run")
                      .arg(aim.peakHold().rsrp().value, 0, 'f', 1));
            check(aim.peakHold().sinr().value >= bestSinr - 0.001,
                  QStringLiteral("SINR peak %1 dB is the best of the run")
                      .arg(aim.peakHold().sinr().value, 0, 'f', 1));
            check(aim.peakHold().rsrp().atMs > 0,
                  QStringLiteral("RSRP peak timestamp: %1")
                      .arg(QDateTime::fromMSecsSinceEpoch(aim.peakHold().rsrp().atMs)
                               .toLocalTime()
                               .toString(Qt::ISODate)));

            const QVariantMap live = aim.live();
            check(live.value(QStringLiteral("rsrp")).isValid(),
                  QStringLiteral("live RSRP %1 dBm")
                      .arg(live.value(QStringLiteral("rsrp")).toDouble(), 0, 'f', 1));

            // Serving cell: the rpcd surface carries it, and the eNB map turns
            // it into an ACMA site. Assert the correlation only when the fixture
            // installed the eNB row; an unmapped cell is a real state too.
            const QVariantMap serving = aim.serving();
            if (serving.value(QStringLiteral("present")).toBool()) {
                out << "  info serving PCI " << serving.value(QStringLiteral("pci")).toString()
                    << " ECI " << serving.value(QStringLiteral("eci")).toString() << Qt::endl;
                if (serving.value(QStringLiteral("matched")).toBool()) {
                    check(serving.value(QStringLiteral("km")).toDouble() > 0,
                          QStringLiteral("serving cell mapped to site %1 at %2 deg / %3 km")
                              .arg(serving.value(QStringLiteral("site")).toString())
                              .arg(serving.value(QStringLiteral("bearing")).toDouble(), 0, 'f', 1)
                              .arg(serving.value(QStringLiteral("km")).toDouble(), 0, 'f', 2));
                } else {
                    out << "  info serving cell is not mapped to an ACMA site" << Qt::endl;
                }
            } else {
                out << "  info no serving cell reported" << Qt::endl;
            }

            const int markRow = aim.addMark(QStringLiteral("045"),
                                            QStringLiteral("live check: candidate bearing"));
            check(markRow == 0 && aim.markCount() == 1,
                  QStringLiteral("mark accepted at row %1").arg(markRow));
            const QVariantMap mark = aim.markAt(0);
            check(mark.value(QStringLiteral("bearing")).toString() == QLatin1String("045")
                      && !mark.value(QStringLiteral("at")).toString().isEmpty(),
                  QStringLiteral("mark reads back with its bearing and time (%1)")
                      .arg(mark.value(QStringLiteral("at")).toString()));
            check(mark.value(QStringLiteral("rsrp")).toDouble()
                      == aim.live().value(QStringLiteral("rsrp")).toDouble(),
                  QStringLiteral("mark holds the live RSRP %1 dBm")
                      .arg(mark.value(QStringLiteral("rsrp")).toDouble(), 0, 'f', 1));
            check(mark.value(QStringLiteral("peakRsrp")).toDouble()
                      == aim.peakHold().rsrp().value,
                  QStringLiteral("mark holds the peak RSRP at that moment"));

            finish(failures == 0 ? 0 : 1);
        });
    });

    QTimer::singleShot(20000 + parser.value(secondsOption).toInt() * 1000, &app, [&] {
        out << "FAILED: timed out connecting or polling" << Qt::endl;
        finish(1);
    });

    engine.connectTo(parser.value(hostOption), QString(), parser.value(userOption),
                     parser.value(passwordOption));
    return app.exec();
}
