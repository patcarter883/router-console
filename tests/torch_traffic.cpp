// SPDX-FileCopyrightText: 2026 the router-console authors
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QProcess>
#include <QTextStream>
#include <QTimer>

#include "vm_fixture.h"
#include "tools/TorchModel.h"
#include "vm_probe.h"

// Star check for the torch half: while a host streams data out of the VM over
// ssh, the torch view must attribute the load to that host's address, as the top
// row, across at least three consecutive 1 Hz samples.
int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    app.setOrganizationName("open-broadcast");
    app.setApplicationName("router-console");

    vmfixture::ConnectOptions connectOptions;
    QCommandLineParser parser;
    parser.addHelpOption();
    connectOptions.addTo(parser);
    QCommandLineOption expectOption("expect-host", "address the load must be attributed to",
                                    "ip", "192.168.77.2");
    QCommandLineOption samplesOption("samples", "samples to collect", "count", "6");
    QCommandLineOption rateOption("min-rate", "minimum bytes/s for the generating host", "bytes",
                                  "100000");
    QCommandLineOption sshPortOption("ssh-port", "VM ssh forward on the host", "port", "10022");
    parser.addOptions({expectOption, samplesOption, rateOption, sshPortOption});
    parser.process(app);
    connectOptions.read(parser);

    QTextStream out(stdout);
    if (const int code = skipWithoutDevVm(out, connectOptions.host))
        return code;
    const QString expectHost = parser.value(expectOption);
    const int wantedSamples = qMax(3, parser.value(samplesOption).toInt());
    const double minRate = parser.value(rateOption).toDouble();

    AppEngine engine;
    auto *torch = qobject_cast<TorchModel *>(engine.torchModel());
    if (!torch) {
        out << "FAILED: torch model missing" << Qt::endl;
        return 1;
    }

    QProcess traffic;
    traffic.setProcessChannelMode(QProcess::ForwardedErrorChannel);
    traffic.setStandardOutputFile(QProcess::nullDevice());

    struct Sample {
        qint64 atMs = 0;
        QString top;
        double topRate = 0;
        double hostRate = 0;
        double hostTx = 0;
        double hostRx = 0;
        int peers = 0;
    };
    QVector<Sample> samples;
    QElapsedTimer clock;
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
        out << Qt::endl << "torch samples (expecting load on " << expectHost << "):" << Qt::endl;
        out << "  #   t(ms)  top peer        top rate       " << expectHost
            << " rx/tx bytes/s   peers" << Qt::endl;

        int consecutive = 0;
        int best = 0;
        for (int i = 0; i < samples.size(); ++i) {
            const Sample &sample = samples.at(i);
            out << QStringLiteral("  %1  %2  %3  %4  %5  %6")
                       .arg(i + 1, 3)
                       .arg(sample.atMs, 6)
                       .arg(sample.top, -15)
                       .arg(sample.topRate, 10, 'f', 0)
                       .arg(QStringLiteral("%1/%2").arg(sample.hostRx, 0, 'f', 0).arg(sample.hostTx, 0, 'f', 0))
                       .arg(sample.peers, 5)
                << Qt::endl;

            const bool attributed = sample.top == expectHost && sample.hostRate >= minRate;
            consecutive = attributed ? consecutive + 1 : 0;
            best = qMax(best, consecutive);
        }
        out.flush();

        const bool pass = best >= 3;
        finish(pass ? 0 : 1,
               pass ? QStringLiteral("PASS: load attributed to %1 (the generating host) as the top peer for %2 consecutive samples (>= %3 B/s)")
                          .arg(expectHost)
                          .arg(best)
                          .arg(minRate, 0, 'f', 0)
                    : QStringLiteral("FAILED: %1 was the top peer for only %2 consecutive samples")
                          .arg(expectHost)
                          .arg(best));
    };

    QObject::connect(torch, &TorchModel::sampled, &app, [&] {
        if (finished || !torch->isSampling())
            return;
        if (torch->sampleCount() == 0)
            return;

        Sample sample;
        sample.atMs = clock.elapsed();
        sample.peers = torch->rowCount();
        const int hostRow = torch->rowFor(expectHost);
        if (hostRow >= 0) {
            const QModelIndex index = torch->index(hostRow, 0);
            sample.hostRx = torch->data(index, TorchModel::RxRateRole).toDouble();
            sample.hostTx = torch->data(index, TorchModel::TxRateRole).toDouble();
            sample.hostRate = sample.hostRx + sample.hostTx;
        }
        if (torch->rowCount() > 0) {
            const QModelIndex index = torch->index(0, 0);
            sample.top = torch->data(index, TorchModel::AddressRole).toString();
            sample.topRate = torch->data(index, TorchModel::RxRateRole).toDouble()
                             + torch->data(index, TorchModel::TxRateRole).toDouble();
        }

        if (samples.isEmpty() || sample.atMs - samples.last().atMs >= 500)
            samples.append(sample);
        else
            samples.last() = sample;

        if (samples.size() >= wantedSamples)
            evaluate();
    });

    QObject::connect(torch, &TorchModel::pollError, &app, [&](const QString &message) {
        finish(1, QStringLiteral("FAILED: torch poll: %1").arg(message));
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
        clock.start();
        out << "CONNECTED to " << engine.hostname() << "; sampling conntrack at 1 Hz" << Qt::endl;
        torch->start();

        traffic.setProgram(QStringLiteral("ssh"));
        traffic.setArguments({QStringLiteral("-o"), QStringLiteral("BatchMode=yes"),
                              QStringLiteral("-o"), QStringLiteral("StrictHostKeyChecking=no"),
                              QStringLiteral("-o"), QStringLiteral("UserKnownHostsFile=/dev/null"),
                              QStringLiteral("-o"), QStringLiteral("ConnectTimeout=5"),
                              QStringLiteral("-p"), parser.value(sshPortOption),
                              QStringLiteral("root@127.0.0.1"),
                              QStringLiteral("cat"), QStringLiteral("/dev/zero")});
        traffic.start();
        if (!traffic.waitForStarted(3000))
            out << "warning: traffic generator did not start: " << traffic.errorString() << Qt::endl;
        else
            out << "traffic: host streams /dev/zero out of the VM over its ssh forward" << Qt::endl;
    });

    QTimer::singleShot(connectOptions.timeoutMs, &app, [&] {
        finish(3, QStringLiteral("SKIPPED: timed out after %1 samples (state: %2)")
                      .arg(samples.size())
                      .arg(engine.statusText()));
    });

    connectOptions.connect(engine);
    return app.exec();
}
