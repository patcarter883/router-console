// SPDX-FileCopyrightText: 2026 the router-console authors
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "BackupTool.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTime>

#include "ToolFormat.h"
#include "protocol/UbusClient.h"

namespace {

const QString kBackupTag = QStringLiteral("backup");
const QString kManifestTag = QStringLiteral("backup-manifest");
const QString kMkdirTag = QStringLiteral("restore-mkdir");
const QString kStageTag = QStringLiteral("restore-stage");
const QString kApplyTag = QStringLiteral("restore-apply");
const QString kConfigDir = QStringLiteral("etc/config");
const QString kStageRoot = QStringLiteral("/tmp/router-console-restore");

bool isConfigMember(const Archive::Member &member)
{
    const QString prefix = kConfigDir + QLatin1Char('/');
    if (!member.name.startsWith(prefix))
        return false;
    return !member.name.mid(prefix.size()).contains(QLatin1Char('/'));
}

} // namespace

BackupTool::BackupTool(UbusClient *client, QObject *parent) : QObject(parent), m_client(client)
{
    connect(m_client, &UbusClient::configNamesReceived, this, &BackupTool::onConfigNames);
    connect(m_client, &UbusClient::fileReadTaggedFinished, this, &BackupTool::onRead);
    connect(m_client, &UbusClient::fileWriteTaggedFinished, this, &BackupTool::onWrite);
    connect(m_client, &UbusClient::execTaggedFinished, this, &BackupTool::onExec);
    connect(m_client, &UbusClient::configNamesFailed, this, &BackupTool::onFailure);
    connect(m_client, &UbusClient::fileReadTaggedFailed, this,
            [this](const QString &tag, const Ubus::Error &error) {
                if (tag == kBackupTag)
                    onFailure(error);
            });
    connect(m_client, &UbusClient::fileWriteTaggedFailed, this,
            [this](const QString &, const Ubus::Error &error) { onFailure(error); });
    connect(m_client, &UbusClient::execTaggedFailed, this,
            [this](const QString &tag, const Ubus::Error &error) {
                if (tag == kMkdirTag) {
                    m_busy = false;
                    m_status = QStringLiteral("cannot create %1: %2").arg(kStageRoot,
                                                                          error.message);
                    appendLog(m_status);
                    emit changed();
                    return;
                }
                if (tag == kManifestTag) {
                    // The package list is a manifest nicety: a router without
                    // apk still gets an archive, with the reason recorded.
                    m_manifest = QStringLiteral("created=%1\npackage list unavailable: %2\n")
                                     .arg(QDateTime::currentDateTime().toString(Qt::ISODate),
                                          error.message);
                    m_manifestSummary = QStringLiteral("package list unavailable");
                    finishBackup();
                }
            });
}

QString BackupTool::lastArchiveSize() const
{
    return m_archiveBytes > 0 ? QStringLiteral("%1 kB").arg(m_archiveBytes / 1000.0, 0, 'f', 1)
                              : QString();
}

QVariantList BackupTool::stagedFiles() const
{
    QVariantList rows;
    rows.reserve(m_staged.size());
    for (const Archive::Member &member : m_staged) {
        rows.append(QVariantMap{{QStringLiteral("path"), QLatin1Char('/') + member.name},
                                {QStringLiteral("size"), QString::number(member.data.size())}});
    }
    return rows;
}

void BackupTool::appendLog(const QString &line)
{
    m_logLines.append(QStringLiteral("%1  %2").arg(QTime::currentTime().toString(
                                                      QStringLiteral("HH:mm:ss")),
                                                  line));
    while (m_logLines.size() > 200)
        m_logLines.removeFirst();
}

void BackupTool::backup(const QString &localDir)
{
    if (m_busy)
        return;
    m_busy = true;
    m_targetDir = localDir.trimmed().isEmpty()
                      ? QDir::homePath() + QStringLiteral("/router-backups")
                      : localDir.trimmed();
    m_configNames.clear();
    m_members.clear();
    m_manifest.clear();
    m_manifestSummary.clear();
    m_archiveBytes = 0;
    m_configIndex = 0;
    m_status = QStringLiteral("listing config files");
    appendLog(QStringLiteral("backup started into %1").arg(m_targetDir));
    emit changed();
    Tools::onClient(m_client, [client = m_client] { client->fetchConfigNames(); });
}

void BackupTool::onConfigNames(const QStringList &names)
{
    if (!m_busy || names.isEmpty()) {
        m_busy = false;
        m_status = QStringLiteral("router reported no config files");
        appendLog(QStringLiteral("backup failed: no config files reported"));
        emit changed();
        return;
    }
    m_configNames = names;
    m_configIndex = 0;
    m_status = QStringLiteral("reading %1 config files").arg(names.size());
    emit changed();
    readNextConfig();
}

void BackupTool::readNextConfig()
{
    if (m_configIndex >= m_configNames.size()) {
        m_status = QStringLiteral("reading the package list");
        emit changed();
        Tools::onClient(m_client, [client = m_client] {
            client->execTagged(kManifestTag, QStringLiteral("/usr/bin/apk"),
                               {QStringLiteral("list"), QStringLiteral("--installed")});
        });
        return;
    }
    const QString name = m_configNames.at(m_configIndex);
    Tools::onClient(m_client, [client = m_client, name] {
        client->fileReadTagged(kBackupTag, QStringLiteral("/etc/config/") + name);
    });
}

void BackupTool::onRead(const QString &tag, const QString &path, const QByteArray &data)
{
    if (tag != kBackupTag)
        return;
    Archive::Member member;
    member.name = path.mid(1); // "/etc/config/x" -> "etc/config/x"
    member.data = data;
    member.modified = QDateTime::currentDateTimeUtc();
    m_members.append(member);
    ++m_configIndex;
    emit changed();
    readNextConfig();
}

void BackupTool::onExec(const QString &tag, const Ubus::ExecResult &result)
{
    if (tag == kMkdirTag) {
        if (!result.isOk()) {
            m_busy = false;
            m_status = QStringLiteral("cannot create %1: %2").arg(kStageRoot, result.stderrText);
            appendLog(m_status);
            emit changed();
            return;
        }
        writeNextStaged();
        return;
    }
    if (tag != kManifestTag)
        return;

    const QString packages = (result.stdoutText + result.stderrText).trimmed();
    const QStringList lines = packages.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    m_manifestSummary = QStringLiteral("%1 packages, %2 config files")
                            .arg(lines.size())
                            .arg(m_members.size());
    m_manifest = QStringLiteral("created=%1\nsource=router-console config backup\n"
                                "config_files=%2\npackages=%3\n\n%4\n")
                     .arg(QDateTime::currentDateTime().toString(Qt::ISODate))
                     .arg(m_members.size())
                     .arg(lines.size())
                     .arg(packages);
    finishBackup();
}

void BackupTool::finishBackup()
{
    QList<Archive::Member> members = m_members;
    Archive::Member manifest;
    manifest.name = QStringLiteral("MANIFEST.txt");
    manifest.data = m_manifest.toUtf8();
    manifest.modified = QDateTime::currentDateTimeUtc();
    members.append(manifest);

    const QByteArray blob = Archive::buildTarGz(members);
    if (blob.isEmpty()) {
        m_busy = false;
        m_status = QStringLiteral("archive build failed");
        appendLog(QStringLiteral("backup failed: archive build failed"));
        emit changed();
        return;
    }

    const QString fileName = QStringLiteral("router-console-backup-%1.tar.gz")
                                 .arg(QDateTime::currentDateTime().toString(
                                     QStringLiteral("yyyyMMdd-HHmmss")));
    if (!QDir().mkpath(m_targetDir)) {
        m_busy = false;
        m_status = QStringLiteral("cannot create %1").arg(m_targetDir);
        appendLog(m_status);
        emit changed();
        return;
    }

    const QString path = m_targetDir + QLatin1Char('/') + fileName;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(blob) != blob.size()) {
        m_busy = false;
        m_status = QStringLiteral("cannot write %1").arg(path);
        appendLog(m_status);
        emit changed();
        return;
    }
    file.close();

    m_lastArchive = path;
    m_archiveBytes = quint64(blob.size());
    m_backupFileCount = int(m_members.size());
    m_busy = false;
    m_status = QStringLiteral("wrote %1 (%2 files, %3 kB)")
                   .arg(fileName)
                   .arg(m_backupFileCount)
                   .arg(blob.size() / 1000.0, 0, 'f', 1);
    appendLog(QStringLiteral("archive %1: %2 config files + MANIFEST.txt, %3 B")
                  .arg(fileName)
                  .arg(m_backupFileCount)
                  .arg(blob.size()));
    emit changed();
}

QByteArray BackupTool::archiveMember(const QString &archivePath, const QString &name) const
{
    QFile file(archivePath);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    QString error;
    const QList<Archive::Member> members = Archive::parseTarGz(file.readAll(), &error);
    for (const Archive::Member &member : members) {
        if (member.name == name)
            return member.data;
    }
    return {};
}

void BackupTool::stageRestore(const QString &archivePath)
{
    if (m_busy)
        return;

    QFile file(archivePath.trimmed());
    if (!file.open(QIODevice::ReadOnly)) {
        m_status = QStringLiteral("cannot read %1").arg(archivePath);
        appendLog(m_status);
        emit changed();
        return;
    }

    QString error;
    const QList<Archive::Member> members = Archive::parseTarGz(file.readAll(), &error);
    file.close();
    if (members.isEmpty()) {
        m_status = QStringLiteral("archive unreadable: %1").arg(error);
        appendLog(m_status);
        emit changed();
        return;
    }

    m_staged.clear();
    for (const Archive::Member &member : members) {
        if (isConfigMember(member))
            m_staged.append(member);
        else
            appendLog(QStringLiteral("skipped %1 (not a config file)").arg(member.name));
    }
    if (m_staged.isEmpty()) {
        m_status = QStringLiteral("archive holds no /etc/config files");
        appendLog(m_status);
        emit changed();
        return;
    }

    m_archiveSource = archivePath;
    m_stageIndex = 0;
    m_restoreStaged = false;
    m_busy = true;
    m_status = QStringLiteral("staging %1 files").arg(m_staged.size());
    appendLog(QStringLiteral("restore staged from %1: %2 files")
                  .arg(QFileInfo(archivePath).fileName())
                  .arg(m_staged.size()));
    emit changed();
    // rpcd's file write does not create parent directories (measured:
    // "Not found"), so the staging tree is created before the first write.
    Tools::onClient(m_client, [client = m_client] {
        client->execTagged(kMkdirTag, QStringLiteral("/bin/mkdir"),
                           {QStringLiteral("-p"), kStageRoot + QStringLiteral("/etc/config")});
    });
}

void BackupTool::writeNextStaged()
{
    if (m_stageIndex >= m_staged.size()) {
        m_busy = false;
        m_restoreStaged = true;
        m_stagedSummary = QStringLiteral("%1 files staged in %2 (not applied)")
                              .arg(m_staged.size())
                              .arg(kStageRoot);
        m_status = m_stagedSummary;
        appendLog(QStringLiteral("staged, waiting for confirmRestore()"));
        emit changed();
        return;
    }
    const Archive::Member member = m_staged.at(m_stageIndex);
    Tools::onClient(m_client, [client = m_client, member] {
        client->fileWriteTagged(kStageTag, kStageRoot + QLatin1Char('/') + member.name,
                                member.data);
    });
}

void BackupTool::onWrite(const QString &tag, const QString &path, int bytes)
{
    if (tag == kStageTag) {
        appendLog(QStringLiteral("staged %1 (%2 B)").arg(path).arg(bytes));
        ++m_stageIndex;
        emit changed();
        writeNextStaged();
        return;
    }
    if (tag == kApplyTag) {
        appendLog(QStringLiteral("applied %1 (%2 B)").arg(path).arg(bytes));
        ++m_applyIndex;
        emit changed();
        applyNext();
    }
}

void BackupTool::confirmRestore()
{
    if (m_busy || !m_restoreStaged || m_staged.isEmpty())
        return;
    m_busy = true;
    m_applyIndex = 0;
    m_status = QStringLiteral("writing %1 files to /etc/config").arg(m_staged.size());
    appendLog(QStringLiteral("restore confirmed: writing to /etc/config"));
    emit changed();
    applyNext();
}

void BackupTool::applyNext()
{
    if (m_applyIndex >= m_staged.size()) {
        m_busy = false;
        m_restoreStaged = false;
        m_status = QStringLiteral("restored %1 config files from %2; services were not restarted")
                       .arg(m_staged.size())
                       .arg(QFileInfo(m_archiveSource).fileName());
        appendLog(QStringLiteral("restore complete (%1 files)").arg(m_staged.size()));
        emit changed();
        return;
    }
    const Archive::Member member = m_staged.at(m_applyIndex);
    Tools::onClient(m_client, [client = m_client, member] {
        client->fileWriteTagged(kApplyTag, QLatin1Char('/') + member.name, member.data);
    });
}

void BackupTool::discardRestore()
{
    if (m_busy)
        return;
    if (!m_staged.isEmpty())
        appendLog(QStringLiteral("staging discarded (%1 files left in %2)")
                      .arg(m_staged.size())
                      .arg(kStageRoot));
    m_staged.clear();
    m_restoreStaged = false;
    m_stagedSummary.clear();
    m_status = QStringLiteral("staging discarded");
    emit changed();
}

void BackupTool::onFailure(const Ubus::Error &error)
{
    m_busy = false;
    m_status = QStringLiteral("failed: %1").arg(error.message);
    appendLog(m_status);
    emit changed();
}

void BackupTool::reset()
{
    m_busy = false;
    m_restoreStaged = false;
    m_configNames.clear();
    m_members.clear();
    m_staged.clear();
    m_logLines.clear();
    m_lastArchive.clear();
    m_status.clear();
    m_manifestSummary.clear();
    m_stagedSummary.clear();
    m_backupFileCount = 0;
    m_archiveBytes = 0;
    m_configIndex = 0;
    m_stageIndex = 0;
    m_applyIndex = 0;
    emit changed();
}
