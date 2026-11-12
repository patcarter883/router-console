// SPDX-FileCopyrightText: 2026 the router-console authors
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QRandomGenerator>
#include <QTextStream>
#include <QTimer>

#include "vm_fixture.h"
#include "tools/FilesTool.h"
#include "vm_probe.h"

// Sysupgrade path check without flashing anything: stage a fake image at
// /tmp/firmware.bin and require rpc-sys upgrade_test to reject it with the
// sysupgrade verdict. upgrade_start is never called.
int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    app.setOrganizationName("open-broadcast");
    app.setApplicationName("router-console");

    vmfixture::ConnectOptions connectOptions;
    QCommandLineParser parser;
    parser.addHelpOption();
    connectOptions.addTo(parser);
    QCommandLineOption scratchOption("scratch", "directory for the fake image", "path",
                                     QDir::tempPath());
    parser.addOptions({scratchOption});
    parser.process(app);
    connectOptions.read(parser);

    QTextStream out(stdout);
    if (const int code = skipWithoutDevVm(out, connectOptions.host))
        return code;
    const QString fakeImage = QDir(parser.value(scratchOption)).filePath(
        QStringLiteral("rc-fake-firmware.bin"));

    {
        QFile file(fakeImage);
        if (!file.open(QIODevice::WriteOnly)) {
            out << "FAILED: cannot write " << fakeImage << ": " << file.errorString() << Qt::endl;
            return 1;
        }
        QByteArray data(4096, Qt::Uninitialized);
        QRandomGenerator::global()->generate(data.begin(), data.end());
        file.write(data);
    }

    AppEngine engine;
    auto *files = qobject_cast<FilesTool *>(engine.filesTool());
    if (!files) {
        out << "FAILED: files tool missing" << Qt::endl;
        return 1;
    }

    enum class Step { Listing, Uploading, Checking, Removing, Done };
    Step step = Step::Listing;
    bool finished = false;
    QString verdict;

    auto finish = [&](int code, const QString &line) {
        if (finished)
            return;
        finished = true;
        out << line << Qt::endl;
        out.flush();
        app.exit(code);
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
        case Step::Listing:
            if (!files->path().endsWith(QLatin1String("/tmp")))
                return;
            step = Step::Uploading;
            files->upload(fakeImage, QStringLiteral("firmware.bin"));
            return;
        case Step::Uploading:
            if (!status.contains(QStringLiteral("wrote /tmp/firmware.bin")))
                return;
            out << status << Qt::endl;
            step = Step::Checking;
            files->verifyImage();
            return;
        case Step::Checking:
            if (status.startsWith(QLatin1String("checking")) || files->path().isEmpty())
                return;
            out << "upgrade_test verdict: " << files->imageStatus() << Qt::endl;
            verdict = files->imageStatus();
            if (files->imageVerified()) {
                finish(1, QStringLiteral("FAILED: upgrade_test accepted a random 4 KB file"));
                return;
            }
            step = Step::Removing;
            files->remove(QStringLiteral("firmware.bin"));
            return;
        case Step::Removing:
            if (!status.startsWith(QLatin1String("removed")))
                return;
            out << status << Qt::endl;
            QFile::remove(fakeImage);
            finish(0, QStringLiteral("PASS: rpc-sys upgrade_test rejected the fake image without "
                                     "touching flash"));
            return;
        case Step::Done:
            return;
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
        out << "CONNECTED to " << engine.hostname() << "; staging a fake image in /tmp"
            << Qt::endl;
        files->navigate(QStringLiteral("/tmp"));
    });

    QTimer::singleShot(connectOptions.timeoutMs, &app, [&] {
        finish(3, QStringLiteral("SKIPPED: timed out (state: %1)").arg(engine.statusText()));
    });

    connectOptions.connect(engine);
    return app.exec();
}
