#pragma once

#include <QMetaType>
#include <QString>

// Typed view of the ubus/rpcd surface (docs/ubus-rpcd-read-surface.md).
// Nothing above the protocol layer sees a JSON value: callers receive these
// structs and Ubus::Error only.
namespace Ubus {

// ubus status codes as returned in result[0] of a JSON-RPC call.
enum class Status {
    Ok = 0,
    InvalidCommand = 1,
    InvalidArgument = 2,
    MethodNotFound = 3,
    NotFound = 4,
    NoData = 5,
    PermissionDenied = 6,
    Timeout = 7,
    NotSupported = 8,
    UnknownError = 9,
    ConnectionFailed = 10,
    TransportFailure = -1, // request never produced a ubus answer (TLS, DNS, HTTP)
    CertificateRequired = -2, // awaiting the user's trust decision; not a failure
};

struct Session {
    QString token;
    int timeoutSeconds = 0;
    QString username;
    bool isValid() const { return !token.isEmpty(); }
};

struct BoardInfo {
    QString hostname;
    QString model;
    QString boardName;
    QString kernel;
    QString processor;
    QString rootfsType;
    QString distribution;
    QString version;
    QString revision;
    QString target;
    QString description;

    // "OpenWrt 25.12.6 r33311-981c978eff" when the release block is present.
    QString firmwareLabel() const
    {
        if (description.isEmpty())
            return QStringLiteral("%1 %2").arg(distribution, version).trimmed();
        return description;
    }
};

struct Error {
    Status status = Status::Ok;
    QString message;

    bool isOk() const { return status == Status::Ok && message.isEmpty(); }
};

} // namespace Ubus

Q_DECLARE_METATYPE(Ubus::Session)
Q_DECLARE_METATYPE(Ubus::BoardInfo)
Q_DECLARE_METATYPE(Ubus::Error)
