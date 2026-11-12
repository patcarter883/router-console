// SPDX-FileCopyrightText: 2026 the router-console authors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <QCommandLineParser>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>

#include "AppEngine.h"
#include "ui/MdiGeometry.h"
#include "ui/MdiRaiseFilter.h"

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    app.setOrganizationName("open-broadcast");
    app.setApplicationName("router-console");

    // Automation hooks for the dev VM checks; the interactive path is the
    // connect window.
    QCommandLineParser parser;
    parser.setApplicationDescription("WinBox-parity OpenWrt client");
    parser.addHelpOption();
    const QCommandLineOption hostOption({"c", "connect"}, "host[:port] to connect on start", "host");
    const QCommandLineOption userOption({"u", "user"}, "username", "user", "root");
    const QCommandLineOption passwordOption({"p", "password"}, "password", "password");
    const QCommandLineOption trustOption("accept-new-cert",
                                         "accept an unseen router certificate on start");
    const QCommandLineOption interfaceOption("open-interface",
                                             "open the graph window for this interface on connect",
                                             "name");
    const QCommandLineOption configOption("open-config",
                                          "open the config editor for this domain on connect",
                                          "domain");
    const QCommandLineOption configSectionOption(
        "config-section", "select this UCI section in the opened config editor", "name");
    const QCommandLineOption stageOption("stage", "stage option=value in the opened config editor",
                                         "option=value");
    const QCommandLineOption routingOption("open-routing",
                                           "open this routing view on connect (routes, ospf, bgp)",
                                           "id");
    const QCommandLineOption pluginOption("open-plugin",
                                          "open the plugin view for this plugin id on connect",
                                          "id");
    const QCommandLineOption toolOption("open-tool",
                                        "open this tool window on connect (ping, traceroute, "
                                        "portscan, torch, files, sqm, terminal, log, leases, "
                                        "backup); repeatable",
                                        "id");
    const QCommandLineOption layoutOption("mdi-layout",
                                          "arrange the opened windows on connect (tile, cascade)",
                                          "layout");
    parser.addOptions({hostOption, userOption, passwordOption, trustOption, interfaceOption,
                       configOption, configSectionOption, stageOption, pluginOption, routingOption,
                       toolOption, layoutOption});
    parser.process(app);

    auto *engine = new AppEngine(&app);
    if (parser.isSet(trustOption)) {
        QObject::connect(engine, &AppEngine::stateChanged, engine, [engine] {
            if (engine->trustPending())
                engine->trustCertificate();
        });
    }

    qmlRegisterSingletonInstance("RouterConsole", 1, 0, "AppEngine", engine);
    auto *geometry = new MdiGeometryStore(&app);
    qmlRegisterSingletonInstance("RouterConsole", 1, 0, "MdiGeometry", geometry);

    QQmlApplicationEngine qml;
    qml.rootContext()->setContextProperty("startupInterfaces", parser.values(interfaceOption));
    qml.rootContext()->setContextProperty("startupConfigDomains", parser.values(configOption));
    qml.rootContext()->setContextProperty("startupConfigSection",
                                          parser.value(configSectionOption));
    qml.rootContext()->setContextProperty("startupStageValue", parser.value(stageOption));
    qml.rootContext()->setContextProperty("startupPlugins", parser.values(pluginOption));
    qml.rootContext()->setContextProperty("startupTools", parser.values(toolOption));
    qml.rootContext()->setContextProperty("startupRoutingIds", parser.values(routingOption));
    qml.rootContext()->setContextProperty("startupMdiLayout", parser.value(layoutOption));
    qml.loadFromModule("RouterConsole", "Main");
    if (qml.rootObjects().isEmpty())
        return 1;

    // Click-to-front for the MDI frames; the filter only raises, never consumes.
    auto *raiseFilter = new MdiRaiseFilter(qobject_cast<QQuickWindow *>(qml.rootObjects().first()),
                                           &app);
    Q_UNUSED(raiseFilter);

    if (parser.isSet(hostOption)) {
        QMetaObject::invokeMethod(
            engine,
            [engine, host = parser.value(hostOption), user = parser.value(userOption),
             password = parser.value(passwordOption)] {
                engine->connectTo(host, QString(), user, password);
            },
            Qt::QueuedConnection);
    }

    return app.exec();
}
