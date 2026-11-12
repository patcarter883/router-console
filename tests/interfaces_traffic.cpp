#include <QCommandLineParser>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QProcess>
#include <QTextStream>
#include <QTimer>

#include "AppEngine.h"
#include "models/InterfacesModel.h"

// Live-graph check against the dev VM: connect, push traffic through an
// interface, then assert the per-interface byte counters the model samples at
// 1 Hz increase across >= 5 consecutive samples (6 samples, 5 deltas).
//
// Traffic is generated from inside the VM over its ssh forward; the stream is
// discarded on the host, so only the router's interface counters change.
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
    const QCommandLineOption interfaceOption("interface", "interface whose counters must grow",
                                             "name", "br-lan");
    const QCommandLineOption samplesOption("samples", "samples to collect", "count", "6");
    const QCommandLineOption timeoutOption("timeout", "overall timeout in ms", "ms", "45000");
    const QCommandLineOption sshPortOption("ssh-port", "VM ssh forward on the host", "port", "10022");
    const QCommandLineOption trafficOption(
        "traffic-cmd",
        "program generating traffic from the VM (default: ssh ... cat /dev/zero)", "path");
    parser.addOptions({hostOption, userOption, passwordOption, trustOption, interfaceOption,
                       samplesOption, timeoutOption, sshPortOption, trafficOption});
    parser.process(app);

    QTextStream out(stdout);
    const int wantedSamples = qMax(2, parser.value(samplesOption).toInt());
    const QString interfaceName = parser.value(interfaceOption);

    AppEngine engine;
    auto *model = qobject_cast<InterfacesModel *>(engine.interfacesModel());
    if (!model) {
        out << "FAILED: interfaces model missing" << Qt::endl;
        return 1;
    }

    QProcess traffic;
    traffic.setProcessChannelMode(QProcess::ForwardedErrorChannel);
    traffic.setStandardOutputFile(QProcess::nullDevice());

    QElapsedTimer clock;
    qint64 previousSampleMs = -1;
    QVector<qint64> sampleTimes;
    QVector<quint64> rxSamples;
    QVector<quint64> txSamples;
    QVector<double> rateSamples;
    bool trusted = false;
    bool finished = false;

    auto stopTraffic = [&traffic] {
        if (traffic.state() != QProcess::NotRunning) {
            traffic.terminate();
            if (!traffic.waitForFinished(3000))
                traffic.kill();
        }
    };

    auto fail = [&](const QString &message) {
        if (finished)
            return;
        finished = true;
        stopTraffic();
        out << "FAILED: " << message << Qt::endl;
        out.flush();
        app.exit(1);
    };

    auto evaluate = [&] {
        finished = true;
        stopTraffic();

        out << Qt::endl << "samples on " << interfaceName << ":" << Qt::endl;
        out << "  #   t(ms)   rx_bytes      tx_bytes      rx_rate" << Qt::endl;
        bool ok = !rxSamples.isEmpty();
        int growing = 0;
        for (int i = 0; i < rxSamples.size(); ++i) {
            out << QStringLiteral("  %1  %2  %3  %4  %5")
                       .arg(i + 1, 3)
                       .arg(sampleTimes.at(i), 6)
                       .arg(rxSamples.at(i), 12)
                       .arg(txSamples.at(i), 12)
                       .arg(rxSamples.at(i) > 0 ? QStringLiteral("%1 kB/s")
                                                      .arg(rateSamples.at(i) / 1024.0, 0, 'f', 1)
                                                : QStringLiteral("-"))
                << Qt::endl;

            if (i == 0)
                continue;

            const qint64 intervalMs = sampleTimes.at(i) - sampleTimes.at(i - 1);
            const bool countersGrew = rxSamples.at(i) > rxSamples.at(i - 1)
                                      && txSamples.at(i) > txSamples.at(i - 1);
            const bool cadenceOk = intervalMs >= 600 && intervalMs <= 1600;
            if (countersGrew && cadenceOk) {
                ++growing;
            } else {
                ok = false;
                out << "    break at sample " << i + 1 << ": interval " << intervalMs
                    << " ms, rx +" << (rxSamples.at(i) - rxSamples.at(i - 1)) << ", tx +"
                    << (txSamples.at(i) - txSamples.at(i - 1)) << Qt::endl;
            }
        }

        out << Qt::endl << "consecutive growing 1 Hz intervals: " << growing << " of "
            << (rxSamples.size() - 1) << Qt::endl;
        out.flush();

        const bool pass = ok && growing >= 5 && rxSamples.size() >= 6;
        app.exit(pass ? 0 : 1);
    };

    QObject::connect(model, &InterfacesModel::sampled, &engine, [&] {
        if (finished)
            return;

        const int row = model->rowFor(interfaceName);
        if (row < 0)
            return;

        const QModelIndex index = model->index(row, 0);
        const quint64 rx = model->data(index, InterfacesModel::RxBytesRole).toULongLong();
        const quint64 tx = model->data(index, InterfacesModel::TxBytesRole).toULongLong();
        const double rate = model->data(index, InterfacesModel::RxRateRole).toDouble();

        const qint64 nowMs = clock.elapsed();
        if (previousSampleMs >= 0) {
            const qint64 interval = nowMs - previousSampleMs;
            // The model polls once per second; anything much slower means the
            // graph would not reach 1 Hz.
            if (interval > 1800)
                out << "  warning: " << interval << " ms between samples" << Qt::endl;
        }
        previousSampleMs = nowMs;

        sampleTimes.append(nowMs);
        rxSamples.append(rx);
        txSamples.append(tx);
        rateSamples.append(rate);

        if (rxSamples.size() >= wantedSamples)
            evaluate();
    });

    QObject::connect(model, &InterfacesModel::pollError, &engine, [&](const QString &message) {
        fail(QStringLiteral("poll failed: %1").arg(message));
    });

    QObject::connect(&engine, &AppEngine::stateChanged, &engine, [&] {
        if (engine.trustPending() && parser.isSet(trustOption) && !trusted) {
            trusted = true;
            engine.trustCertificate();
            return;
        }
        if (engine.isConnected()) {
            clock.start();
            if (parser.isSet(trafficOption)) {
                traffic.setProgram(parser.value(trafficOption));
            } else {
                traffic.setProgram(QStringLiteral("ssh"));
                traffic.setArguments({QStringLiteral("-o"), QStringLiteral("BatchMode=yes"),
                                      QStringLiteral("-o"), QStringLiteral("StrictHostKeyChecking=no"),
                                      QStringLiteral("-o"), QStringLiteral("UserKnownHostsFile=/dev/null"),
                                      QStringLiteral("-o"), QStringLiteral("ConnectTimeout=5"),
                                      QStringLiteral("-p"), parser.value(sshPortOption),
                                      QStringLiteral("root@127.0.0.1"),
                                      QStringLiteral("cat"), QStringLiteral("/dev/zero")});
            }
            out << "CONNECTED to " << engine.hostname() << ", sampling " << interfaceName
                << " at 1 Hz" << Qt::endl;
            traffic.start();
            if (!traffic.waitForStarted(3000))
                out << "warning: traffic generator did not start: " << traffic.errorString()
                    << Qt::endl;
            else
                out << "traffic: streaming from inside the VM over its ssh forward" << Qt::endl;
            return;
        }
        if (!engine.errorText().isEmpty())
            fail(engine.errorText());
    });

    QTimer::singleShot(parser.value(timeoutOption).toInt(), &app, [&] {
        if (finished)
            return;
        finished = true;
        stopTraffic();
        out << "SKIPPED: timed out after " << rxSamples.size() << " samples (state: "
            << engine.statusText() << ")" << Qt::endl;
        out.flush();
        app.exit(3);
    });

    engine.connectTo(parser.value(hostOption), QString(), parser.value(userOption),
                     parser.value(passwordOption));
    return app.exec();
}
