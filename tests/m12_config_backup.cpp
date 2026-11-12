// SPDX-FileCopyrightText: 2026 the router-console authors
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QTimer>

#include "tools/ArchiveCodec.h"
#include "tools/BackupTool.h"
#include "tools/FilesTool.h"
#include "vm_fixture.h"
#include "vm_probe.h"

namespace {

QByteArray sha(const QByteArray &data)
{
    return QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex();
}

} // namespace

// Config backup check against the dev VM:
//  1. one-button backup produces a non-empty .tar.gz that holds
//     etc/config/network, one member per `uci configs` package plus MANIFEST.txt,
//  2. the archived copy of /etc/config/network is byte-identical to the file the
//     router serves through the file manager,
//  3. restore stages the archive under /tmp/router-console-restore (nothing in
//     /etc/config changes) and that staged copy matches the archive,
//  4. a harmless local archive round-trips through the same stage path.
int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    app.setOrganizationName("open-broadcast");
    app.setApplicationName("router-console");

    vmfixture::ConnectOptions connectOptions;
    QCommandLineParser parser;
    parser.addHelpOption();
    connectOptions.addTo(parser);
    QCommandLineOption outOption("out-dir", "where the test writes backup archives", "path", "");
    parser.addOptions({outOption});
    parser.process(app);
    connectOptions.read(parser);

    QTextStream out(stdout);
    if (const int code = skipWithoutDevVm(out, connectOptions.host))
        return code;

    const QString outDir =
        parser.value(outOption).isEmpty() ? QDir::tempPath() : parser.value(outOption);

    AppEngine engine;
    auto *backup = qobject_cast<BackupTool *>(engine.backupTool());
    auto *files = qobject_cast<FilesTool *>(engine.filesTool());
    if (!backup || !files) {
        out << "FAILED: tools missing" << Qt::endl;
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

    const QString stageDir = QStringLiteral("/tmp/router-console-restore/etc/config");
    const QByteArray harmlessPayload =
        QByteArray("router-console m12 restore probe ")
        + QDateTime::currentDateTime().toString(Qt::ISODate).toUtf8() + '\n';

    int step = 0;
    QByteArray archivedNetwork;
    QByteArray liveNetwork;
    QByteArray stagedNetwork;
    QString harmlessArchive;

    QTimer poll;
    poll.setInterval(200);
    QObject::connect(&poll, &QTimer::timeout, &app, [&] {
        if (finished)
            return;

        const QString backupStatus = backup->statusText();
        const QString fileStatus = files->statusText();
        if (backupStatus.startsWith(QLatin1String("failed"))
            || backupStatus.startsWith(QLatin1String("archive unreadable"))
            || fileStatus.startsWith(QLatin1String("failed"))) {
            fail(backupStatus.startsWith(QLatin1String("failed")) ? backupStatus : fileStatus);
            return;
        }

        switch (step) {
        case 1: // /etc/config listing is in
            if (files->isBusy() || !files->path().endsWith(QLatin1String("/etc/config")))
                return;
            if (files->entries().isEmpty())
                return;
            out << "router lists " << files->entries().size() << " config entries" << Qt::endl;
            backup->backup(outDir);
            step = 2;
            return;

        case 2: { // backup finished
            if (backup->isBusy() || backup->lastArchive().isEmpty())
                return;
            out << backupStatus << Qt::endl;
            out << "  manifest: " << backup->manifestSummary() << Qt::endl;
            for (const QVariant &value : backup->logLines())
                out << "  " << value.toString() << Qt::endl;

            QFile file(backup->lastArchive());
            if (!file.open(QIODevice::ReadOnly))
                return fail(QStringLiteral("cannot read %1").arg(backup->lastArchive()));
            const QByteArray blob = file.readAll();
            out << "  archive " << backup->lastArchive() << ": " << blob.size() << " bytes"
                << Qt::endl;
            if (blob.size() < 1024)
                return fail(QStringLiteral("archive is only %1 bytes").arg(blob.size()));
            if (static_cast<quint8>(blob[0]) != 0x1F || static_cast<quint8>(blob[1]) != 0x8B)
                return fail(QStringLiteral("archive is not a gzip container"));

            QString error;
            const QList<Archive::Member> members = Archive::parseTarGz(blob, &error);
            if (members.isEmpty())
                return fail(QStringLiteral("archive unreadable: %1").arg(error));

            QStringList names;
            for (const Archive::Member &member : members) {
                names.append(member.name);
                if (member.name == QLatin1String("etc/config/network"))
                    archivedNetwork = member.data;
            }
            out << "  " << names.size() << " members: " << names.join(QStringLiteral(" "))
                << Qt::endl;
            if (archivedNetwork.isEmpty())
                return fail(QStringLiteral("archive has no etc/config/network"));
            if (!names.contains(QStringLiteral("MANIFEST.txt")))
                return fail(QStringLiteral("archive has no MANIFEST.txt"));
            if (names.size() != backup->backupFileCount() + 1)
                return fail(QStringLiteral("%1 members for %2 config files")
                                .arg(names.size())
                                .arg(backup->backupFileCount()));
            out << "  archived network: " << archivedNetwork.size() << " bytes, sha256 "
                << sha(archivedNetwork) << Qt::endl;

            files->openFile(QStringLiteral("network"));
            step = 3;
            return;
        }

        case 3: { // live /etc/config/network read back through the file manager
            if (!fileStatus.startsWith(QLatin1String("loaded /etc/config/network")))
                return;
            liveNetwork = files->contentText().toUtf8();
            out << "  live network: " << liveNetwork.size() << " bytes, sha256 "
                << sha(liveNetwork) << Qt::endl;
            if (liveNetwork != archivedNetwork)
                return fail(QStringLiteral("archived network differs from the live file"));
            out << "  backup round trip: identical" << Qt::endl;

            backup->stageRestore(backup->lastArchive());
            step = 4;
            return;
        }

        case 4: // real archive staged
            if (backup->isBusy() || !backup->isRestoreStaged())
                return;
            if (backup->stagedFiles().size() < 2)
                return fail(QStringLiteral("only %1 files staged")
                                .arg(backup->stagedFiles().size()));
            out << "  staged " << backup->stagedFiles().size() << " files under "
                << stageDir << Qt::endl;
            files->navigate(stageDir);
            step = 5;
            return;

        case 5:
            if (files->isBusy() || !files->path().endsWith(QLatin1String("/etc/config")))
                return;
            if (files->entries().isEmpty())
                return;
            files->openFile(QStringLiteral("network"));
            step = 6;
            return;

        case 6: { // staged copy of the real backup
            if (!fileStatus.startsWith(QLatin1String("loaded /tmp/router-console-restore")))
                return;
            stagedNetwork = files->contentText().toUtf8();
            out << "  staged network: " << stagedNetwork.size() << " bytes, sha256 "
                << sha(stagedNetwork) << Qt::endl;
            if (stagedNetwork != archivedNetwork)
                return fail(QStringLiteral("staged network differs from the archive"));

            // Harmless file through the same stage path.
            harmlessArchive = QDir(outDir).filePath(
                QStringLiteral("m12-harmless-%1.tar.gz")
                    .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"))));
            Archive::Member member;
            member.name = QStringLiteral("etc/config/m12-harmless");
            member.data = harmlessPayload;
            member.modified = QDateTime::currentDateTimeUtc();
            Archive::Member manifest;
            manifest.name = QStringLiteral("MANIFEST.txt");
            manifest.data = QByteArray("test archive\n");
            QFile file(harmlessArchive);
            if (!file.open(QIODevice::WriteOnly))
                return fail(QStringLiteral("cannot write %1").arg(harmlessArchive));
            file.write(Archive::buildTarGz({member, manifest}));
            file.close();

            backup->discardRestore();
            backup->stageRestore(harmlessArchive);
            step = 7;
            return;
        }

        case 7:
            if (backup->isBusy() || !backup->isRestoreStaged())
                return;
            if (backup->stagedFiles().size() != 1)
                return fail(QStringLiteral("%1 files staged for a one-file archive")
                                .arg(backup->stagedFiles().size()));
            out << "  harmless file staged as "
                << backup->stagedFiles().first().toMap().value(QStringLiteral("path")).toString()
                << Qt::endl;
            files->openFile(QStringLiteral("m12-harmless"));
            step = 8;
            return;

        case 8: { // harmless file read back from the router's staging directory
            if (!fileStatus.startsWith(QLatin1String("loaded /tmp/router-console-restore")))
                return;
            const QByteArray readBack = files->contentText().toUtf8();
            out << "  staged harmless content: " << readBack.size() << " bytes, sha256 "
                << sha(readBack) << Qt::endl;
            if (readBack != harmlessPayload)
                return fail(QStringLiteral("harmless file did not round-trip"));
            QFile::remove(harmlessArchive);
            finish(0,
                   QStringLiteral("PASS: %1 config files archived, restore staged and "
                                  "round-tripped byte for byte")
                       .arg(backup->backupFileCount()));
            return;
        }

        default:
            return;
        }
    });

    QObject::connect(&engine, &AppEngine::stateChanged, &engine, [&] {
        if (!engine.errorText().isEmpty()) {
            fail(engine.errorText());
            return;
        }
        if (!engine.isConnected() || step != 0)
            return;
        out << "CONNECTED to " << engine.hostname() << Qt::endl;
        step = 1;
        poll.start();
        files->navigate(QStringLiteral("/etc/config"));
    });

    vmfixture::wireTrust(engine, connectOptions.acceptNewCert);

    QTimer::singleShot(connectOptions.timeoutMs, &app, [&] {
        fail(QStringLiteral("timed out at step %1 (state: %2, backup: %3, files: %4)")
                 .arg(step)
                 .arg(engine.statusText(), backup->statusText(), files->statusText()));
    });

    connectOptions.connect(engine);
    return app.exec();
}
