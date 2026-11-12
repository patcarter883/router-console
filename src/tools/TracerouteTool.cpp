// SPDX-FileCopyrightText: 2026 the router-console authors
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "TracerouteTool.h"

#include <QMetaObject>

#include "ToolFormat.h"
#include "protocol/UbusClient.h"

TracerouteTool::TracerouteTool(UbusClient *client, QObject *parent)
    : QObject(parent), m_client(client)
{
    connect(m_client, &UbusClient::tracerouteFinished, this, &TracerouteTool::onFinished);
    connect(m_client, &UbusClient::tracerouteFailed, this, &TracerouteTool::onFailed);
}

void TracerouteTool::setTarget(const QString &target)
{
    if (m_target == target)
        return;
    m_target = target;
    emit changed();
}

void TracerouteTool::setMaxHops(int hops)
{
    const int bounded = qBound(1, hops, 30);
    if (m_maxHops == bounded)
        return;
    m_maxHops = bounded;
    emit changed();
}

QVariantList TracerouteTool::hops() const
{
    QVariantList rows;
    rows.reserve(m_report.hops.size());
    for (const Ubus::TracerouteHop &hop : m_report.hops) {
        rows.append(QVariantMap{
            {QStringLiteral("ttl"), hop.ttl},
            {QStringLiteral("address"), hop.address == QLatin1String("*")
                                            ? QStringLiteral("*")
                                            : hop.address},
            {QStringLiteral("rtt"), hop.rttMs > 0.0
                                        ? QStringLiteral("%1 ms").arg(hop.rttMs, 0, 'f', 3)
                                        : QStringLiteral("-")},
            {QStringLiteral("note"), hop.annotation.isEmpty() ? QString()
                                                             : QStringLiteral("!%1").arg(hop.annotation)},
            {QStringLiteral("reached"), hop.reached}});
    }
    return rows;
}

void TracerouteTool::start()
{
    if (m_running || m_target.isEmpty())
        return;
    m_running = true;
    m_status = QStringLiteral("tracing %1 (max %2 hops)").arg(m_target).arg(m_maxHops);
    m_report = {};
    emit changed();

    const QString target = m_target;
    const int hops = m_maxHops;
    Tools::onClient(m_client, [client = m_client, target, hops] { client->traceroute(target, hops); });
}

void TracerouteTool::clear()
{
    m_report = {};
    m_status.clear();
    emit changed();
}

void TracerouteTool::reset()
{
    m_running = false;
    clear();
}

void TracerouteTool::onFinished(const Ubus::TracerouteReport &report)
{
    m_running = false;
    m_report = report;
    m_status = report.hops.isEmpty()
                   ? QStringLiteral("no hops answered")
                   : QStringLiteral("%1 hops").arg(report.hops.size());
    emit changed();
}

void TracerouteTool::onFailed(const Ubus::Error &error)
{
    m_running = false;
    m_status = QStringLiteral("failed: %1").arg(error.message);
    emit changed();
}
