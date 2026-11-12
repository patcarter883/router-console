#pragma once

#include <QByteArray>
#include <QElapsedTimer>
#include <QJsonObject>
#include <QObject>
#include <QUrl>

#include <functional>

#include "UbusTypes.h"

class QNetworkAccessManager;
class QNetworkReply;

// ubus over HTTPS JSON-RPC (uhttpd `/ubus`). One instance per router; it must
// live in a worker thread — every request is a QUEUED signal/slot pair from the
// UI thread, and QNetworkAccessManager is never touched from the UI thread.
//
// The router serves a self-signed certificate, so the peer certificate is
// pinned by SHA-256: an unpinned host emits certificateRequired() and aborts;
// a pin mismatch is a hard failure. session tokens expire after the ACL
// `timeout` (300 s by default), so an expired or rejected session re-logins once
// and retries before reporting an error.
class UbusClient : public QObject
{
    Q_OBJECT

public:
    explicit UbusClient(QObject *parent = nullptr);

    void setPinnedFingerprint(const QByteArray &sha256Hex) { m_pin = sha256Hex; }
    QByteArray pinnedFingerprint() const { return m_pin; }

public slots:
    void login(const QUrl &endpoint, const QString &username, const QString &password);
    void fetchBoard();
    void closeSession();

signals:
    void loggedIn(const Ubus::Session &session);
    void loginFailed(const Ubus::Error &error);
    void boardReceived(const Ubus::BoardInfo &board);
    void boardFailed(const Ubus::Error &error);
    void certificateRequired(const QString &host, const QByteArray &sha256Hex);
    void certificateRejected(const QString &reason);

private:
    using ResultHandler = std::function<void(const QJsonObject &result, const Ubus::Error &error)>;

    void call(const QString &object, const QString &method, const QJsonObject &args,
              ResultHandler handler, bool allowRelogin = true);
    void callRaw(const QString &sessionToken, const QString &object, const QString &method,
                 const QJsonObject &args, ResultHandler handler);
    void post(const QByteArray &body, ResultHandler handler);
    void queueAfterLogin(const QString &object, const QString &method, const QJsonObject &args,
                         ResultHandler handler, bool allowRelogin);
    void sessionExpired();

    QNetworkAccessManager *m_nam = nullptr;
    QUrl m_endpoint;
    QString m_username;
    QString m_password;
    Ubus::Session m_session;
    QElapsedTimer m_sessionAge;
    QByteArray m_pin;
    std::function<void()> m_afterLogin;
    int m_requestId = 0;
    bool m_loggingIn = false;
};
