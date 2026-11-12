// SPDX-FileCopyrightText: 2026 the router-console authors
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>

#include "protocol/ToolTypes.h"
#include "protocol/UbusTypes.h"

class UbusClient;

// DHCP lease window state: live dnsmasq/odhcpd leases plus the reservations
// from `config host`, in one table. Refreshed on demand (leases move slowly and
// every refresh costs two ubus calls).
class LeaseTool : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QVariantList leases READ leases NOTIFY changed)
    Q_PROPERTY(bool busy READ isBusy NOTIFY changed)
    Q_PROPERTY(QString statusText READ statusText NOTIFY changed)
    Q_PROPERTY(int dynamicCount READ dynamicCount NOTIFY changed)
    Q_PROPERTY(int reservationCount READ reservationCount NOTIFY changed)

public:
    explicit LeaseTool(UbusClient *client, QObject *parent = nullptr);

    QVariantList leases() const;
    bool isBusy() const { return m_busy; }
    QString statusText() const { return m_status; }
    int dynamicCount() const { return m_dynamicCount; }
    int reservationCount() const { return int(m_leases.size()) - m_dynamicCount; }

    Q_INVOKABLE void refresh();
    void reset();

signals:
    void changed();

private:
    void onLeases(const QList<Ubus::DhcpLease> &leases);
    void onFailed(const Ubus::Error &error);

    UbusClient *m_client = nullptr;
    QList<Ubus::DhcpLease> m_leases;
    int m_dynamicCount = 0;
    bool m_busy = false;
    QString m_status;
};
