#pragma once

#include <QMetaType>
#include <QString>
#include <QStringList>
#include <QList>
#include <QtGlobal>

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

// network.device.status `statistics`: kernel interface counters.
struct DeviceCounters {
    quint64 rxBytes = 0;
    quint64 txBytes = 0;
    quint64 rxPackets = 0;
    quint64 txPackets = 0;
    quint64 rxErrors = 0;
    quint64 txErrors = 0;
    quint64 rxDropped = 0;
    quint64 txDropped = 0;
};

// One entry of network.device.status, keyed by kernel device name (br-lan, eth1, lo).
struct DeviceStatus {
    QString name;
    QString type;       // ubus `type`: "bridge", "Network device", ...
    QString deviceType; // kernel devtype: ethernet, bridge, loopback
    QString macAddress;
    int mtu = 0;
    bool present = false;
    bool up = false;
    bool carrier = false;
    DeviceCounters counters;
};

struct DeviceStatusSnapshot {
    QList<DeviceStatus> devices;

    bool isEmpty() const { return devices.isEmpty(); }

    DeviceStatus device(const QString &name) const
    {
        for (const DeviceStatus &device : devices) {
            if (device.name == name)
                return device;
        }
        return {};
    }
};

// One entry of network.interface.dump.
struct InterfaceInfo {
    QString name;
    QString device;   // configured device, e.g. "br-lan"
    QString l3Device; // device carrying the addresses, e.g. "br-lan", "eth1"
    QString protocol; // static, dhcp, dhcpv6, ...
    bool up = false;
    bool pending = false;
    bool available = false;
    bool autostart = false;
    bool dynamic = false;
    quint64 uptimeSeconds = 0;
    QStringList ipv4Addresses; // "192.168.77.1/24"
    QStringList ipv6Addresses;

    // Device whose counters describe this interface's traffic.
    QString trafficDevice() const { return l3Device.isEmpty() ? device : l3Device; }
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
Q_DECLARE_METATYPE(Ubus::DeviceCounters)
Q_DECLARE_METATYPE(Ubus::DeviceStatus)
Q_DECLARE_METATYPE(Ubus::DeviceStatusSnapshot)
Q_DECLARE_METATYPE(Ubus::InterfaceInfo)
Q_DECLARE_METATYPE(QList<Ubus::InterfaceInfo>)
