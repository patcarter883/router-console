#include "UbusClient.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSslCertificate>
#include <QSslConfiguration>
#include <QSslError>
#include <QThread>

#include <memory>

namespace {

constexpr const char *kAnonymousSession = "00000000000000000000000000000000";

Ubus::Status statusFromCode(int code)
{
    switch (code) {
    case 0: return Ubus::Status::Ok;
    case 1: return Ubus::Status::InvalidCommand;
    case 2: return Ubus::Status::InvalidArgument;
    case 3: return Ubus::Status::MethodNotFound;
    case 4: return Ubus::Status::NotFound;
    case 5: return Ubus::Status::NoData;
    case 6: return Ubus::Status::PermissionDenied;
    case 7: return Ubus::Status::Timeout;
    case 8: return Ubus::Status::NotSupported;
    case 9: return Ubus::Status::UnknownError;
    case 10: return Ubus::Status::ConnectionFailed;
    default: return Ubus::Status::UnknownError;
    }
}

QString statusName(Ubus::Status status)
{
    switch (status) {
    case Ubus::Status::Ok: return QStringLiteral("ok");
    case Ubus::Status::InvalidCommand: return QStringLiteral("invalid command");
    case Ubus::Status::InvalidArgument: return QStringLiteral("invalid argument");
    case Ubus::Status::MethodNotFound: return QStringLiteral("method not found");
    case Ubus::Status::NotFound: return QStringLiteral("not found");
    case Ubus::Status::NoData: return QStringLiteral("no data");
    case Ubus::Status::PermissionDenied: return QStringLiteral("permission denied");
    case Ubus::Status::Timeout: return QStringLiteral("timeout");
    case Ubus::Status::NotSupported: return QStringLiteral("not supported");
    case Ubus::Status::ConnectionFailed: return QStringLiteral("connection failed");
    case Ubus::Status::TransportFailure: return QStringLiteral("transport failure");
    case Ubus::Status::UnknownError: break;
    }
    return QStringLiteral("unknown error");
}

// Unwraps the JSON-RPC envelope into the ubus result object plus a typed error.
Ubus::Error parseEnvelope(const QByteArray &payload, QJsonObject &result)
{
    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError)
        return {Ubus::Status::TransportFailure,
                QStringLiteral("malformed JSON from /ubus: %1").arg(parseError.errorString())};

    const QJsonObject root = doc.object();
    if (const QJsonObject rpcError = root.value(QStringLiteral("error")).toObject();
        !rpcError.isEmpty()) {
        const QString message = rpcError.value(QStringLiteral("message")).toString();
        return {Ubus::Status::TransportFailure,
                message.isEmpty() ? QStringLiteral("/ubus rejected the request") : message};
    }

    const QJsonArray resultArray = root.value(QStringLiteral("result")).toArray();
    if (resultArray.isEmpty())
        return {Ubus::Status::TransportFailure, QStringLiteral("ubus reply carried no result")};

    // A successful call answers [0, {...}]; a rejected one may answer [6] with
    // no data object at all.
    const int code = resultArray.at(0).toInt(-1);
    result = resultArray.size() > 1 ? resultArray.at(1).toObject() : QJsonObject{};
    if (code == 0)
        return {};

    const Ubus::Status status = statusFromCode(code);
    const QString detail = result.value(QStringLiteral("status")).toString();
    return {status, detail.isEmpty() ? statusName(status)
                                     : QStringLiteral("%1 (%2)").arg(statusName(status), detail)};
}

} // namespace

UbusClient::UbusClient(QObject *parent)
    : QObject(parent), m_nam(new QNetworkAccessManager(this))
{
}

void UbusClient::login(const QUrl &endpoint, const QString &username, const QString &password)
{
    m_endpoint = endpoint;
    m_username = username;
    m_password = password;
    m_session = {};
    m_afterLogin = nullptr;
    m_loggingIn = true;

    const QJsonObject args{{QStringLiteral("username"), username},
                           {QStringLiteral("password"), password}};
    callRaw(QLatin1String(kAnonymousSession), QStringLiteral("session"), QStringLiteral("login"),
            args, [this](const QJsonObject &result, const Ubus::Error &error) {
                m_loggingIn = false;
                if (!error.isOk()) {
                    m_afterLogin = nullptr;
                    emit loginFailed(error);
                    return;
                }

                Ubus::Session session;
                session.token = result.value(QStringLiteral("ubus_rpc_session")).toString();
                session.timeoutSeconds = result.value(QStringLiteral("timeout")).toInt();
                session.username =
                    result.value(QStringLiteral("data")).toObject().value(QStringLiteral("username")).toString();
                if (!session.isValid()) {
                    m_afterLogin = nullptr;
                    emit loginFailed({Ubus::Status::UnknownError,
                                      QStringLiteral("login reply carried no session token")});
                    return;
                }

                m_session = session;
                m_sessionAge.start();
                emit loggedIn(session);

                if (m_afterLogin) {
                    const auto next = std::move(m_afterLogin);
                    m_afterLogin = nullptr;
                    next();
                }
            });
}

void UbusClient::fetchBoard()
{
    call(QStringLiteral("system"), QStringLiteral("board"), {},
         [this](const QJsonObject &result, const Ubus::Error &error) {
             if (!error.isOk()) {
                 emit boardFailed(error);
                 return;
             }

             Ubus::BoardInfo board;
             board.hostname = result.value(QStringLiteral("hostname")).toString();
             board.model = result.value(QStringLiteral("model")).toString();
             board.boardName = result.value(QStringLiteral("board_name")).toString();
             board.kernel = result.value(QStringLiteral("kernel")).toString();
             board.processor = result.value(QStringLiteral("system")).toString();
             board.rootfsType = result.value(QStringLiteral("rootfs_type")).toString();

             const QJsonObject release = result.value(QStringLiteral("release")).toObject();
             board.distribution = release.value(QStringLiteral("distribution")).toString();
             board.version = release.value(QStringLiteral("version")).toString();
             board.revision = release.value(QStringLiteral("revision")).toString();
             board.target = release.value(QStringLiteral("target")).toString();
             board.description = release.value(QStringLiteral("description")).toString();

             emit boardReceived(board);
         });
}

void UbusClient::closeSession()
{
    m_session = {};
    m_afterLogin = nullptr;
}

void UbusClient::sessionExpired()
{
    m_session = {};
}

void UbusClient::call(const QString &object, const QString &method, const QJsonObject &args,
                      ResultHandler handler, bool allowRelogin)
{
    if (m_endpoint.isEmpty() || m_password.isNull()) {
        handler({}, {Ubus::Status::ConnectionFailed, QStringLiteral("no session established")});
        return;
    }

    // Drop a session that is within the ACL expiry window instead of waiting for
    // the router to reject it.
    if (m_session.isValid() && m_session.timeoutSeconds > 0
        && m_sessionAge.elapsed() / 1000 >= m_session.timeoutSeconds - 15) {
        m_session = {};
    }

    if (!m_session.isValid()) {
        if (!allowRelogin) {
            handler({}, {Ubus::Status::PermissionDenied,
                         QStringLiteral("session rejected by router")});
            return;
        }
        queueAfterLogin(object, method, args, handler, false);
        return;
    }

    callRaw(m_session.token, object, method, args,
            [this, object, method, args, handler, allowRelogin](const QJsonObject &result,
                                                               const Ubus::Error &error) {
                if (error.status == Ubus::Status::PermissionDenied && allowRelogin) {
                    sessionExpired();
                    queueAfterLogin(object, method, args, handler, false);
                    return;
                }
                handler(result, error);
            });
}

void UbusClient::queueAfterLogin(const QString &object, const QString &method,
                                 const QJsonObject &args, ResultHandler handler,
                                 bool allowRelogin)
{
    m_afterLogin = [this, object, method, args, handler, allowRelogin] {
        call(object, method, args, handler, allowRelogin);
    };
    if (!m_loggingIn)
        login(m_endpoint, m_username, m_password);
}

void UbusClient::callRaw(const QString &sessionToken, const QString &object,
                         const QString &method, const QJsonObject &args, ResultHandler handler)
{
    QJsonObject params;
    params.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
    params.insert(QStringLiteral("id"), ++m_requestId);
    params.insert(QStringLiteral("method"), QStringLiteral("call"));
    params.insert(QStringLiteral("params"),
                  QJsonArray{sessionToken, object, method, args});

    post(QJsonDocument(params).toJson(QJsonDocument::Compact), std::move(handler));
}

void UbusClient::post(const QByteArray &body, ResultHandler handler)
{
    if (QThread::currentThread() != thread()) {
        handler({}, {Ubus::Status::TransportFailure,
                     QStringLiteral("ubus request issued from outside the protocol thread")});
        return;
    }

    QNetworkRequest request(m_endpoint);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Accept", "application/json");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::ManualRedirectPolicy);

    QNetworkReply *reply = m_nam->post(request, body);

    auto answered = std::make_shared<bool>(false);
    auto finish = [answered, handler](const QJsonObject &result, const Ubus::Error &error) {
        if (*answered)
            return;
        *answered = true;
        handler(result, error);
    };

    connect(reply, &QNetworkReply::sslErrors, this,
            [this, reply, finish](const QList<QSslError> &) {
                const QSslCertificate certificate = reply->sslConfiguration().peerCertificate();
                const QByteArray fingerprint =
                    certificate.isNull() ? QByteArray()
                                         : QCryptographicHash::hash(certificate.toDer(),
                                                                    QCryptographicHash::Sha256)
                                               .toHex();

                if (!fingerprint.isEmpty() && fingerprint == m_pin) {
                    reply->ignoreSslErrors();
                    return;
                }

                if (!fingerprint.isEmpty() && !m_pin.isEmpty()) {
                    emit certificateRejected(
                        QStringLiteral("certificate for %1 changed (pinned %2, presented %3)")
                            .arg(reply->url().host().toHtmlEscaped(),
                                 QString::fromLatin1(m_pin), QString::fromLatin1(fingerprint)));
                    finish({}, {Ubus::Status::TransportFailure,
                                QStringLiteral("router certificate changed")});
                    reply->abort();
                    return;
                }

                // Answer before aborting: abort() emits finished() synchronously
                // and would otherwise report "Operation canceled" as the error.
                emit certificateRequired(reply->url().host(), fingerprint);
                finish({}, {Ubus::Status::CertificateRequired,
                            QStringLiteral("router certificate is not trusted")});
                reply->abort();
            });

    connect(reply, &QNetworkReply::finished, this, [reply, finish] {
        if (reply->error() != QNetworkReply::NoError) {
            const QString reason = reply->errorString();
            reply->deleteLater();
            finish({}, {Ubus::Status::TransportFailure, reason});
            return;
        }

        const QByteArray payload = reply->readAll();
        reply->deleteLater();

        QJsonObject result;
        const Ubus::Error error = parseEnvelope(payload, result);
        finish(result, error);
    });
}
