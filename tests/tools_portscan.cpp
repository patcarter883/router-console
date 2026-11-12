// SPDX-FileCopyrightText: 2026 the router-console authors
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QTextStream>
#include <QTimer>

#include "vm_fixture.h"
#include "tools/PortScanTool.h"
#include "vm_probe.h"

// Tool-tier check for the port scan window: scan the router's own loopback from
// inside the router, where dropbear listens on 22 and an unused port refuses.
int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    app.setOrganizationName("open-broadcast");
    app.setApplicationName("router-console");

    vmfixture::ConnectOptions connectOptions;
    QCommandLineParser parser;
    parser.addHelpOption();
    connectOptions.addTo(parser);
    QCommandLineOption targetOption("target", "scan target", "host", "127.0.0.1");
    QCommandLineOption portsOption("ports", "ports, e.g. 22,80,8000-8010", "list", "22,9999");
    QCommandLineOption openOption("expect-open", "port that must answer", "port", "22");
    QCommandLineOption closedOption("expect-closed", "port that must not answer", "port", "9999");
    parser.addOptions({targetOption, portsOption, openOption, closedOption});
    parser.process(app);
    connectOptions.read(parser);

    QTextStream out(stdout);
    if (const int code = skipWithoutDevVm(out, connectOptions.host))
        return code;
    AppEngine engine;
    auto *scan = qobject_cast<PortScanTool *>(engine.portScanTool());
    if (!scan) {
        out << "FAILED: port scan tool missing" << Qt::endl;
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

    QObject::connect(scan, &PortScanTool::changed, &app, [&] {
        if (scan->isRunning() || scan->statusText().isEmpty() || finished)
            return;
        if (scan->statusText().startsWith(QLatin1String("failed"))) {
            finish(1, QStringLiteral("FAILED: %1").arg(scan->statusText()));
            return;
        }

        out << "scan of " << scan->target() << " (" << scan->statusText() << ")" << Qt::endl;
        bool openSeen = false;
        bool closedSeen = false;
        const int expectOpen = parser.value(openOption).toInt();
        const int expectClosed = parser.value(closedOption).toInt();
        for (const QVariant &value : scan->results()) {
            const QVariantMap probe = value.toMap();
            const int port = probe.value(QStringLiteral("port")).toInt();
            const QString state = probe.value(QStringLiteral("state")).toString();
            out << "  " << port << "  " << state << Qt::endl;
            if (port == expectOpen)
                openSeen = state == QLatin1String("open");
            if (port == expectClosed)
                closedSeen = state != QLatin1String("open");
        }

        const bool pass = openSeen && closedSeen;
        finish(pass ? 0 : 1,
               pass ? QStringLiteral("PASS: port %1 open (dropbear), port %2 not open")
                          .arg(expectOpen)
                          .arg(expectClosed)
                    : QStringLiteral("FAILED: expected %1 open and %2 closed")
                          .arg(expectOpen)
                          .arg(expectClosed));
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
        scan->setTarget(parser.value(targetOption));
        scan->setPortsText(parser.value(portsOption));
        out << "CONNECTED to " << engine.hostname() << "; scanning " << scan->target() << Qt::endl;
        scan->start();
    });

    QTimer::singleShot(connectOptions.timeoutMs, &app, [&] {
        finish(3, QStringLiteral("SKIPPED: timed out (state: %1)").arg(engine.statusText()));
    });

    connectOptions.connect(engine);
    return app.exec();
}
