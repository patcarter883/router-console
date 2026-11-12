// SPDX-FileCopyrightText: 2026 the router-console authors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "MdnsDiscovery.h"

#include <QNetworkDatagram>
#include <QTimer>
#include <QUdpSocket>

namespace
{
constexpr quint16 MdnsPort = 5353;
const char *const MdnsGroup = "224.0.0.251";
const char *const ServiceEnumeration = "_services._dns-sd._udp.local";

enum RecordType : quint16 { TypeA = 1, TypePtr = 12, TypeTxt = 16, TypeAaaa = 28, TypeSrv = 33 };

struct Reader
{
    const QByteArray &buf;
    int offset = 0;

    bool atEnd() const { return offset >= buf.size(); }
    quint8 u8()
    {
        if (atEnd())
            return 0;
        return static_cast<quint8>(buf.at(offset++));
    }
    quint16 u16()
    {
        const quint8 hi = u8();
        const quint8 lo = u8();
        return static_cast<quint16>((hi << 8) | lo);
    }
    quint32 u32()
    {
        const quint16 hi = u16();
        const quint16 lo = u16();
        return (static_cast<quint32>(hi) << 16) | lo;
    }

    // RFC 1035 name with compression pointers; returns false on malformed input.
    bool readName(QString &out, int depth = 0)
    {
        if (depth > 8)
            return false;
        QStringList labels;
        while (!atEnd()) {
            const int length = u8();
            if (length == 0)
                break;
            if ((length & 0xC0) == 0xC0) {
                const int low = u8();
                const int target = ((length & 0x3F) << 8) | low;
                if (target >= buf.size())
                    return false;
                const int saved = offset;
                offset = target;
                QString tail;
                if (!readName(tail, depth + 1))
                    return false;
                labels.append(tail);
                offset = saved;
                break;
            }
            if (offset + length > buf.size())
                return false;
            labels.append(QString::fromUtf8(buf.constData() + offset, length));
            offset += length;
        }
        out = labels.join(QLatin1Char('.'));
        return true;
    }
};

QByteArray encodeName(const QString &name)
{
    QByteArray out;
    const QStringList labels = name.split(QLatin1Char('.'), Qt::SkipEmptyParts);
    for (const QString &label : labels) {
        out.append(static_cast<char>(label.size()));
        out.append(label.toUtf8());
    }
    out.append('\0');
    return out;
}

QString localSuffix()
{
    return QStringLiteral(".local");
}

bool endsWithLocal(const QString &name)
{
    return name.endsWith(localSuffix(), Qt::CaseInsensitive);
}

QString withoutLocal(const QString &name)
{
    return name.chopped(localSuffix().size());
}
} // namespace

MdnsDiscovery::MdnsDiscovery(QObject *parent) : QObject(parent) {}

MdnsDiscovery::~MdnsDiscovery() = default;

void MdnsDiscovery::start()
{
    if (m_socket)
        return;

    m_socket = new QUdpSocket(this);
    if (!m_socket->bind(QHostAddress::AnyIPv4, MdnsPort,
                        QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint)) {
        emit stateChanged(QStringLiteral("discovery unavailable: %1").arg(m_socket->errorString()));
        delete m_socket;
        m_socket = nullptr;
        return;
    }
    m_socket->setSocketOption(QAbstractSocket::MulticastTtlOption, 255);
    connect(m_socket, &QUdpSocket::readyRead, this, &MdnsDiscovery::onReadyRead);

    // Avahi and friends join the same group; a repeated join is not an error and
    // an interface without IPv4 multicast support is simply skipped.
    const QList<QNetworkInterface> all = QNetworkInterface::allInterfaces();

    // Every local address, including interfaces not joined below: a responder
    // announcing on one segment also advertises the host's other addresses.
    for (const QNetworkInterface &iface : all) {
        for (const QNetworkAddressEntry &entry : iface.addressEntries()) {
            if (entry.ip().protocol() == QAbstractSocket::IPv4Protocol)
                m_localAddresses.insert(entry.ip().toString());
        }
    }

    for (const QNetworkInterface &iface : all) {
        const auto flags = iface.flags();
        if (!(flags & QNetworkInterface::IsUp) || !(flags & QNetworkInterface::IsRunning)
            || (flags & QNetworkInterface::IsLoopBack))
            continue;
        bool hasIpv4 = false;
        for (const QNetworkAddressEntry &entry : iface.addressEntries()) {
            if (entry.ip().protocol() == QAbstractSocket::IPv4Protocol)
                hasIpv4 = true;
        }
        if (!hasIpv4)
            continue;
        const QString name = iface.humanReadableName();
        // Container bridges are not the segment a router is on.
        if (name.startsWith(QLatin1String("docker")) || name.startsWith(QLatin1String("veth"))
            || (name.startsWith(QLatin1String("br-")) && name.size() == 15))
            continue;
        m_socket->joinMulticastGroup(QHostAddress(QString::fromLatin1(MdnsGroup)), iface);
        m_joined.append(iface);
        m_interfaces.append(iface.humanReadableName());
    }

    if (m_joined.isEmpty()) {
        emit stateChanged(QStringLiteral("discovery idle: no multicast interface"));
        return;
    }

    emit stateChanged(QStringLiteral("discovery listening on %1")
                          .arg(m_interfaces.join(QStringLiteral(", "))));

    sendBrowseQueries();
    m_queryTimer = new QTimer(this);
    m_queryTimer->setInterval(5000);
    connect(m_queryTimer, &QTimer::timeout, this, &MdnsDiscovery::sendBrowseQueries);
    m_queryTimer->start();
}

void MdnsDiscovery::stop()
{
    if (m_queryTimer) {
        m_queryTimer->stop();
        m_queryTimer->deleteLater();
        m_queryTimer = nullptr;
    }
    if (m_socket) {
        for (const QNetworkInterface &iface : m_joined)
            m_socket->leaveMulticastGroup(QHostAddress(QString::fromLatin1(MdnsGroup)), iface);
        m_socket->deleteLater();
        m_socket = nullptr;
    }
    m_joined.clear();
    m_interfaces.clear();
}

void MdnsDiscovery::sendBrowseQueries()
{
    queryServiceType(QString::fromLatin1(ServiceEnumeration));
    for (const QString &type : m_serviceTypes)
        queryServiceType(type);
}

void MdnsDiscovery::queryServiceType(const QString &serviceType)
{
    if (!m_socket)
        return;

    QByteArray packet;
    packet.append(QByteArray(2, '\0'));            // transaction id (unused in mDNS)
    packet.append(QByteArray::fromHex("0000"));    // flags: standard query
    packet.append(QByteArray::fromHex("0001"));    // 1 question
    packet.append(QByteArray(6, '\0'));            // answer/authority/additional counts
    packet.append(encodeName(serviceType));
    packet.append(QByteArray::fromHex("000c"));    // PTR
    packet.append(QByteArray::fromHex("0001"));    // class IN, unicast-response not requested

    for (const QNetworkInterface &iface : m_joined) {
        for (const QNetworkAddressEntry &entry : iface.addressEntries()) {
            if (entry.ip().protocol() != QAbstractSocket::IPv4Protocol)
                continue;
            m_socket->setMulticastInterface(iface);
            m_socket->writeDatagram(packet, QHostAddress(QString::fromLatin1(MdnsGroup)), MdnsPort);
            break;
        }
    }
}

void MdnsDiscovery::onReadyRead()
{
    while (m_socket && m_socket->hasPendingDatagrams()) {
        const QNetworkDatagram datagram = m_socket->receiveDatagram();
        if (!datagram.isValid())
            continue;
        handlePacket(datagram.data(), datagram.senderAddress().toString());
    }
}

void MdnsDiscovery::handlePacket(const QByteArray &packet, const QString &sender)
{
    Reader reader{packet};
    const quint16 id = reader.u16();
    Q_UNUSED(id);
    reader.u16(); // flags: queries and responses both carry usable answers
    const quint16 questions = reader.u16();
    const quint16 answers = reader.u16();
    const quint16 authority = reader.u16();
    const quint16 additional = reader.u16();
    if (questions > 64 || answers + authority + additional > 512)
        return;

    for (quint16 i = 0; i < questions; ++i) {
        QString name;
        if (!reader.readName(name))
            return;
        reader.u16();
        reader.u16();
    }

    const quint32 total = static_cast<quint32>(answers) + authority + additional;
    for (quint32 i = 0; i < total; ++i) {
        QString name;
        if (!reader.readName(name))
            return;
        const quint16 type = reader.u16();
        reader.u16(); // class (top bit is cache-flush on responses)
        reader.u32(); // ttl
        const quint16 rdlength = reader.u16();
        const int rdataStart = reader.offset;
        if (rdataStart + rdlength > packet.size())
            return;
        const QByteArray rdata = packet.mid(rdataStart, rdlength);
        reader.offset = rdataStart + rdlength;
        switch (type) {
        case TypeA:
            handleARecord(name, rdata);
            break;
        case TypePtr: {
            // Names inside rdata may use compression pointers into the message.
            Reader sub{packet};
            sub.offset = rdataStart;
            QString target;
            if (sub.readName(target))
                handlePtrRecord(name, target, sender);
            break;
        }
        case TypeSrv: {
            Reader sub{packet};
            sub.offset = rdataStart;
            sub.u16();
            sub.u16();
            const quint16 port = sub.u16();
            QString target;
            if (sub.readName(target))
                handleSrvRecord(name, target, port);
            break;
        }
        default:
            break;
        }
    }
}

void MdnsDiscovery::handleARecord(const QString &name, const QByteArray &rdata)
{
    if (rdata.size() != 4 || !endsWithLocal(name))
        return;
    const QString address = QStringLiteral("%1.%2.%3.%4")
                                .arg(static_cast<quint8>(rdata.at(0)))
                                .arg(static_cast<quint8>(rdata.at(1)))
                                .arg(static_cast<quint8>(rdata.at(2)))
                                .arg(static_cast<quint8>(rdata.at(3)));
    if (address.startsWith(QLatin1String("127.")) || address == QLatin1String("0.0.0.0")
        || m_localAddresses.contains(address))
        return; // the client's own avahi announcing itself on this segment

    const QString fqdn = name.toLower();
    Entry &entry = m_entries[fqdn];
    entry.fqdn = fqdn;
    entry.address = address;
    const QStringList pending = m_pendingServices.value(address);
    for (const QString &service : pending) {
        if (!entry.services.contains(service))
            entry.services.append(service);
    }
    publish(fqdn);
}

void MdnsDiscovery::handlePtrRecord(const QString &name, const QString &target,
                                    const QString &sender)
{
    if (!name.endsWith(QLatin1String(ServiceEnumeration), Qt::CaseInsensitive))
        return;
    if (!target.endsWith(QLatin1String("._tcp.local"), Qt::CaseInsensitive)
        && !target.endsWith(QLatin1String("._udp.local"), Qt::CaseInsensitive))
        return;

    // The responder that enumerates its service types is the host offering
    // them, so attribute the type to its address even before its A record.
    const QString service = target.chopped(QStringLiteral(".local").size());
    if (!sender.isEmpty() && !m_localAddresses.contains(sender)) {
        bool attributed = false;
        for (auto it = m_entries.begin(); it != m_entries.end(); ++it) {
            if (it->address != sender)
                continue;
            if (!it->services.contains(service)) {
                it->services.append(service);
                publish(it.key());
            }
            attributed = true;
        }
        if (!attributed && !m_pendingServices.value(sender).contains(service))
            m_pendingServices[sender].append(service);
    }

    if (m_serviceTypes.contains(target))
        return;
    m_serviceTypes.insert(target);
    queryServiceType(target);
}

void MdnsDiscovery::handleSrvRecord(const QString &name, const QString &target, quint16 port)
{
    if (!endsWithLocal(target))
        return;
    // instance._service._tcp.local -> "_service._tcp.local"
    const QStringList labels = name.split(QLatin1Char('.'));
    QString service;
    if (labels.size() >= 4)
        service = labels.mid(1, labels.size() - 2).join(QLatin1Char('.'));

    const QString fqdn = target.toLower();
    Entry &entry = m_entries[fqdn];
    entry.fqdn = fqdn;
    entry.port = port;
    if (!service.isEmpty() && !entry.services.contains(service))
        entry.services.append(service);
    publish(fqdn);
}

void MdnsDiscovery::publish(const QString &fqdn)
{
    const auto it = m_entries.constFind(fqdn);
    if (it == m_entries.constEnd() || it->address.isEmpty())
        return;

    Mdns::Host host;
    host.hostname = withoutLocal(it->fqdn);
    host.address = it->address;
    host.port = it->port == 0 ? 443 : it->port;
    host.services = it->services;
    host.lastSeen = QDateTime::currentDateTimeUtc();

    if (m_published.contains(fqdn) && m_published.value(fqdn) == host)
        return;
    m_published.insert(fqdn, host);
    emit hostFound(host);
}
