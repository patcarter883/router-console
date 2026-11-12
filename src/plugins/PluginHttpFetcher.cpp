#include "PluginHttpFetcher.h"

#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSslCertificate>
#include <QSslConfiguration>
#include <QSslError>

namespace Plugin {

HttpFetcher::HttpFetcher(QObject *parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
{
}

void HttpFetcher::fetch(int requestId, const QUrl &url, const QByteArray &sha256Pin)
{
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute,
                         QNetworkRequest::AlwaysNetwork);
    QNetworkReply *reply = m_nam->get(request);

    connect(reply, &QNetworkReply::sslErrors, this, [reply, sha256Pin, requestId, this] {
        const QSslCertificate certificate = reply->sslConfiguration().peerCertificate();
        const QByteArray fingerprint =
            certificate.isNull()
                ? QByteArray()
                : QCryptographicHash::hash(certificate.toDer(), QCryptographicHash::Sha256).toHex();
        if (!fingerprint.isEmpty() && !sha256Pin.isEmpty() && fingerprint == sha256Pin) {
            reply->ignoreSslErrors();
            return;
        }
        emit finished(requestId, {},
                      QStringLiteral("certificate for %1 is not the pinned one")
                          .arg(reply->url().host()));
        reply->abort();
    });

    connect(reply, &QNetworkReply::finished, this, [reply, requestId, this] {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() != QNetworkReply::NoError) {
            const QString reason = reply->errorString();
            reply->deleteLater();
            emit finished(requestId, {}, reason);
            return;
        }
        if (status < 200 || status >= 300) {
            reply->deleteLater();
            emit finished(requestId, {}, QStringLiteral("HTTP %1").arg(status));
            return;
        }

        const QByteArray body = reply->readAll();
        reply->deleteLater();

        QJsonParseError parseError{};
        const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            emit finished(requestId, {},
                          QStringLiteral("not a JSON object: %1").arg(parseError.errorString()));
            return;
        }
        emit finished(requestId, document.object(), {});
    });
}

} // namespace Plugin
