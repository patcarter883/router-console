// SPDX-FileCopyrightText: 2026 the router-console authors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QTextStream>
#include <QTimer>

#include "AppEngine.h"
#include "vm_probe.h"

// Headless connect check against the dev VM: drives the same AppEngine the QML
// window uses and asserts the typed board fields arrive. Exit 0 only when the
// session and system.board round-trip both succeeded.
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
    const QCommandLineOption timeoutOption("timeout", "overall timeout in ms", "ms", "15000");
    parser.addOptions({hostOption, userOption, passwordOption, trustOption, timeoutOption});
    parser.process(app);

    QTextStream out(stdout);
    if (const int skip = skipWithoutDevVm(out, parser.value(hostOption)))
        return skip;

    AppEngine engine;
    bool trusted = false;
    bool finished = false;

    QObject::connect(&engine, &AppEngine::stateChanged, &engine, [&] {
        if (engine.trustPending() && parser.isSet(trustOption) && !trusted) {
            trusted = true;
            out << "certificate: trusting " << engine.trustFingerprint() << Qt::endl;
            engine.trustCertificate();
            return;
        }
        if (engine.isConnected()) {
            finished = true;
            out << "CONNECTED" << Qt::endl
                << "  hostname: " << engine.hostname() << Qt::endl
                << "  model:    " << engine.model() << Qt::endl
                << "  firmware: " << engine.firmware() << Qt::endl
                << "  kernel:   " << engine.kernel() << Qt::endl
                << "  target:   " << engine.target() << Qt::endl;
            const bool complete = !engine.hostname().isEmpty() && !engine.model().isEmpty()
                                  && !engine.firmware().isEmpty();
            out.flush();
            app.exit(complete ? 0 : 2);
            return;
        }
        if (!engine.errorText().isEmpty()) {
            finished = true;
            out << "FAILED: " << engine.statusText() << " — " << engine.errorText() << Qt::endl;
            out.flush();
            app.exit(1);
        }
    });

    QTimer::singleShot(parser.value(timeoutOption).toInt(), &app, [&] {
        if (finished)
            return;
        out << "FAILED: timed out, status=" << engine.statusText() << Qt::endl;
        out.flush();
        app.exit(3);
    });

    engine.connectTo(parser.value(hostOption), QString(), parser.value(userOption),
                     parser.value(passwordOption));
    return app.exec();
}
