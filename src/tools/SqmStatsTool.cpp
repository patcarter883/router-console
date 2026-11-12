// SPDX-FileCopyrightText: 2026 the router-console authors
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "SqmStatsTool.h"

#include <QMetaObject>

#include "ToolFormat.h"
#include "protocol/UbusClient.h"

SqmStatsTool::SqmStatsTool(UbusClient *client, QObject *parent)
    : QObject(parent), m_client(client)
{
    connect(m_client, &UbusClient::qdiscsReceived, this, &SqmStatsTool::onQdiscs);
    connect(m_client, &UbusClient::qdiscsFailed, this, &SqmStatsTool::onFailed);
    m_timer.setInterval(1000);
    connect(&m_timer, &QTimer::timeout, this, &SqmStatsTool::refresh);
}

QVariantList SqmStatsTool::qdiscs() const
{
    QVariantList rows;
    rows.reserve(m_qdiscs.size());
    for (const Ubus::QdiscStats &qdisc : m_qdiscs) {
        QVariantList tins;
        for (const Ubus::CakeTin &tin : qdisc.tins) {
            tins.append(QVariantMap{
                {QStringLiteral("index"), tin.index},
                {QStringLiteral("threshold"), tin.threshold},
                {QStringLiteral("packets"), QString::number(tin.packets)},
                {QStringLiteral("bytes"), Tools::bytesText(tin.bytes)},
                {QStringLiteral("drops"), QString::number(tin.drops)},
                {QStringLiteral("marks"), QString::number(tin.marks)},
                {QStringLiteral("backlog"), Tools::bytesText(tin.backlogBytes)},
                {QStringLiteral("delay"), QStringLiteral("%1/%2 us").arg(tin.averageDelayUs, 0, 'f', 0)
                                              .arg(tin.peakDelayUs, 0, 'f', 0)}});
        }

        rows.append(QVariantMap{
            {QStringLiteral("device"), qdisc.device},
            {QStringLiteral("kind"), qdisc.kind},
            {QStringLiteral("handle"), qdisc.handle},
            {QStringLiteral("detail"), qdisc.detail},
            {QStringLiteral("cake"), qdisc.isCake()},
            {QStringLiteral("sent"), Tools::bytesText(qdisc.sentBytes)},
            {QStringLiteral("packets"), QString::number(qdisc.sentPackets)},
            {QStringLiteral("dropped"), QString::number(qdisc.dropped)},
            {QStringLiteral("overlimits"), QString::number(qdisc.overlimits)},
            {QStringLiteral("requeues"), QString::number(qdisc.requeues)},
            {QStringLiteral("backlog"), Tools::bytesText(qdisc.backlogBytes)},
            {QStringLiteral("memory"),
             QStringLiteral("%1 of %2").arg(Tools::bytesText(qdisc.memoryUsed),
                                            Tools::bytesText(qdisc.memoryLimit))},
            {QStringLiteral("capacity"), qdisc.capacity},
            {QStringLiteral("tins"), tins}});
    }
    return rows;
}

void SqmStatsTool::setAutoRefresh(bool enabled)
{
    if (m_autoRefresh == enabled)
        return;
    m_autoRefresh = enabled;
    if (enabled) {
        m_timer.start();
        refresh();
    } else {
        m_timer.stop();
    }
    emit changed();
}

void SqmStatsTool::refresh()
{
    if (m_busy)
        return;
    m_busy = true;
    emit changed();
    Tools::onClient(m_client, [client = m_client] { client->fetchQdiscs(); });
}

void SqmStatsTool::reset()
{
    m_timer.stop();
    m_autoRefresh = false;
    m_busy = false;
    m_qdiscs.clear();
    m_cakeActive = false;
    m_cakeSummary.clear();
    m_status.clear();
    emit changed();
}

void SqmStatsTool::onQdiscs(const QList<Ubus::QdiscStats> &qdiscs)
{
    m_busy = false;
    m_qdiscs = qdiscs;
    m_cakeActive = false;

    QStringList cakeDevices;
    for (const Ubus::QdiscStats &qdisc : qdiscs) {
        if (!qdisc.isCake())
            continue;
        m_cakeActive = true;
        cakeDevices.append(QStringLiteral("%1 %2").arg(qdisc.device, qdisc.detail));
    }
    m_cakeSummary = m_cakeActive
                        ? QStringLiteral("cake on %1").arg(cakeDevices.join(QStringLiteral("; ")))
                        : QStringLiteral("no cake qdisc (sqm idle or disabled)");
    m_status = QStringLiteral("%1 qdiscs").arg(qdiscs.size());
    emit changed();
}

void SqmStatsTool::onFailed(const Ubus::Error &error)
{
    m_busy = false;
    m_status = QStringLiteral("failed: %1").arg(error.message);
    emit changed();
}
