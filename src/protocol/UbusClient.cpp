#include "UbusClient.h"

#include "ToolCodec.h"
#include "UciCodec.h"

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

namespace UciCodec = Ubus::UciCodec;
namespace ToolCodec = Ubus::ToolCodec;

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

QString formatAddress(const QJsonObject &address)
{
    const QString ip = address.value(QStringLiteral("address")).toString();
    const int mask = address.value(QStringLiteral("mask")).toInt(-1);
    if (ip.isEmpty())
        return {};
    return mask >= 0 ? QStringLiteral("%1/%2").arg(ip).arg(mask) : ip;
}

QStringList addressList(const QJsonObject &device, const QString &key)
{
    QStringList addresses;
    for (const QJsonValue &value : device.value(key).toArray()) {
        const QString address = formatAddress(value.toObject());
        if (!address.isEmpty())
            addresses.append(address);
    }
    return addresses;
}

// statistics is a flat map of kernel counters; missing keys stay zero.
Ubus::DeviceCounters parseCounters(const QJsonObject &statistics)
{
    auto counter = [&statistics](const char *key) {
        return static_cast<quint64>(statistics.value(QLatin1String(key)).toDouble());
    };
    Ubus::DeviceCounters counters;
    counters.rxBytes = counter("rx_bytes");
    counters.txBytes = counter("tx_bytes");
    counters.rxPackets = counter("rx_packets");
    counters.txPackets = counter("tx_packets");
    counters.rxErrors = counter("rx_errors");
    counters.txErrors = counter("tx_errors");
    counters.rxDropped = counter("rx_dropped");
    counters.txDropped = counter("tx_dropped");
    return counters;
}

} // namespace

UbusClient::UbusClient(QObject *parent)
    : QObject(parent), m_nam(new QNetworkAccessManager(this))
{
    qRegisterMetaType<Ubus::PingReport>();
    qRegisterMetaType<Ubus::TracerouteReport>();
    qRegisterMetaType<Ubus::PortScanReport>();
    qRegisterMetaType<Ubus::QdiscStats>();
    qRegisterMetaType<QList<Ubus::QdiscStats>>();
    qRegisterMetaType<Ubus::ConntrackFlow>();
    qRegisterMetaType<QList<Ubus::ConntrackFlow>>();
    qRegisterMetaType<Ubus::FileListing>();
    qRegisterMetaType<Ubus::SysupgradeCheck>();
    qRegisterMetaType<Ubus::ExecResult>();
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

void UbusClient::fetchInterfaces()
{
    call(QStringLiteral("network.interface"), QStringLiteral("dump"), {},
         [this](const QJsonObject &result, const Ubus::Error &error) {
             if (!error.isOk()) {
                 emit interfacesFailed(error);
                 return;
             }

             QList<Ubus::InterfaceInfo> interfaces;
             for (const QJsonValue &value : result.value(QStringLiteral("interface")).toArray()) {
                 const QJsonObject entry = value.toObject();
                 Ubus::InterfaceInfo info;
                 info.name = entry.value(QStringLiteral("interface")).toString();
                 if (info.name.isEmpty())
                     continue;
                 info.device = entry.value(QStringLiteral("device")).toString();
                 info.l3Device = entry.value(QStringLiteral("l3_device")).toString();
                 info.protocol = entry.value(QStringLiteral("proto")).toString();
                 info.up = entry.value(QStringLiteral("up")).toBool();
                 info.pending = entry.value(QStringLiteral("pending")).toBool();
                 info.available = entry.value(QStringLiteral("available")).toBool();
                 info.autostart = entry.value(QStringLiteral("autostart")).toBool();
                 info.dynamic = entry.value(QStringLiteral("dynamic")).toBool();
                 info.uptimeSeconds =
                     static_cast<quint64>(entry.value(QStringLiteral("uptime")).toDouble());
                 info.ipv4Addresses = addressList(entry, QStringLiteral("ipv4-address"));
                 info.ipv6Addresses = addressList(entry, QStringLiteral("ipv6-address"));
                 interfaces.append(info);
             }

             emit interfacesReceived(interfaces);
         });
}

void UbusClient::fetchDeviceStatus()
{
    call(QStringLiteral("network.device"), QStringLiteral("status"), {},
         [this](const QJsonObject &result, const Ubus::Error &error) {
             if (!error.isOk()) {
                 emit deviceStatusFailed(error);
                 return;
             }

             // The result is an object keyed by device name, not an array.
             Ubus::DeviceStatusSnapshot snapshot;
             for (auto it = result.begin(); it != result.end(); ++it) {
                 if (!it.value().isObject())
                     continue;
                 const QJsonObject entry = it.value().toObject();
                 Ubus::DeviceStatus device;
                 device.name = it.key();
                 device.type = entry.value(QStringLiteral("type")).toString();
                 device.deviceType = entry.value(QStringLiteral("devtype")).toString();
                 device.macAddress = entry.value(QStringLiteral("macaddr")).toString();
                 device.mtu = entry.value(QStringLiteral("mtu")).toInt();
                 device.present = entry.value(QStringLiteral("present")).toBool();
                 device.up = entry.value(QStringLiteral("up")).toBool();
                 device.carrier = entry.value(QStringLiteral("carrier")).toBool();
                 device.counters = parseCounters(entry.value(QStringLiteral("statistics")).toObject());
                 snapshot.devices.append(device);
             }

             emit deviceStatusReceived(snapshot);
         });
}

void UbusClient::uciGetConfig(const QString &config, const QString &sectionType)
{
    call(QStringLiteral("uci"), QStringLiteral("get"),
         UciCodec::getTypeArgs(config, sectionType),
         [this, config](const QJsonObject &result, const Ubus::Error &error) {
             if (!error.isOk()) {
                 emit uciConfigFailed(error);
                 return;
             }
             emit uciConfigReceived(UciCodec::parseConfig(config, result));
         });
}

void UbusClient::uciGetOption(const QString &config, const QString &section, const QString &option)
{
    call(QStringLiteral("uci"), QStringLiteral("get"),
         UciCodec::getOptionArgs(config, section, option),
         [this, config, section, option](const QJsonObject &result, const Ubus::Error &error) {
             if (!error.isOk()) {
                 emit uciOptionFailed(error);
                 return;
             }
             emit uciOptionReceived(config, section, option, UciCodec::parseOptionValue(result));
         });
}

void UbusClient::uciChanges(const QString &config)
{
    call(QStringLiteral("uci"), QStringLiteral("changes"), UciCodec::changesArgs(config),
         [this, config](const QJsonObject &result, const Ubus::Error &error) {
             if (!error.isOk()) {
                 emit uciChangesFailed(error);
                 return;
             }
             emit uciChangesReceived(UciCodec::parseChanges(config, result));
         });
}

void UbusClient::uciSet(const Ubus::UciSetRequest &request)
{
    call(QStringLiteral("uci"), QStringLiteral("set"), UciCodec::setArgs(request),
         [this, request](const QJsonObject &, const Ubus::Error &error) {
             if (!error.isOk()) {
                 emit uciStageFailed(error);
                 return;
             }
             emit uciStaged(request);
         });
}

// set* -> apply -> confirm, serialised so the router never sees a commit with a
// partially written change set. A failure leaves the staged changes in place:
// the editor reports it and the operator can revert.
void UbusClient::uciApplyStaged(const QString &config,
                                const QList<Ubus::UciSetRequest> &requests,
                                int rollbackTimeoutSeconds)
{
    if (requests.isEmpty()) {
        emit uciApplyFailed({Ubus::Status::InvalidArgument,
                             QStringLiteral("no staged changes to apply")});
        return;
    }

    auto step = std::make_shared<int>(0);
    auto advance = std::make_shared<std::function<void()>>();

    *advance = [this, config, requests, rollbackTimeoutSeconds, step, advance]() {
        if (*step < requests.size()) {
            const Ubus::UciSetRequest request = requests.at(*step);
            ++(*step);
            call(QStringLiteral("uci"), QStringLiteral("set"), UciCodec::setArgs(request),
                 [this, request, advance](const QJsonObject &, const Ubus::Error &error) {
                     if (!error.isOk()) {
                         emit uciApplyFailed(error);
                         return;
                     }
                     emit uciStaged(request);
                     (*advance)();
                 });
            return;
        }

        call(QStringLiteral("uci"), QStringLiteral("apply"),
             UciCodec::applyArgs(rollbackTimeoutSeconds),
             [this, requests](const QJsonObject &, const Ubus::Error &error) {
                 if (!error.isOk()) {
                     emit uciApplyFailed(error);
                     return;
                 }
                 call(QStringLiteral("uci"), QStringLiteral("confirm"), {},
                      [this, requests](const QJsonObject &, const Ubus::Error &error) {
                          if (!error.isOk()) {
                              emit uciApplyFailed(error);
                              return;
                          }
                          emit uciApplied(int(requests.size()));
                      });
             });
    };

    (*advance)();
}

void UbusClient::uciRevert(const QString &config)
{
    call(QStringLiteral("uci"), QStringLiteral("revert"), UciCodec::revertArgs(config),
         [this, config](const QJsonObject &, const Ubus::Error &error) {
             if (!error.isOk()) {
                 emit uciRevertFailed(error);
                 return;
             }
             emit uciReverted(config);
         });
}

void UbusClient::systemReboot()
{
    call(QStringLiteral("system"), QStringLiteral("reboot"), {},
         [this](const QJsonObject &, const Ubus::Error &error) {
             if (!error.isOk()) {
                 emit rebootFailed(error);
                 return;
             }
             emit rebootRequested();
         });
}

// --- Tool tier ---------------------------------------------------------------

void UbusClient::ping(const QString &target, int count)
{
    if (!ToolCodec::isSafeTarget(target)) {
        emit pingFailed({Ubus::Status::InvalidArgument,
                         QStringLiteral("invalid ping target: %1").arg(target)});
        return;
    }
    const int packets = qBound(1, count, 20);
    const QStringList params{QStringLiteral("-c"), QString::number(packets),
                             QStringLiteral("-W"), QStringLiteral("1"),
                             QStringLiteral("-i"), QStringLiteral("1"), target};
    call(QStringLiteral("file"), QStringLiteral("exec"),
         ToolCodec::execArgs(QStringLiteral("/bin/ping"), params),
         [this, target, packets](const QJsonObject &result, const Ubus::Error &error) {
             if (!error.isOk()) {
                 emit pingFailed(error);
                 return;
             }
             emit pingFinished(ToolCodec::parsePing(target, result.value(QStringLiteral("stdout")).toString(),
                                                    packets));
         });
}

void UbusClient::traceroute(const QString &target, int maxHops)
{
    if (!ToolCodec::isSafeTarget(target)) {
        emit tracerouteFailed({Ubus::Status::InvalidArgument,
                               QStringLiteral("invalid traceroute target: %1").arg(target)});
        return;
    }
    const int hops = qBound(1, maxHops, 30);
    const QStringList params{QStringLiteral("-n"), QStringLiteral("-q"), QStringLiteral("1"),
                             QStringLiteral("-w"), QStringLiteral("1"),
                             QStringLiteral("-m"), QString::number(hops), target};
    call(QStringLiteral("file"), QStringLiteral("exec"),
         ToolCodec::execArgs(QStringLiteral("/bin/traceroute"), params),
         [this, target](const QJsonObject &result, const Ubus::Error &error) {
             if (!error.isOk()) {
                 emit tracerouteFailed(error);
                 return;
             }
             emit tracerouteFinished(
                 ToolCodec::parseTraceroute(target, result.value(QStringLiteral("stdout")).toString()));
         });
}

void UbusClient::portScan(const QString &target, const QList<int> &ports)
{
    const QString script = ToolCodec::portScanScript(target, ports);
    if (script.isEmpty()) {
        emit portScanFailed({Ubus::Status::InvalidArgument,
                             QStringLiteral("invalid port scan target or empty port list")});
        return;
    }
    call(QStringLiteral("file"), QStringLiteral("exec"),
         ToolCodec::execArgs(QStringLiteral("/bin/sh"),
                             {QStringLiteral("-c"), script}),
         [this, target](const QJsonObject &result, const Ubus::Error &error) {
             if (!error.isOk()) {
                 emit portScanFailed(error);
                 return;
             }
             emit portScanFinished(
                 ToolCodec::parsePortScan(target, result.value(QStringLiteral("stdout")).toString()));
         });
}

void UbusClient::fetchQdiscs()
{
    call(QStringLiteral("file"), QStringLiteral("exec"),
         ToolCodec::execArgs(QStringLiteral("/sbin/tc"),
                             {QStringLiteral("-s"), QStringLiteral("qdisc"), QStringLiteral("show")}),
         [this](const QJsonObject &result, const Ubus::Error &error) {
             if (!error.isOk()) {
                 emit qdiscsFailed(error);
                 return;
             }
             emit qdiscsReceived(
                 ToolCodec::parseQdiscs(result.value(QStringLiteral("stdout")).toString()));
         });
}

void UbusClient::fetchConntrack()
{
    // `file read` of /proc/net/nf_conntrack answers one short read (~4 KB): the
    // kernel's seq_file shows only the first entries. cat through file.exec
    // returns the whole table, both directions included.
    call(QStringLiteral("file"), QStringLiteral("exec"),
         ToolCodec::execArgs(QStringLiteral("/bin/cat"),
                             {QStringLiteral("/proc/net/nf_conntrack")}),
         [this](const QJsonObject &result, const Ubus::Error &error) {
             if (!error.isOk()) {
                 emit conntrackFailed(error);
                 return;
             }
             emit conntrackReceived(
                 ToolCodec::parseConntrack(result.value(QStringLiteral("stdout")).toString()));
         });
}

void UbusClient::fetchLocalAddresses()
{
    call(QStringLiteral("network.interface"), QStringLiteral("dump"), {},
         [this](const QJsonObject &result, const Ubus::Error &error) {
             if (!error.isOk()) {
                 emit conntrackFailed(error);
                 return;
             }
             QStringList addresses;
             for (const QJsonValue &value : result.value(QStringLiteral("interface")).toArray()) {
                 const QJsonObject entry = value.toObject();
                 for (const char *key : {"ipv4-address", "ipv6-address"}) {
                     for (const QJsonValue &address :
                          entry.value(QLatin1String(key)).toArray()) {
                         const QString ip = address.toObject().value(QStringLiteral("address")).toString();
                         if (!ip.isEmpty())
                             addresses.append(ip);
                     }
                 }
             }
             addresses.removeDuplicates();
             emit localAddressesReceived(addresses);
         });
}

void UbusClient::fileList(const QString &path)
{
    call(QStringLiteral("file"), QStringLiteral("list"), ToolCodec::fileListArgs(path),
         [this, path](const QJsonObject &result, const Ubus::Error &error) {
             if (!error.isOk()) {
                 emit fileListingFailed(error);
                 return;
             }
             emit fileListingReceived(ToolCodec::parseFileListing(path, result));
         });
}

void UbusClient::fileRead(const QString &path)
{
    call(QStringLiteral("file"), QStringLiteral("read"), ToolCodec::fileReadArgs(path),
         [this, path](const QJsonObject &result, const Ubus::Error &error) {
             if (!error.isOk()) {
                 emit fileReadFailed(error);
                 return;
             }
             emit fileReadFinished(path, ToolCodec::parseFileData(result, true));
         });
}

void UbusClient::fileWrite(const QString &path, const QByteArray &data)
{
    call(QStringLiteral("file"), QStringLiteral("write"), ToolCodec::fileWriteArgs(path, data),
         [this, path, bytes = int(data.size())](const QJsonObject &, const Ubus::Error &error) {
             if (!error.isOk()) {
                 emit fileWriteFailed(error);
                 return;
             }
             emit fileWriteFinished(path, bytes);
         });
}

void UbusClient::fileRemove(const QString &path)
{
    call(QStringLiteral("file"), QStringLiteral("remove"), ToolCodec::fileRemoveArgs(path),
         [this, path](const QJsonObject &, const Ubus::Error &error) {
             if (!error.isOk()) {
                 emit fileRemoveFailed(error);
                 return;
             }
             emit fileRemoveFinished(path);
         });
}

// upgrade_test validates whatever sits at /tmp/firmware.bin and reports the
// sysupgrade(8) verdict; it never writes flash.
void UbusClient::sysupgradeTest()
{
    call(QStringLiteral("rpc-sys"), QStringLiteral("upgrade_test"), {},
         [this](const QJsonObject &result, const Ubus::Error &error) {
             if (!error.isOk()) {
                 emit sysupgradeCheckFailed(error);
                 return;
             }
             emit sysupgradeChecked(ToolCodec::parseSysupgradeCheck(result));
         });
}

void UbusClient::sysupgradeStart()
{
    call(QStringLiteral("rpc-sys"), QStringLiteral("upgrade_start"),
         QJsonObject{{QStringLiteral("keep"), true}},
         [this](const QJsonObject &, const Ubus::Error &error) {
             if (!error.isOk()) {
                 emit sysupgradeStartFailed(error);
                 return;
             }
             emit sysupgradeStarted();
         });
}

void UbusClient::packageInstall(const QStringList &packages)
{
    QStringList cleaned;
    for (const QString &package : packages) {
        if (ToolCodec::isSafeTarget(package))
            cleaned.append(package);
    }
    if (cleaned.isEmpty()) {
        emit packageInstallFailed({Ubus::Status::InvalidArgument,
                                   QStringLiteral("no valid package names")});
        return;
    }
    QStringList params{QStringLiteral("add")};
    params.append(cleaned);
    call(QStringLiteral("file"), QStringLiteral("exec"),
         ToolCodec::execArgs(QStringLiteral("/usr/bin/apk"), params),
         [this](const QJsonObject &result, const Ubus::Error &error) {
             if (!error.isOk()) {
                 emit packageInstallFailed(error);
                 return;
             }
             emit packageInstallFinished(ToolCodec::parseExec(QStringLiteral("/usr/bin/apk"), result));
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
