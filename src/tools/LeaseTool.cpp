// SPDX-FileCopyrightText: 2026 the router-console authors
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "LeaseTool.h"

#include "ToolFormat.h"
#include "protocol/UbusClient.h"

LeaseTool::LeaseTool(UbusClient *client, QObject *parent) : QObject(parent), m_client(client)
{
    connect(m_client, &UbusClient::leasesReceived, this, &LeaseTool::onLeases);
    connect(m_client, &UbusClient::leasesFailed, this, &LeaseTool::onFailed);
}

QVariantList LeaseTool::leases() const
{
    QVariantList rows;
    rows.reserve(m_leases.size());
    for (const Ubus::DhcpLease &lease : m_leases) {
        rows.append(QVariantMap{
            {QStringLiteral("hostname"), lease.hostname.isEmpty() ? QStringLiteral("-")
                                                                 : lease.hostname},
            {QStringLiteral("address"), lease.address},
            {QStringLiteral("mac"), lease.mac},
            {QStringLiteral("duid"), lease.duid},
            {QStringLiteral("interface"), lease.interfaceName},
            {QStringLiteral("remaining"), lease.remainingText()},
            {QStringLiteral("kind"), lease.kindText()},
            {QStringLiteral("dynamic"), lease.isDynamic()}});
    }
    return rows;
}

void LeaseTool::refresh()
{
    if (m_busy)
        return;
    m_busy = true;
    emit changed();
    Tools::onClient(m_client, [client = m_client] { client->fetchDhcpLeases(); });
}

void LeaseTool::reset()
{
    m_busy = false;
    m_leases.clear();
    m_dynamicCount = 0;
    m_status.clear();
    emit changed();
}

void LeaseTool::onLeases(const QList<Ubus::DhcpLease> &leases)
{
    m_busy = false;
    m_leases = leases;
    m_dynamicCount = 0;
    for (const Ubus::DhcpLease &lease : leases) {
        if (lease.isDynamic())
            ++m_dynamicCount;
    }
    m_status = QStringLiteral("%1 rows: %2 dynamic, %3 reserved")
                   .arg(leases.size())
                   .arg(m_dynamicCount)
                   .arg(leases.size() - m_dynamicCount);
    emit changed();
}

void LeaseTool::onFailed(const Ubus::Error &error)
{
    m_busy = false;
    m_status = QStringLiteral("lease read failed: %1").arg(error.message);
    emit changed();
}
