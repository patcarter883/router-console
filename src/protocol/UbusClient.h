#pragma once

#include <QByteArray>
#include <QElapsedTimer>
#include <QJsonObject>
#include <QList>
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
    void fetchInterfaces();
    void fetchDeviceStatus();
    void closeSession();

    // UCI staged editing. Reads are per section type (rpcd has no get_all);
    // writes stage, then uciApplyStaged commits set* -> apply -> confirm.
    void uciGetConfig(const QString &config, const QString &sectionType);
    void uciGetOption(const QString &config, const QString &section, const QString &option);
    void uciChanges(const QString &config);
    void uciSet(const Ubus::UciSetRequest &request);
    void uciApplyStaged(const QString &config, const QList<Ubus::UciSetRequest> &requests,
                        int rollbackTimeoutSeconds);
    void uciRevert(const QString &config);
    void systemReboot();

signals:
    void loggedIn(const Ubus::Session &session);
    void loginFailed(const Ubus::Error &error);
    void boardReceived(const Ubus::BoardInfo &board);
    void boardFailed(const Ubus::Error &error);
    void interfacesReceived(const QList<Ubus::InterfaceInfo> &interfaces);
    void interfacesFailed(const Ubus::Error &error);
    void deviceStatusReceived(const Ubus::DeviceStatusSnapshot &snapshot);
    void deviceStatusFailed(const Ubus::Error &error);
    void certificateRequired(const QString &host, const QByteArray &sha256Hex);
    void certificateRejected(const QString &reason);

    void uciConfigReceived(const Ubus::UciConfig &config);
    void uciConfigFailed(const Ubus::Error &error);
    void uciOptionReceived(const QString &config, const QString &section, const QString &option,
                           const QString &value);
    void uciOptionFailed(const Ubus::Error &error);
    void uciChangesReceived(const Ubus::UciChangeSet &changes);
    void uciChangesFailed(const Ubus::Error &error);
    void uciStaged(const Ubus::UciSetRequest &request);
    void uciStageFailed(const Ubus::Error &error);
    void uciApplied(int changeCount);
    void uciApplyFailed(const Ubus::Error &error);
    void uciReverted(const QString &config);
    void uciRevertFailed(const Ubus::Error &error);
    void rebootRequested();
    void rebootFailed(const Ubus::Error &error);

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
