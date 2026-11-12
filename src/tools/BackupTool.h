// SPDX-FileCopyrightText: 2026 the router-console authors
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>

#include "protocol/ToolTypes.h"
#include "protocol/UbusTypes.h"
#include "tools/ArchiveCodec.h"

class UbusClient;

// Config backup window state. Backup reads every /etc/config file the router
// reports through `uci configs` plus the installed package list, and writes a
// .tar.gz on this machine. Restore never applies anything: it writes the
// archive into /tmp/router-console-restore first, lists what is staged, and
// only copies to /etc/config when confirmRestore() is called.
class BackupTool : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool busy READ isBusy NOTIFY changed)
    Q_PROPERTY(QString statusText READ statusText NOTIFY changed)
    Q_PROPERTY(QString lastArchive READ lastArchive NOTIFY changed)
    Q_PROPERTY(QString lastArchiveSize READ lastArchiveSize NOTIFY changed)
    Q_PROPERTY(int backupFileCount READ backupFileCount NOTIFY changed)
    Q_PROPERTY(QString manifestSummary READ manifestSummary NOTIFY changed)
    Q_PROPERTY(QVariantList stagedFiles READ stagedFiles NOTIFY changed)
    Q_PROPERTY(QVariantList logLines READ logLines NOTIFY changed)
    Q_PROPERTY(bool restoreStaged READ isRestoreStaged NOTIFY changed)
    Q_PROPERTY(QString stagedSummary READ stagedSummary NOTIFY changed)

public:
    explicit BackupTool(UbusClient *client, QObject *parent = nullptr);

    bool isBusy() const { return m_busy; }
    QString statusText() const { return m_status; }
    QString lastArchive() const { return m_lastArchive; }
    QString lastArchiveSize() const;
    int backupFileCount() const { return m_backupFileCount; }
    QString manifestSummary() const { return m_manifestSummary; }
    QVariantList stagedFiles() const;
    QVariantList logLines() const { return m_logLines; }
    bool isRestoreStaged() const { return m_restoreStaged; }
    QString stagedSummary() const { return m_stagedSummary; }

    Q_INVOKABLE void backup(const QString &localDir);
    Q_INVOKABLE void stageRestore(const QString &archivePath);
    Q_INVOKABLE void confirmRestore();
    Q_INVOKABLE void discardRestore();
    void reset();

    // Exposed for the integration test: one archive member by name.
    QByteArray archiveMember(const QString &archivePath, const QString &name) const;

signals:
    void changed();

private:
    void readNextConfig();
    void writeNextStaged();
    void applyNext();
    void onConfigNames(const QStringList &names);
    void onRead(const QString &tag, const QString &path, const QByteArray &data);
    void onWrite(const QString &tag, const QString &path, int bytes);
    void onExec(const QString &tag, const Ubus::ExecResult &result);
    void onFailure(const Ubus::Error &error);
    void finishBackup();
    void appendLog(const QString &line);

    UbusClient *m_client = nullptr;
    QString m_targetDir;
    QStringList m_configNames;
    QList<Archive::Member> m_members;
    QString m_manifest;
    qsizetype m_configIndex = 0;
    qsizetype m_stageIndex = 0;
    qsizetype m_applyIndex = 0;
    int m_backupFileCount = 0;
    quint64 m_archiveBytes = 0;
    QString m_lastArchive;
    QString m_status;
    QString m_manifestSummary;
    QString m_stagedSummary;
    QString m_archiveSource;
    QList<Archive::Member> m_staged;
    QVariantList m_logLines;
    bool m_busy = false;
    bool m_restoreStaged = false;
};
