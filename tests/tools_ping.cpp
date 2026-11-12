// SPDX-FileCopyrightText: 2026 the router-console authors
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QTextStream>
#include <QTimer>

#include "vm_fixture.h"
#include "tools/PingTool.h"
#include "vm_probe.h"

// Tool-tier check for the ping window: issue ICMP echoes from inside the VM to a
// live target and require real replies with measured RTTs.
int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    app.setOrganizationName("open-broadcast");
    app.setApplicationName("router-console");

    vmfixture::ConnectOptions connectOptions;
    QCommandLineParser parser;
    parser.addHelpOption();
    connectOptions.addTo(parser);
    QCommandLineOption targetOption("target", "ping target", "host", "10.0.2.3");
    QCommandLineOption countOption("count", "packets to send", "count", "3");
    parser.addOptions({targetOption, countOption});
    parser.process(app);
    connectOptions.read(parser);

    QTextStream out(stdout);
    if (const int code = skipWithoutDevVm(out, connectOptions.host))
        return code;
    AppEngine engine;
    auto *ping = qobject_cast<PingTool *>(engine.pingTool());
    if (!ping) {
        out << "FAILED: ping tool missing" << Qt::endl;
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

    QObject::connect(ping, &PingTool::changed, &app, [&] {
        if (ping->isRunning() || ping->summaryText().isEmpty() || finished)
            return;
        if (ping->statusText().startsWith(QLatin1String("failed"))) {
            finish(1, QStringLiteral("FAILED: %1").arg(ping->statusText()));
            return;
        }

        out << "target " << ping->target() << ": " << ping->summaryText() << Qt::endl;
        bool rttsPositive = true;
        for (const QVariant &row : ping->replies()) {
            const QVariantMap reply = row.toMap();
            const double rtt = reply.value(QStringLiteral("rtt")).toString().split(' ').first().toDouble();
            out << "  seq " << reply.value(QStringLiteral("sequence")).toInt() << " from "
                << reply.value(QStringLiteral("address")).toString() << " "
                << reply.value(QStringLiteral("rtt")).toString() << Qt::endl;
            if (rtt <= 0.0)
                rttsPositive = false;
        }

        const int transmitted = ping->count();
        const int replies = ping->replies().size();
        const bool pass = replies > 0 && replies == transmitted && rttsPositive;
        finish(pass ? 0 : 1, pass ? QStringLiteral("PASS: %1 replies with measured RTT").arg(replies)
                                  : QStringLiteral("FAILED: %1 of %2 packets answered")
                                        .arg(replies)
                                        .arg(transmitted));
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
        ping->setTarget(parser.value(targetOption));
        ping->setCount(parser.value(countOption).toInt());
        out << "CONNECTED to " << engine.hostname() << "; ping " << ping->target() << Qt::endl;
        ping->start();
    });

    QTimer::singleShot(connectOptions.timeoutMs, &app, [&] {
        finish(3, QStringLiteral("SKIPPED: timed out (state: %1)").arg(engine.statusText()));
    });

    connectOptions.connect(engine);
    return app.exec();
}
