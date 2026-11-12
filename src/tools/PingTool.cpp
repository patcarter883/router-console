// SPDX-FileCopyrightText: 2026 the router-console authors
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "PingTool.h"

#include <QMetaObject>

#include "ToolFormat.h"
#include "protocol/UbusClient.h"

PingTool::PingTool(UbusClient *client, QObject *parent)
    : QObject(parent), m_client(client)
{
    connect(m_client, &UbusClient::pingFinished, this, &PingTool::onFinished);
    connect(m_client, &UbusClient::pingFailed, this, &PingTool::onFailed);
}

void PingTool::setTarget(const QString &target)
{
    if (m_target == target)
        return;
    m_target = target;
    emit changed();
}

void PingTool::setCount(int count)
{
    const int bounded = qBound(1, count, 20);
    if (m_count == bounded)
        return;
    m_count = bounded;
    emit changed();
}

QVariantList PingTool::replies() const
{
    QVariantList rows;
    rows.reserve(m_report.replies.size());
    for (const Ubus::PingReply &reply : m_report.replies) {
        rows.append(QVariantMap{{QStringLiteral("sequence"), reply.sequence},
                                {QStringLiteral("address"), reply.address},
                                {QStringLiteral("rtt"), QStringLiteral("%1 ms").arg(reply.rttMs, 0, 'f', 3)}});
    }
    return rows;
}

void PingTool::start()
{
    if (m_running || m_target.isEmpty())
        return;
    m_running = true;
    m_status = QStringLiteral("pinging %1 (%2 packets)").arg(m_target).arg(m_count);
    m_report = {};
    m_summary.clear();
    emit changed();

    const QString target = m_target;
    const int count = m_count;
    Tools::onClient(m_client, [client = m_client, target, count] { client->ping(target, count); });
}

void PingTool::clear()
{
    m_report = {};
    m_summary.clear();
    m_status.clear();
    emit changed();
}

void PingTool::reset()
{
    m_running = false;
    clear();
}

void PingTool::onFinished(const Ubus::PingReport &report)
{
    m_running = false;
    m_report = report;
    if (report.received > 0) {
        m_summary = QStringLiteral("%1/%2 packets, %3% loss, min/avg/max %4/%5/%6 ms")
                        .arg(report.received)
                        .arg(report.transmitted)
                        .arg(report.lossPercent, 0, 'f', 1)
                        .arg(report.minMs, 0, 'f', 3)
                        .arg(report.avgMs, 0, 'f', 3)
                        .arg(report.maxMs, 0, 'f', 3);
        m_status = QStringLiteral("done");
    } else {
        m_summary = QStringLiteral("no replies (%1 packets sent)").arg(report.transmitted);
        m_status = QStringLiteral("unreachable");
    }
    emit changed();
}

void PingTool::onFailed(const Ubus::Error &error)
{
    m_running = false;
    m_status = QStringLiteral("failed: %1").arg(error.message);
    emit changed();
}
