// SPDX-FileCopyrightText: 2026 the router-console authors
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDateTime>
#include <QSet>
#include <QTcpSocket>
#include <QTextStream>
#include <QTimer>

#include "tools/LogTool.h"
#include "vm_fixture.h"
#include "vm_probe.h"

// Log viewer check: the polled `logread` window is fetched and parsed into typed
// entries; a new line produced on the router (a bare TCP connect makes dropbear
// log a child connection) shows up on the next poll; pause freezes the tail and
// the filter narrows the visible rows without dropping any.
int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    app.setOrganizationName("open-broadcast");
    app.setApplicationName("router-console");

    vmfixture::ConnectOptions connectOptions;
    QCommandLineParser parser;
    parser.addHelpOption();
    connectOptions.addTo(parser);
    QCommandLineOption sshPortOption("ssh-port", "router ssh forward used to produce a log line",
                                     "port", "10022");
    QCommandLineOption minimumOption("min-entries", "entries the first fetch must produce", "count",
                                     "20");
    parser.addOptions({sshPortOption, minimumOption});
    parser.process(app);
    connectOptions.read(parser);

    QTextStream out(stdout);
    if (const int code = skipWithoutDevVm(out, connectOptions.host))
        return code;

    const quint16 sshPort = static_cast<quint16>(parser.value(sshPortOption).toUShort());
    const int minEntries = parser.value(minimumOption).toInt();

    AppEngine engine;
    auto *tool = qobject_cast<LogTool *>(engine.logTool());
    if (!tool) {
        out << "FAILED: log tool missing" << Qt::endl;
        return 1;
    }

    bool finished = false;
    int baseline = 0;
    int afterFilter = -1;
    int pausedCount = 0;

    auto finish = [&](int code, const QString &line) {
        if (finished)
            return;
        finished = true;
        out << line << Qt::endl;
        out.flush();
        app.exit(code);
    };

    auto fail = [&](const QString &line) { finish(1, QStringLiteral("FAILED: %1").arg(line)); };

    auto produceLogLine = [&](const char *why) {
        QTcpSocket socket;
        socket.connectToHost(QStringLiteral("127.0.0.1"), sshPort);
        const bool connected = socket.waitForConnected(1500);
        socket.abort();
        out << "  triggered a log line (" << why << "): tcp connect to 127.0.0.1:" << sshPort
            << (connected ? " accepted" : " not accepted") << Qt::endl;
        return connected;
    };

    // Steps run from the tool's changed() signal so every assertion reads the
    // state the window would show.
    QObject::connect(tool, &LogTool::changed, &app, [&] {
        if (finished || tool->isBusy())
            return;

        const QString status = tool->statusText();
        if (status.startsWith(QLatin1String("logread failed"))) {
            fail(status);
            return;
        }

        const QVariantList rows = tool->entries();
        if (baseline == 0) {
            if (rows.isEmpty())
                return;

            baseline = rows.size();
            out << "first fetch: " << tool->totalCount() << " entries — " << status << Qt::endl;
            if (baseline < minEntries) {
                fail(QStringLiteral("only %1 entries, expected at least %2")
                         .arg(baseline)
                         .arg(minEntries));
                return;
            }

            QSet<QString> facilities;
            int unparsedTime = 0;
            int unparsedHeader = 0;
            for (const QVariant &value : rows) {
                const QVariantMap row = value.toMap();
                facilities.insert(row.value(QStringLiteral("level")).toString().section('.', 0, 0));
                if (row.value(QStringLiteral("time")).toString() == QLatin1String("-"))
                    ++unparsedTime;
                if (row.value(QStringLiteral("level")).toString().isEmpty())
                    ++unparsedHeader;
                if (row.value(QStringLiteral("message")).toString().isEmpty())
                    ++unparsedHeader;
            }
            out << "  facilities: " << QStringList(facilities.values()).join(QStringLiteral(" "))
                << Qt::endl;
            out << "  unparsed timestamps: " << unparsedTime << ", unparsed headers: "
                << unparsedHeader << Qt::endl;
            if (unparsedTime > 0) {
                fail(QStringLiteral("%1 entries carry no timestamp").arg(unparsedTime));
                return;
            }
            if (!facilities.contains(QStringLiteral("kern"))
                && !facilities.contains(QStringLiteral("authpriv"))) {
                fail(QStringLiteral("neither kern nor authpriv lines parsed"));
                return;
            }

            // Filter narrows the rows; the total stays put.
            tool->setFilter(QStringLiteral("dropbear"));
            afterFilter = tool->entries().size();
            out << "  filter 'dropbear': " << afterFilter << " of " << tool->totalCount()
                << " rows" << Qt::endl;
            if (afterFilter <= 0 || afterFilter >= rows.size()) {
                fail(QStringLiteral("filter did not narrow the rows"));
                return;
            }
            for (const QVariant &value : tool->entries()) {
                const QVariantMap row = value.toMap();
                const QString haystack = row.value(QStringLiteral("level")).toString()
                                         + row.value(QStringLiteral("source")).toString()
                                         + row.value(QStringLiteral("message")).toString();
                if (!haystack.contains(QStringLiteral("dropbear"))) {
                    fail(QStringLiteral("filtered row does not match: %1").arg(haystack));
                    return;
                }
            }
            if (tool->totalCount() != baseline) {
                fail(QStringLiteral("filter changed the entry count"));
                return;
            }
            tool->setFilter(QString());

            // New line from the router: pause first so the count cannot move.
            tool->setPaused(true);
            pausedCount = tool->totalCount();
            produceLogLine("paused");
            QTimer::singleShot(4000, &app, [&] {
                if (finished)
                    return;
                if (tool->totalCount() != pausedCount) {
                    fail(QStringLiteral("paused tail grew from %1 to %2")
                             .arg(pausedCount)
                             .arg(tool->totalCount()));
                    return;
                }
                out << "  paused: tail stayed at " << pausedCount << " entries" << Qt::endl;
                produceLogLine("resumed");
                tool->setPaused(false); // restarts the 2 s poller
                QTimer::singleShot(8000, &app, [&] {
                    if (finished)
                        return;
                    const int grown = tool->totalCount();
                    out << "  resumed: tail grew to " << grown << " entries" << Qt::endl;
                    if (grown <= pausedCount) {
                        fail(QStringLiteral("no new line picked up after resume"));
                        return;
                    }
                    const QVariantMap newest = tool->entries().last().toMap();
                    out << "  newest: " << newest.value(QStringLiteral("time")).toString() << " "
                        << newest.value(QStringLiteral("level")).toString() << " "
                        << newest.value(QStringLiteral("source")).toString() << ": "
                        << newest.value(QStringLiteral("message")).toString() << Qt::endl;
                    finish(0, QStringLiteral("PASS: %1 entries parsed, poller appended %2 new lines, "
                                             "pause and filter verified")
                                .arg(baseline)
                                .arg(grown - pausedCount));
                });
            });
            return;
        }
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
        fail(QStringLiteral("timed out (state: %1, log: %2)")
                 .arg(engine.statusText(), tool->statusText()));
    });

    connectOptions.connect(engine);
    return app.exec();
}
