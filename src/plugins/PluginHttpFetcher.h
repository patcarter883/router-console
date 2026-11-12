#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QObject>
#include <QUrl>

class QNetworkAccessManager;

namespace Plugin {

// HTTP side of the plugin contract, run in its own thread: a plugin may fetch a
// router file (modemaim's towers.json is a static /luci-static/ document) and
// the UI thread must never wait for it. TLS is pinned to the same SHA-256 the
// session login trusted; an unpinned peer is a failure, never an ignore.
class HttpFetcher : public QObject
{
    Q_OBJECT

public:
    explicit HttpFetcher(QObject *parent = nullptr);

public slots:
    void fetch(int requestId, const QUrl &url, const QByteArray &sha256Pin);

signals:
    void finished(int requestId, const QJsonObject &payload, const QString &error);

private:
    QNetworkAccessManager *m_nam = nullptr;
};

} // namespace Plugin
