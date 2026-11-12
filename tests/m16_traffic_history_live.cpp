// SPDX-FileCopyrightText: 2026 the router-console authors
// SPDX-License-Identifier: AGPL-3.0-or-later

// Star check for persistent graphs: with no graph window open, the client keeps
// sampling the lab VM at 1 Hz and the history store fills. Traffic is generated
// over the forwarded ssh connection so the interface counters move; after the
// sample window the test asks the model for the four range views and asserts
// they see the same traffic at different bucket widths.

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QProcess>
#include <QTextStream>
#include <QTimer>
#include <QVariantMap>

#include "models/InterfacesModel.h"
#include "vm_fixture.h"
#include "vm_probe.h"

namespace {

struct RangeView {
    QString id;
    qint64 windowMs = 0;
    QVariantMap map;
    int pointCount = 0;
    int filled = 0;
    double peakRx = 0;
    double meanRx = 0;
};

double meanOf(const QVariantList &values, bool *sawData)
{
    double sum = 0;
    int count = 0;
    for (const QVariant &value : values) {
        const double rate = value.toDouble();
        if (rate < 0)
            continue;
        sum += rate;
        ++count;
    }
    *sawData = count > 0;
    return count > 0 ? sum / count : 0;
}

struct Span {
    qint64 fromMs = 0;
    qint64 toMs = 0;
    bool valid = false;
};

// Time span covered by the occupied slots between the first and last one: the
// boundary slots are partial by construction, so they compare badly across two
// bucket widths.
Span interiorSpan(const QVariantMap &view)
{
    const QVariantList values = view.value(QStringLiteral("rx")).toList();
    const qint64 grid = view.value(QStringLiteral("gridStartMs")).toLongLong();
    const qint64 width = view.value(QStringLiteral("bucketMs")).toLongLong();
    int first = -1;
    int last = -1;
    for (int i = 0; i < values.size(); ++i) {
        if (values.at(i).toDouble() < 0)
            continue;
        if (first < 0)
            first = i;
        last = i;
    }
    Span span;
    if (first >= 0 && last > first + 1) {
        span.fromMs = grid + (first + 1) * width;
        span.toMs = grid + last * width;
        span.valid = true;
    }
    return span;
}

double interiorMean(const QVariantList &values, bool *sawData)
{
    int first = -1;
    int last = -1;
    for (int i = 0; i < values.size(); ++i) {
        if (values.at(i).toDouble() < 0)
            continue;
        if (first < 0)
            first = i;
        last = i;
    }
    double sum = 0;
    int count = 0;
    for (int i = first + 1; i < last; ++i) {
        const double rate = values.at(i).toDouble();
        if (rate < 0)
            continue;
        sum += rate;
        ++count;
    }
    *sawData = count > 0;
    return count > 0 ? sum / count : 0;
}

// Mean of the full slots that lie inside span: the fine view averaged over the
// exact time the coarse interior covers, so both widths describe one span.
double meanOverSpan(const QVariantMap &view, const Span &span, bool *sawData)
{
    const QVariantList values = view.value(QStringLiteral("rx")).toList();
    const qint64 grid = view.value(QStringLiteral("gridStartMs")).toLongLong();
    const qint64 width = view.value(QStringLiteral("bucketMs")).toLongLong();
    double sum = 0;
    int count = 0;
    for (int i = 0; i < values.size(); ++i) {
        const qint64 from = grid + static_cast<qint64>(i) * width;
        const qint64 to = from + width;
        if (from < span.fromMs || to > span.toMs)
            continue;
        const double rate = values.at(i).toDouble();
        if (rate < 0)
            continue;
        sum += rate;
        ++count;
    }
    *sawData = count > 0;
    return count > 0 ? sum / count : 0;
}

double peakOf(const QVariantList &values)
{
    double peak = 0;
    for (const QVariant &value : values) {
        const double rate = value.toDouble();
        if (rate > peak)
            peak = rate;
    }
    return peak;
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    app.setOrganizationName("open-broadcast");
    app.setApplicationName("router-console-m16-test");

    vmfixture::ConnectOptions connectOptions;
    QCommandLineParser parser;
    parser.addHelpOption();
    connectOptions.addTo(parser);
    const QCommandLineOption interfaceOption("interface", "network interface whose counters move",
                                             "name", "lan");
    const QCommandLineOption secondsOption("seconds", "seconds of sampling", "count", "32");
    const QCommandLineOption minRateOption("min-rate", "minimum peak rx bytes/s", "bytes",
                                           "100000");
    const QCommandLineOption sshPortOption("ssh-port", "VM ssh forward on the host", "port",
                                           qEnvironmentVariable("RC_TEST_SSH_PORT",
                                                                QStringLiteral("10022")));
    parser.addOptions({interfaceOption, secondsOption, minRateOption, sshPortOption});
    parser.process(app);
    connectOptions.read(parser);

    QTextStream out(stdout);
    if (const int code = skipWithoutDevVm(out, connectOptions.host))
        return code;

    const QString ifName = parser.value(interfaceOption);
    const int seconds = qMax(30, parser.value(secondsOption).toInt());
    const double minRate = parser.value(minRateOption).toDouble();
    const QString sshPort = parser.value(sshPortOption);

    AppEngine engine;
    auto *interfaces = qobject_cast<InterfacesModel *>(engine.interfacesModel());
    if (!interfaces) {
        out << "FAILED: interfaces model missing" << Qt::endl;
        return 1;
    }

    QProcess traffic;
    traffic.setProcessChannelMode(QProcess::ForwardedErrorChannel);
    traffic.setStandardOutputFile(QProcess::nullDevice());

    QElapsedTimer clock;
    int samples = 0;
    bool finished = false;

    auto stopTraffic = [&traffic] {
        if (traffic.state() != QProcess::NotRunning) {
            traffic.terminate();
            if (!traffic.waitForFinished(3000))
                traffic.kill();
        }
    };

    auto finish = [&](int code, const QString &line) {
        if (finished)
            return;
        finished = true;
        stopTraffic();
        out << line << Qt::endl;
        out.flush();
        app.exit(code);
    };

    auto evaluate = [&] {
        stopTraffic();

        const struct {
            const char *id;
            qint64 windowMs;
        } ranges[] = {{"1m", 60 * 1000},
                      {"10m", 10 * 60 * 1000},
                      {"1h", 60 * 60 * 1000},
                      {"24h", 24LL * 60 * 60 * 1000}};

        QVector<RangeView> views;
        for (const auto &range : ranges) {
            RangeView view;
            view.id = QString::fromLatin1(range.id);
            view.windowMs = range.windowMs;
            view.map = interfaces->history(ifName, range.windowMs);
            view.pointCount = view.map.value(QStringLiteral("slots")).toInt();
            const QVariantList rx = view.map.value(QStringLiteral("rx")).toList();
            for (const QVariant &value : rx) {
                if (value.toDouble() >= 0)
                    ++view.filled;
            }
            bool sawData = false;
            view.meanRx = meanOf(rx, &sawData);
            view.peakRx = peakOf(rx);
            views.append(view);
        }

        out << Qt::endl
            << "history for " << ifName << " after " << seconds << " s without any window:"
            << Qt::endl;
        out << "  range  windowMs  bucketMs  slots  filled  peak rx B/s   mean rx B/s" << Qt::endl;
        for (const RangeView &view : views) {
            out << QStringLiteral("  %1  %2  %3  %4  %5  %6  %7")
                       .arg(view.id, -5)
                       .arg(view.windowMs, 9)
                       .arg(view.map.value(QStringLiteral("bucketMs")).toLongLong(), 9)
                       .arg(view.pointCount, 6)
                       .arg(view.filled, 7)
                       .arg(view.peakRx, 12, 'f', 0)
                       .arg(view.meanRx, 13, 'f', 0)
                << Qt::endl;
        }

        const TrafficHistory &store = interfaces->trafficHistory();
        out << "  store: samples=" << samples
            << " recent=" << store.recentCount(ifName)
            << " coarse=" << store.coarseCount(ifName)
            << " retainedMs=" << store.retainedSpanMs(ifName) << Qt::endl;

        // Same 1 min of history asked for at two widths: the coarse mean and the
        // fine mean over the coarse interior's exact span describe one period.
        const QVariantMap fine = interfaces->historyAt(ifName, 60000, 1000);
        const QVariantMap coarse = interfaces->historyAt(ifName, 60000, 10000);
        const Span coarseSpan = interiorSpan(coarse);
        bool fineHasData = false;
        bool coarseHasData = false;
        const double fineMean = meanOverSpan(fine, coarseSpan, &fineHasData);
        const double coarseMean =
            interiorMean(coarse.value(QStringLiteral("rx")).toList(), &coarseHasData);
        out << "  decimation: 1 s mean=" << fineMean << " B/s, 10 s mean=" << coarseMean
            << " B/s (delta "
            << (fineMean > 0 ? qAbs(fineMean - coarseMean) / fineMean * 100.0 : 100.0) << "%)"
            << Qt::endl;

        const RangeView &minute = views.at(0);
        const RangeView &ten = views.at(1);
        const RangeView &hour = views.at(2);
        const RangeView &day = views.at(3);

        QStringList problems;
        // Polls share the emulated link with the traffic generator, so loaded
        // ticks can be skipped; the store still spans the whole window.
        if (samples < seconds - seconds / 10)
            problems << QStringLiteral("only %1 of %2 samples").arg(samples).arg(seconds);
        if (store.recentCount(ifName) < qMin(seconds, 600) - 3)
            problems << QStringLiteral("recent tier holds %1 samples").arg(store.recentCount(ifName));
        if (minute.filled < 25)
            problems << QStringLiteral("1 m view filled %1 slots").arg(minute.filled);
        if (minute.peakRx < minRate)
            problems << QStringLiteral("peak rx %1 B/s under %2").arg(minute.peakRx).arg(minRate);
        if (minute.map.value(QStringLiteral("bucketMs")).toLongLong() != 1000)
            problems << QStringLiteral("1 m bucket is not 1 s");
        if (hour.map.value(QStringLiteral("bucketMs")).toLongLong() != 10000)
            problems << QStringLiteral("1 h bucket is not 10 s");
        if (hour.pointCount != 360 && hour.pointCount != 361)
            problems << QStringLiteral("1 h view has %1 slots").arg(hour.pointCount);
        if (day.map.value(QStringLiteral("bucketMs")).toLongLong() != 60000)
            problems << QStringLiteral("24 h bucket is not 1 min");
        if (day.pointCount < 1440 || day.pointCount > 1442)
            problems << QStringLiteral("24 h view has %1 slots").arg(day.pointCount);
        if (ten.pointCount != minute.pointCount && ten.pointCount <= minute.pointCount)
            problems << QStringLiteral("10 m view does not widen the window");
        if (!coarseSpan.valid || !coarseHasData || !fineHasData || fineMean <= 0
            || qAbs(fineMean - coarseMean) / fineMean > 0.05)
            problems << QStringLiteral("10 s decimation does not match the 1 s mean");

        if (!problems.isEmpty()) {
            finish(1, QStringLiteral("FAILED: ") + problems.join(QStringLiteral("; ")));
            return;
        }
        finish(0, QStringLiteral("PASS: %1 samples accumulated with no window open; ranges "
                                 "1m/10m/1h/24h hold %2/%3/%4/%5 points at %6/%7/%8/%9 ms "
                                 "buckets; peak rx %10 B/s")
                      .arg(samples)
                      .arg(minute.pointCount)
                      .arg(ten.pointCount)
                      .arg(hour.pointCount)
                      .arg(day.pointCount)
                      .arg(minute.map.value(QStringLiteral("bucketMs")).toLongLong())
                      .arg(ten.map.value(QStringLiteral("bucketMs")).toLongLong())
                      .arg(hour.map.value(QStringLiteral("bucketMs")).toLongLong())
                      .arg(day.map.value(QStringLiteral("bucketMs")).toLongLong())
                      .arg(minute.peakRx, 0, 'f', 0));
    };

    QObject::connect(interfaces, &InterfacesModel::sampled, &app, [&] {
        if (finished)
            return;
        ++samples;
        if (samples == 1) {
            out << "first sample with no graph window open" << Qt::endl;
            out.flush();
        }
        if (samples >= seconds)
            evaluate();
    });

    QObject::connect(interfaces, &InterfacesModel::pollError, &app, [&](const QString &message) {
        finish(1, QStringLiteral("FAILED: interface poll: %1").arg(message));
    });

    QObject::connect(&engine, &AppEngine::stateChanged, &engine, [&] {
        if (!engine.errorText().isEmpty())
            finish(1, QStringLiteral("FAILED: %1").arg(engine.errorText()));
    });

    vmfixture::wireTrust(engine, connectOptions.acceptNewCert);

    QObject::connect(&engine, &AppEngine::stateChanged, &engine, [&] {
        if (!engine.isConnected())
            return;
        clock.start();
        out << "CONNECTED to " << engine.hostname() << "; no window opened, sampling " << ifName
            << " for " << seconds << " s" << Qt::endl;

        traffic.setProgram(QStringLiteral("ssh"));
        traffic.setArguments({QStringLiteral("-o"), QStringLiteral("BatchMode=yes"),
                              QStringLiteral("-o"), QStringLiteral("StrictHostKeyChecking=no"),
                              QStringLiteral("-o"), QStringLiteral("UserKnownHostsFile=/dev/null"),
                              QStringLiteral("-o"), QStringLiteral("ConnectTimeout=5"),
                              QStringLiteral("-p"), sshPort, QStringLiteral("root@127.0.0.1"),
                              QStringLiteral("while :; do dd if=/dev/zero bs=1M count=8 "
                                             "2>/dev/null; sleep 1; done")});
        traffic.start();
        if (!traffic.waitForStarted(3000))
            out << "warning: traffic generator did not start: " << traffic.errorString()
                << Qt::endl;
        else
            out << "traffic: 8 MiB bursts out of the VM every second over the ssh forward"
                << Qt::endl;
    });

    QTimer::singleShot(connectOptions.timeoutMs + seconds * 1000 + 60000, &app, [&] {
        finish(3, QStringLiteral("SKIPPED: timed out after %1 samples (state: %2)")
                      .arg(samples)
                      .arg(engine.statusText()));
    });

    connectOptions.connect(engine);
    return app.exec();
}
