// SPDX-FileCopyrightText: 2026 the router-console authors
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "FilesTool.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMetaObject>

#include "ToolFormat.h"
#include "protocol/UbusClient.h"

namespace {

constexpr qint64 kMaxUploadBytes = 32 * 1024 * 1024;
constexpr int kPreviewBytes = 256 * 1024;

QString joinPath(const QString &base, const QString &name)
{
    if (name.startsWith(QLatin1Char('/')))
        return name;
    return base.endsWith(QLatin1Char('/')) ? base + name : base + QLatin1Char('/') + name;
}

} // namespace

FilesTool::FilesTool(UbusClient *client, QObject *parent)
    : QObject(parent), m_client(client)
{
    connect(m_client, &UbusClient::fileListingReceived, this, &FilesTool::onListing);
    connect(m_client, &UbusClient::fileReadFinished, this, &FilesTool::onRead);
    connect(m_client, &UbusClient::fileWriteFinished, this, &FilesTool::onWriteFinished);
    connect(m_client, &UbusClient::fileRemoveFinished, this, &FilesTool::onRemoved);
    connect(m_client, &UbusClient::sysupgradeChecked, this, &FilesTool::onSysupgradeChecked);
    connect(m_client, &UbusClient::packageInstallFinished, this, &FilesTool::onPackagesFinished);
    connect(m_client, &UbusClient::fileListingFailed, this, &FilesTool::onFailure);
    connect(m_client, &UbusClient::fileReadFailed, this, &FilesTool::onFailure);
    connect(m_client, &UbusClient::fileWriteFailed, this, &FilesTool::onFailure);
    connect(m_client, &UbusClient::fileRemoveFailed, this, &FilesTool::onFailure);
    connect(m_client, &UbusClient::sysupgradeCheckFailed, this, &FilesTool::onFailure);
    connect(m_client, &UbusClient::packageInstallFailed, this, &FilesTool::onFailure);
}

QString FilesTool::parentPath() const
{
    if (m_path == QLatin1String("/") || m_path.isEmpty())
        return {};
    const int slash = m_path.lastIndexOf(QLatin1Char('/'));
    return slash <= 0 ? QStringLiteral("/") : m_path.left(slash);
}

QVariantList FilesTool::entries() const
{
    QVariantList rows;
    rows.reserve(m_listing.size());
    for (const Ubus::FileEntry &entry : m_listing) {
        rows.append(QVariantMap{
            {QStringLiteral("name"), entry.name},
            {QStringLiteral("path"), entry.path},
            {QStringLiteral("type"), entry.type},
            {QStringLiteral("directory"), entry.isDirectory()},
            {QStringLiteral("size"), Tools::bytesText(entry.size)},
            {QStringLiteral("mode"), entry.mode},
            {QStringLiteral("owner"), QStringLiteral("%1:%2").arg(entry.owner, entry.group)},
            {QStringLiteral("modified"), entry.modified.toString(QStringLiteral("yyyy-MM-dd HH:mm"))}});
    }
    return rows;
}

void FilesTool::refresh()
{
    setBusy(true);
    const QString path = m_path;
    Tools::onClient(m_client, [client = m_client, path] { client->fileList(path); });
}

void FilesTool::navigate(const QString &path)
{
    m_path = path;
    emit changed();
    refresh();
}

void FilesTool::up()
{
    const QString parent = parentPath();
    if (!parent.isEmpty())
        navigate(parent);
}

void FilesTool::openFile(const QString &name)
{
    const QString path = joinPath(m_path, name);
    m_readPurpose = ReadPurpose::Preview;
    m_openPath = path;
    setBusy(true);
    Tools::onClient(m_client, [client = m_client, path] { client->fileRead(path); });
}

void FilesTool::saveOpenFile(const QString &text)
{
    if (m_openPath.isEmpty())
        return;
    setBusy(true);
    m_pendingWritePath = m_openPath;
    const QByteArray data = text.toUtf8();
    Tools::onClient(m_client, [client = m_client, path = m_openPath, data] {
        client->fileWrite(path, data);
    });
}

void FilesTool::upload(const QString &localPath, const QString &remoteName)
{
    QFile file(localPath);
    if (!file.open(QIODevice::ReadOnly)) {
        setStatus(QStringLiteral("cannot read %1: %2").arg(localPath, file.errorString()));
        return;
    }
    const qint64 size = file.size();
    if (size > kMaxUploadBytes) {
        setStatus(QStringLiteral("%1 is %2; the upload limit is %3")
                      .arg(QFileInfo(localPath).fileName(), Tools::bytesText(size),
                           Tools::bytesText(kMaxUploadBytes)));
        return;
    }
    const QByteArray data = file.readAll();
    const QString remote = joinPath(m_path, remoteName.isEmpty() ? QFileInfo(localPath).fileName()
                                                                : remoteName);
    setBusy(true);
    m_pendingWritePath = remote;
    Tools::onClient(m_client, [client = m_client, path = remote, data] {
        client->fileWrite(path, data);
    });
}

void FilesTool::download(const QString &name, const QString &localPath)
{
    const QString path = joinPath(m_path, name);
    m_readPurpose = ReadPurpose::Download;
    m_pendingDownloadPath = localPath;
    m_openPath = path;
    setBusy(true);
    Tools::onClient(m_client, [client = m_client, path] { client->fileRead(path); });
}

void FilesTool::remove(const QString &name)
{
    const QString path = joinPath(m_path, name);
    setBusy(true);
    Tools::onClient(m_client, [client = m_client, path] { client->fileRemove(path); });
}

void FilesTool::verifyImage()
{
    setBusy(true);
    m_imageStatus = QStringLiteral("checking /tmp/firmware.bin");
    emit changed();
    Tools::onClient(m_client, [client = m_client] { client->sysupgradeTest(); });
}

void FilesTool::flashImage()
{
    if (!m_imageVerified) {
        m_imageStatus = QStringLiteral("image not verified; run the check first");
        emit changed();
        return;
    }
    Tools::onClient(m_client, [client = m_client] { client->sysupgradeStart(); });
}

void FilesTool::installPackages(const QString &packages)
{
    const QStringList names = packages.split(QRegularExpression(QStringLiteral("[\\s,]+")),
                                             Qt::SkipEmptyParts);
    if (names.isEmpty()) {
        setStatus(QStringLiteral("no package names"));
        return;
    }
    setBusy(true);
    setStatus(QStringLiteral("installing %1").arg(names.join(QLatin1Char(' '))));
    Tools::onClient(m_client, [client = m_client, names] { client->packageInstall(names); });
}

void FilesTool::reset()
{
    m_listing.clear();
    m_status.clear();
    m_openPath.clear();
    m_contentText.clear();
    m_busy = false;
    emit changed();
}

void FilesTool::onListing(const Ubus::FileListing &listing)
{
    m_busy = false;
    m_path = listing.path;
    m_listing = listing.entries;
    setStatus(QStringLiteral("%1 entries").arg(listing.entries.size()));
    emit changed();
}

void FilesTool::onRead(const QString &path, const QByteArray &data)
{
    m_busy = false;
    m_imageVerified = false;

    if (m_readPurpose == ReadPurpose::Download) {
        QFile file(m_pendingDownloadPath);
        if (!file.open(QIODevice::WriteOnly)) {
            setStatus(QStringLiteral("cannot write %1: %2").arg(m_pendingDownloadPath,
                                                                file.errorString()));
            emit changed();
            return;
        }
        file.write(data);
        file.close();
        setStatus(QStringLiteral("downloaded %1 (%2)").arg(path, Tools::bytesText(data.size())));
        emit changed();
        return;
    }

    const QByteArray trimmed = data.left(kPreviewBytes);
    m_contentText = QString::fromUtf8(trimmed);
    setStatus(QStringLiteral("loaded %1 (%2)").arg(path, Tools::bytesText(data.size())));
    emit changed();
}

void FilesTool::onWriteFinished(const QString &path, int bytes)
{
    m_busy = false;
    if (path == QLatin1String("/tmp/firmware.bin")) {
        m_imageVerified = false;
        m_imageStatus = QStringLiteral("image uploaded (%1); check it before flashing")
                            .arg(Tools::bytesText(static_cast<quint64>(bytes)));
    }
    setStatus(QStringLiteral("wrote %1 (%2)").arg(path, Tools::bytesText(static_cast<quint64>(bytes))));
    refresh();
}

void FilesTool::onRemoved(const QString &path)
{
    m_busy = false;
    if (path == m_openPath) {
        m_openPath.clear();
        m_contentText.clear();
    }
    setStatus(QStringLiteral("removed %1").arg(path));
    refresh();
}

void FilesTool::onSysupgradeChecked(const Ubus::SysupgradeCheck &check)
{
    m_busy = false;
    m_imageVerified = check.accepted;
    m_imageStatus = check.accepted
                        ? QStringLiteral("image accepted by sysupgrade -T")
                        : QStringLiteral("rejected (code %1): %2").arg(check.code).arg(check.output);
    emit changed();
}

void FilesTool::onPackagesFinished(const Ubus::ExecResult &result)
{
    m_busy = false;
    const QString detail = (result.stdoutText + result.stderrText).trimmed();
    setStatus(result.isOk() ? QStringLiteral("packages installed: %1").arg(detail)
                            : QStringLiteral("apk failed (code %1): %2").arg(result.code).arg(detail));
    emit changed();
}

void FilesTool::onFailure(const Ubus::Error &error)
{
    m_busy = false;
    setStatus(QStringLiteral("failed: %1").arg(error.message));
    emit changed();
}

void FilesTool::setStatus(const QString &status)
{
    m_status = status;
    emit changed();
}

void FilesTool::setBusy(bool busy)
{
    m_busy = busy;
    emit changed();
}
