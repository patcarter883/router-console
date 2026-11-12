#pragma once

#include <QDateTime>
#include <QMetaType>
#include <QString>
#include <QStringList>

namespace Mdns
{
// One responder record set: the A record supplies hostname + IPv4 address, an
// SRV record (when the router publishes a service) supplies the port.
struct Host
{
    QString hostname;
    QString address;
    quint16 port = 443;
    QStringList services;
    QDateTime lastSeen;

    bool operator==(const Host &other) const
    {
        return hostname == other.hostname && address == other.address && port == other.port
               && services == other.services;
    }
};
} // namespace Mdns

Q_DECLARE_METATYPE(Mdns::Host)
