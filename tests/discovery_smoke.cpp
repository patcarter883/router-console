// SPDX-FileCopyrightText: 2026 the router-console authors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QTextStream>
#include <QTimer>

#include "AppEngine.h"
#include "vm_probe.h"

// Discovery check: starts the same AppEngine the connect window uses and
// asserts the dev VM shows up in the discovered-router list within the budget.
// Exit 0 found, 1 timed out, 3 no discovery interface (test skipped).
int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    app.setOrganizationName("open-broadcast");
    app.setApplicationName("router-console");

    QCommandLineParser parser;
    parser.addHelpOption();
    const QCommandLineOption hostnameOption("hostname", "expected hostname", "name", "openwrt-dev");
    const QCommandLineOption addressOption("address", "expected IPv4 address", "addr",
                                           "192.168.78.1");
    const QCommandLineOption timeoutOption("timeout", "budget in ms", "ms", "10000");
    const QCommandLineOption vmOption("vm", "dev VM rpcd forward used to decide the skip",
                                      "host:port", "127.0.0.1:10443");
    parser.addOptions({hostnameOption, addressOption, timeoutOption, vmOption});
    parser.process(app);

    QTextStream out(stdout);
    if (const int skip = skipWithoutDevVm(out, parser.value(vmOption)))
        return skip;

    QElapsedTimer elapsed;
    elapsed.start();

    AppEngine engine;
    const QString expectedHost = parser.value(hostnameOption);
    const QString expectedAddress = parser.value(addressOption);
    bool done = false;

    auto check = [&] {
        if (done)
            return;
        QAbstractItemModel *model = engine.discoveredHosts();
        for (int row = 0; row < model->rowCount(); ++row) {
            const QModelIndex idx = model->index(row, 0);
            const QString hostname = idx.data(DiscoveredHostsModel::HostnameRole).toString();
            const QString address = idx.data(DiscoveredHostsModel::AddressRole).toString();
            if (hostname.compare(expectedHost, Qt::CaseInsensitive) != 0)
                continue;
            if (!expectedAddress.isEmpty() && address != expectedAddress)
                continue;
            done = true;
            out << "DISCOVERED " << hostname << " " << address << ":"
                << idx.data(DiscoveredHostsModel::PortRole).toInt() << " after "
                << elapsed.elapsed() << " ms" << Qt::endl
                << "  services: " << idx.data(DiscoveredHostsModel::ServicesRole).toString()
                << Qt::endl
                << "  rows: " << model->rowCount() << Qt::endl;
            out.flush();
            app.exit(elapsed.elapsed() <= parser.value(timeoutOption).toInt() ? 0 : 1);
            return;
        }
    };

    QObject::connect(&engine, &AppEngine::discoveredChanged, &engine, check);
    QObject::connect(&engine, &AppEngine::stateChanged, &engine, [&] {
        if (engine.discoveryStatus().startsWith(QLatin1String("discovery idle"))) {
            done = true;
            out << "SKIP: " << engine.discoveryStatus() << Qt::endl;
            out.flush();
            app.exit(3);
        }
    });

    QTimer ticker;
    ticker.setInterval(100);
    QObject::connect(&ticker, &QTimer::timeout, &engine, check);
    ticker.start();

    QTimer::singleShot(parser.value(timeoutOption).toInt(), &app, [&] {
        if (done)
            return;
        done = true;
        QAbstractItemModel *model = engine.discoveredHosts();
        out << "NOT FOUND: " << expectedHost << " (" << expectedAddress << ") within "
            << parser.value(timeoutOption) << " ms; " << model->rowCount()
            << " host(s) discovered, status=" << engine.discoveryStatus() << Qt::endl;
        for (int row = 0; row < model->rowCount(); ++row) {
            const QModelIndex idx = model->index(row, 0);
            out << "  seen: " << idx.data(DiscoveredHostsModel::HostnameRole).toString() << " "
                << idx.data(DiscoveredHostsModel::AddressRole).toString() << " ["
                << idx.data(DiscoveredHostsModel::ServicesRole).toString() << "]" << Qt::endl;
        }
        out.flush();
        app.exit(1);
    });

    check();
    return app.exec();
}
