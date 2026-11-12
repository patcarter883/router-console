// SPDX-FileCopyrightText: 2026 the router-console authors
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>

#include "protocol/ToolTypes.h"
#include "protocol/UbusTypes.h"

class UbusClient;

// File manager window state: browse/list, read into a text editor, save, upload
// a local file, remove, plus the sysupgrade image check and apk package install
// that live next to it.
class FilesTool : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString path READ path NOTIFY changed)
    Q_PROPERTY(QString parentPath READ parentPath NOTIFY changed)
    Q_PROPERTY(QVariantList entries READ entries NOTIFY changed)
    Q_PROPERTY(bool busy READ isBusy NOTIFY changed)
    Q_PROPERTY(QString statusText READ statusText NOTIFY changed)
    Q_PROPERTY(QString openPath READ openPath NOTIFY changed)
    Q_PROPERTY(QString contentText READ contentText NOTIFY changed)
    Q_PROPERTY(QString imageStatus READ imageStatus NOTIFY changed)
    Q_PROPERTY(bool imageVerified READ imageVerified NOTIFY changed)

public:
    explicit FilesTool(UbusClient *client, QObject *parent = nullptr);

    QString path() const { return m_path; }
    QString parentPath() const;
    QVariantList entries() const;
    bool isBusy() const { return m_busy; }
    QString statusText() const { return m_status; }
    QString openPath() const { return m_openPath; }
    QString contentText() const { return m_contentText; }
    QString imageStatus() const { return m_imageStatus; }
    bool imageVerified() const { return m_imageVerified; }

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void navigate(const QString &path);
    Q_INVOKABLE void up();
    Q_INVOKABLE void openFile(const QString &name);
    Q_INVOKABLE void saveOpenFile(const QString &text);
    Q_INVOKABLE void upload(const QString &localPath, const QString &remoteName);
    Q_INVOKABLE void download(const QString &name, const QString &localPath);
    Q_INVOKABLE void remove(const QString &name);
    Q_INVOKABLE void verifyImage();
    Q_INVOKABLE void flashImage();
    Q_INVOKABLE void installPackages(const QString &packages);
    void reset();

signals:
    void changed();

private:
    enum class ReadPurpose { Preview, Download };

    void onListing(const Ubus::FileListing &listing);
    void onRead(const QString &path, const QByteArray &data);
    void onWriteFinished(const QString &path, int bytes);
    void onRemoved(const QString &path);
    void onSysupgradeChecked(const Ubus::SysupgradeCheck &check);
    void onPackagesFinished(const Ubus::ExecResult &result);
    void onFailure(const Ubus::Error &error);
    void setStatus(const QString &status);
    void setBusy(bool busy);

    UbusClient *m_client = nullptr;
    QString m_path = QStringLiteral("/etc/config");
    QList<Ubus::FileEntry> m_listing;
    bool m_busy = false;
    QString m_status;
    QString m_openPath;
    QString m_contentText;
    QString m_imageStatus;
    bool m_imageVerified = false;
    ReadPurpose m_readPurpose = ReadPurpose::Preview;
    QString m_pendingDownloadPath;
    QString m_pendingWritePath;
};
