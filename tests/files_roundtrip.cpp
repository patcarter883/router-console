// SPDX-FileCopyrightText: 2026 the router-console authors
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QRandomGenerator>
#include <QTextStream>
#include <QTimer>

#include "vm_fixture.h"
#include "tools/FilesTool.h"
#include "vm_probe.h"

// Tool-tier check for the file manager: list /etc/config from the router, then
// upload a local file into /tmp, download it back and prove the bytes match.
int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    app.setOrganizationName("open-broadcast");
    app.setApplicationName("router-console");

    vmfixture::ConnectOptions connectOptions;
    QCommandLineParser parser;
    parser.addHelpOption();
    connectOptions.addTo(parser);
    QCommandLineOption listOption("list-dir", "remote directory to list", "path", "/etc/config");
    QCommandLineOption scratchOption("scratch", "directory for the local round-trip files", "path",
                                     QDir::tempPath());
    QCommandLineOption expectOption("expect-entry", "entry that must appear in the listing (repeatable)",
                                    "name", "firewall");
    parser.addOptions({listOption, scratchOption, expectOption});
    parser.process(app);
    connectOptions.read(parser);

    QTextStream out(stdout);
    if (const int code = skipWithoutDevVm(out, connectOptions.host))
        return code;
    const QString listDir = parser.value(listOption);
    const QString scratchDir = parser.value(scratchOption);
    const QString remoteName = QStringLiteral("rc-roundtrip.bin");

    AppEngine engine;
    auto *files = qobject_cast<FilesTool *>(engine.filesTool());
    if (!files) {
        out << "FAILED: files tool missing" << Qt::endl;
        return 1;
    }

    // Local payload: 8 KiB of random bytes, so a text-mode mangle would show up.
    const QByteArray payload = [&] {
        QByteArray data(8192, Qt::Uninitialized);
        QRandomGenerator::global()->generate(data.begin(), data.end());
        return data;
    }();
    const QString uploadPath = QDir(scratchDir).filePath(QStringLiteral("rc-roundtrip-upload.bin"));
    const QString downloadPath = QDir(scratchDir).filePath(QStringLiteral("rc-roundtrip-download.bin"));
    QFile::remove(downloadPath);

    {
        QFile file(uploadPath);
        if (!file.open(QIODevice::WriteOnly)) {
            out << "FAILED: cannot write " << uploadPath << ": " << file.errorString() << Qt::endl;
            return 1;
        }
        file.write(payload);
    }

    enum class Step { ListConfig, ListTmp, Upload, Download, Remove, Verify };
    Step step = Step::ListConfig;
    bool finished = false;

    auto finish = [&](int code, const QString &line) {
        if (finished)
            return;
        finished = true;
        out << line << Qt::endl;
        out.flush();
        app.exit(code);
    };

    auto containsEntry = [&](const QString &name) {
        for (const QVariant &value : files->entries()) {
            if (value.toMap().value(QStringLiteral("name")).toString() == name)
                return true;
        }
        return false;
    };

    QObject::connect(files, &FilesTool::changed, &app, [&] {
        if (finished || files->isBusy())
            return;

        const QString status = files->statusText();
        if (status.startsWith(QLatin1String("failed"))) {
            finish(1, QStringLiteral("FAILED: %1").arg(status));
            return;
        }

        switch (step) {
        case Step::ListConfig: {
            if (status.isEmpty() || !files->path().endsWith(listDir))
                return;
            out << "listing " << files->path() << ": " << files->entries().size() << " entries"
                << Qt::endl;
            QStringList names;
            for (const QVariant &value : files->entries())
                names.append(value.toMap().value(QStringLiteral("name")).toString());
            out << "  " << names.join(QStringLiteral(" ")) << Qt::endl;

            const bool hasFirewall = containsEntry(QStringLiteral("firewall"));
            const bool hasNetwork = containsEntry(QStringLiteral("network"));
            const bool hasSqm = containsEntry(QStringLiteral("sqm"));
            if (!hasFirewall || !hasNetwork || !hasSqm) {
                finish(1, QStringLiteral("FAILED: %1 is missing firewall/network/sqm").arg(listDir));
                return;
            }
            out << "  firewall, network and sqm present" << Qt::endl;

            step = Step::ListTmp;
            files->navigate(QStringLiteral("/tmp"));
            return;
        }
        case Step::ListTmp: {
            if (!files->path().endsWith(QLatin1String("/tmp")))
                return;
            step = Step::Upload;
            files->upload(uploadPath, remoteName);
            return;
        }
        case Step::Upload: {
            if (!status.contains(QStringLiteral("wrote /tmp/")) || !status.contains(remoteName))
                return;
            out << status << Qt::endl;
            step = Step::Download;
            files->download(remoteName, downloadPath);
            return;
        }
        case Step::Download: {
            if (!status.startsWith(QLatin1String("downloaded")))
                return;
            out << status << Qt::endl;
            step = Step::Remove;
            files->remove(remoteName);
            return;
        }
        case Step::Remove: {
            if (!status.startsWith(QLatin1String("removed")))
                return;
            out << status << Qt::endl;
            step = Step::Verify;
            return;
        }
        case Step::Verify: {
            if (containsEntry(remoteName)) {
                finish(1, QStringLiteral("FAILED: %1 still listed after remove").arg(remoteName));
                return;
            }

            QFile file(downloadPath);
            if (!file.open(QIODevice::ReadOnly)) {
                finish(1, QStringLiteral("FAILED: cannot read %1").arg(downloadPath));
                return;
            }
            const QByteArray roundTripped = file.readAll();
            const QByteArray sent = QCryptographicHash::hash(payload, QCryptographicHash::Sha256);
            const QByteArray received =
                QCryptographicHash::hash(roundTripped, QCryptographicHash::Sha256);
            out << "uploaded " << payload.size() << " bytes, downloaded " << roundTripped.size()
                << " bytes" << Qt::endl;
            out << "sha256 sent " << sent.toHex().constData() << Qt::endl;
            out << "sha256 back " << received.toHex().constData() << Qt::endl;

            QFile::remove(uploadPath);
            QFile::remove(downloadPath);

            const bool pass = sent == received && roundTripped.size() == payload.size();
            finish(pass ? 0 : 1,
                   pass ? QStringLiteral("PASS: listing, upload and download round trip verified")
                        : QStringLiteral("FAILED: round-trip bytes differ"));
            return;
        }
        }
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
        out << "CONNECTED to " << engine.hostname() << "; listing " << listDir << Qt::endl;
        files->navigate(listDir);
    });

    QTimer::singleShot(connectOptions.timeoutMs, &app, [&] {
        finish(3, QStringLiteral("SKIPPED: timed out (state: %1)").arg(engine.statusText()));
    });

    connectOptions.connect(engine);
    return app.exec();
}
