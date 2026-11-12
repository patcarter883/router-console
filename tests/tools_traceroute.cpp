// SPDX-FileCopyrightText: 2026 the router-console authors
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QTextStream>
#include <QTimer>

#include "vm_fixture.h"
#include "tools/TracerouteTool.h"

// Tool-tier check for the traceroute window: the router traces a live target and
// the first hop must be a real address with a measured RTT.
int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    app.setOrganizationName("open-broadcast");
    app.setApplicationName("router-console");

    vmfixture::ConnectOptions connectOptions;
    QCommandLineParser parser;
    parser.addHelpOption();
    connectOptions.addTo(parser);
    QCommandLineOption targetOption("target", "traceroute target", "host", "10.0.2.3");
    QCommandLineOption hopsOption("max-hops", "hop limit", "hops", "6");
    parser.addOptions({targetOption, hopsOption});
    parser.process(app);
    connectOptions.read(parser);

    QTextStream out(stdout);
    AppEngine engine;
    auto *trace = qobject_cast<TracerouteTool *>(engine.tracerouteTool());
    if (!trace) {
        out << "FAILED: traceroute tool missing" << Qt::endl;
        return 1;
    }

    bool finished = false;
    auto finish = [&](int code, const QString &line) {
        if (finished)
            return;
        finished = true;
        out << line << Qt::endl;
        out.flush();
        app.exit(code);
    };

    QObject::connect(trace, &TracerouteTool::changed, &app, [&] {
        if (trace->isRunning() || trace->statusText().isEmpty() || finished)
            return;
        if (trace->statusText().startsWith(QLatin1String("failed"))) {
            finish(1, QStringLiteral("FAILED: %1").arg(trace->statusText()));
            return;
        }

        const QVariantList hops = trace->hops();
        out << "target " << trace->target() << ": " << trace->statusText() << Qt::endl;
        for (const QVariant &value : hops) {
            const QVariantMap hop = value.toMap();
            out << "  " << hop.value(QStringLiteral("ttl")).toInt() << "  "
                << hop.value(QStringLiteral("address")).toString() << "  "
                << hop.value(QStringLiteral("rtt")).toString() << "  "
                << hop.value(QStringLiteral("note")).toString() << Qt::endl;
        }

        bool firstHopOk = false;
        if (!hops.isEmpty()) {
            const QVariantMap first = hops.first().toMap();
            const QString address = first.value(QStringLiteral("address")).toString();
            const double rtt = first.value(QStringLiteral("rtt")).toString().split(' ').first().toDouble();
            firstHopOk = !address.isEmpty() && address != QLatin1String("*") && rtt > 0.0;
        }

        const bool pass = hops.size() >= 1 && firstHopOk;
        finish(pass ? 0 : 1,
               pass ? QStringLiteral("PASS: %1 hops, first hop %2")
                          .arg(hops.size())
                          .arg(hops.first().toMap().value(QStringLiteral("address")).toString())
                    : QStringLiteral("FAILED: no usable first hop"));
    });

    QObject::connect(&engine, &AppEngine::stateChanged, &engine, [&] {
        if (engine.errorText().isEmpty())
            return;
        finish(1, QStringLiteral("FAILED: %1").arg(engine.errorText()));
    });

    vmfixture::wireTrust(engine, connectOptions.acceptNewCert);

    QObject::connect(&engine, &AppEngine::stateChanged, &engine, [&] {
        if (!engine.isConnected())
            return;
        trace->setTarget(parser.value(targetOption));
        trace->setMaxHops(parser.value(hopsOption).toInt());
        out << "CONNECTED to " << engine.hostname() << "; tracing " << trace->target() << Qt::endl;
        trace->start();
    });

    QTimer::singleShot(connectOptions.timeoutMs, &app, [&] {
        finish(3, QStringLiteral("SKIPPED: timed out (state: %1)").arg(engine.statusText()));
    });

    connectOptions.connect(engine);
    return app.exec();
}
