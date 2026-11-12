// SPDX-FileCopyrightText: 2026 the router-console authors
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QTextStream>
#include <QTimer>

#include "tools/LeaseTool.h"
#include "vm_fixture.h"
#include "vm_probe.h"

// DHCP lease table check. The dev VM's LAN has no DHCP clients (QEMU's slirp
// DHCP answers a DISCOVER before dnsmasq does), so the stable rows are the
// `config host` reservations scripts/vm-m12-fixtures.sh installs; the live-lease
// surface still has to answer, and any row it returns has to carry an address.
int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    app.setOrganizationName("open-broadcast");
    app.setApplicationName("router-console");

    vmfixture::ConnectOptions connectOptions;
    QCommandLineParser parser;
    parser.addHelpOption();
    connectOptions.addTo(parser);
    QCommandLineOption hostnameOption("reserve-hostname", "reservation the table must hold",
                                      "name", "rc-m12-reserved");
    QCommandLineOption addressOption("reserve-address", "address of that reservation", "ip",
                                     "192.168.77.240");
    QCommandLineOption macOption("reserve-mac", "mac of that reservation", "mac",
                                 "52:54:00:12:34:56");
    parser.addOptions({hostnameOption, addressOption, macOption});
    parser.process(app);
    connectOptions.read(parser);

    QTextStream out(stdout);
    if (const int code = skipWithoutDevVm(out, connectOptions.host))
        return code;

    const QString wantHost = parser.value(hostnameOption);
    const QString wantAddress = parser.value(addressOption);
    const QString wantMac = parser.value(macOption);

    AppEngine engine;
    auto *tool = qobject_cast<LeaseTool *>(engine.leaseTool());
    if (!tool) {
        out << "FAILED: lease tool missing" << Qt::endl;
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
    auto fail = [&](const QString &line) { finish(1, QStringLiteral("FAILED: %1").arg(line)); };

    QObject::connect(tool, &LeaseTool::changed, &app, [&] {
        if (finished || tool->isBusy())
            return;
        const QString status = tool->statusText();
        if (status.startsWith(QLatin1String("lease read failed"))) {
            fail(status);
            return;
        }
        if (!status.contains(QLatin1String("rows:")))
            return;

        out << status << Qt::endl;

        bool sawReservation = false;
        for (const QVariant &value : tool->leases()) {
            const QVariantMap row = value.toMap();
            out << "  " << row.value(QStringLiteral("hostname")).toString() << "  "
                << row.value(QStringLiteral("address")).toString() << "  "
                << row.value(QStringLiteral("mac")).toString() << "  "
                << row.value(QStringLiteral("remaining")).toString() << "  "
                << row.value(QStringLiteral("kind")).toString() << Qt::endl;

            const bool dynamic = row.value(QStringLiteral("dynamic")).toBool();
            if (dynamic && row.value(QStringLiteral("address")).toString().isEmpty()) {
                fail(QStringLiteral("a dynamic lease row has no address"));
                return;
            }
            if (!dynamic && row.value(QStringLiteral("hostname")).toString() == wantHost) {
                if (row.value(QStringLiteral("address")).toString() != wantAddress
                    || row.value(QStringLiteral("mac")).toString() != wantMac) {
                    fail(QStringLiteral("reservation %1 reads %2/%3, expected %4/%5")
                             .arg(wantHost,
                                  row.value(QStringLiteral("address")).toString(),
                                  row.value(QStringLiteral("mac")).toString(), wantAddress,
                                  wantMac));
                    return;
                }
                sawReservation = true;
            }
        }

        if (!sawReservation) {
            fail(QStringLiteral("reservation %1 (%2) is missing; "
                                "run scripts/vm-m12-fixtures.sh against the VM")
                     .arg(wantHost, wantAddress));
            return;
        }

        finish(0, QStringLiteral("PASS: %1 dynamic + %2 reserved rows, reservation %3 -> %4 (%5)")
                     .arg(tool->dynamicCount())
                     .arg(tool->reservationCount())
                     .arg(wantHost, wantAddress, wantMac));
    });

    QObject::connect(&engine, &AppEngine::stateChanged, &engine, [&] {
        if (!engine.errorText().isEmpty()) {
            fail(engine.errorText());
            return;
        }
        if (!engine.isConnected())
            return;
        out << "CONNECTED to " << engine.hostname() << Qt::endl;
        tool->refresh();
    });

    vmfixture::wireTrust(engine, connectOptions.acceptNewCert);

    QTimer::singleShot(connectOptions.timeoutMs, &app, [&] {
        fail(QStringLiteral("timed out (state: %1, leases: %2)")
                 .arg(engine.statusText(), tool->statusText()));
    });

    connectOptions.connect(engine);
    return app.exec();
}
