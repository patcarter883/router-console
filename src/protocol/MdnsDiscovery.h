#pragma once

#include <QHash>
#include <QNetworkInterface>
#include <QObject>
#include <QSet>

#include "MdnsTypes.h"

class QTimer;
class QUdpSocket;

// mDNS (RFC 6762) discovery of OpenWrt routers on the local segment.
//
// umdns — the OpenWrt responder — announces its records once at start-up and
// does not answer queries, so this listener relies on the announcements and
// additionally browses DNS-SD (_services._dns-sd._udp.local) for responders
// that do answer. Records handled: A (hostname.local -> IPv4), SRV/PTR for the
// service list and port. Runs in a worker thread; every socket lives here.
class MdnsDiscovery : public QObject
{
    Q_OBJECT

public:
    explicit MdnsDiscovery(QObject *parent = nullptr);
    ~MdnsDiscovery() override;

    QStringList interfaces() const { return m_interfaces; }

public slots:
    void start();
    void stop();

signals:
    void hostFound(const Mdns::Host &host);
    void stateChanged(const QString &description);

private:
    struct Entry
    {
        QString fqdn;
        QString address;
        quint16 port = 0;
        QStringList services;
    };

    void onReadyRead();
    void sendBrowseQueries();
    void queryServiceType(const QString &serviceType);
    void handlePacket(const QByteArray &packet, const QString &sender);
    void handleARecord(const QString &name, const QByteArray &rdata);
    void handlePtrRecord(const QString &name, const QString &target, const QString &sender);
    void handleSrvRecord(const QString &name, const QString &target, quint16 port);
    void publish(const QString &fqdn);

    QUdpSocket *m_socket = nullptr;
    QTimer *m_queryTimer = nullptr;
    QList<QNetworkInterface> m_joined;
    QStringList m_interfaces;
    QSet<QString> m_localAddresses;
    QSet<QString> m_serviceTypes;
    QHash<QString, Entry> m_entries;
    QHash<QString, QStringList> m_pendingServices; // address -> service types
    QHash<QString, Mdns::Host> m_published;
};
