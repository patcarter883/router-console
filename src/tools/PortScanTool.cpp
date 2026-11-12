// SPDX-FileCopyrightText: 2026 the router-console authors
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "PortScanTool.h"

#include <QMetaObject>
#include <QRegularExpression>

#include "ToolFormat.h"
#include "protocol/UbusClient.h"

PortScanTool::PortScanTool(UbusClient *client, QObject *parent)
    : QObject(parent), m_client(client)
{
    connect(m_client, &UbusClient::portScanFinished, this, &PortScanTool::onFinished);
    connect(m_client, &UbusClient::portScanFailed, this, &PortScanTool::onFailed);
}

void PortScanTool::setTarget(const QString &target)
{
    if (m_target == target)
        return;
    m_target = target;
    emit changed();
}

void PortScanTool::setPortsText(const QString &text)
{
    if (m_portsText == text)
        return;
    m_portsText = text;
    emit changed();
}

QVariantList PortScanTool::results() const
{
    QVariantList rows;
    rows.reserve(m_report.probes.size());
    for (const Ubus::PortProbe &probe : m_report.probes) {
        rows.append(QVariantMap{{QStringLiteral("port"), probe.port},
                                {QStringLiteral("state"), probe.stateText()},
                                {QStringLiteral("isOpen"), probe.state == Ubus::PortProbe::State::Open}});
    }
    return rows;
}

QList<int> PortScanTool::parsePorts(const QString &text)
{
    QList<int> ports;
    static const QRegularExpression tokenPattern(QStringLiteral("^\\s*(\\d+)\\s*(?:-\\s*(\\d+)\\s*)?$"));
    for (const QString &token : text.split(QRegularExpression(QStringLiteral("[,;\\s]+")),
                                           Qt::SkipEmptyParts)) {
        const QRegularExpressionMatch match = tokenPattern.match(token);
        if (!match.hasMatch())
            continue;
        int first = match.captured(1).toInt();
        int last = match.captured(2).isEmpty() ? first : match.captured(2).toInt();
        if (first > last)
            std::swap(first, last);
        first = qBound(1, first, 65535);
        last = qBound(1, last, 65535);
        for (int port = first; port <= last; ++port)
            ports.append(port);
    }
    std::sort(ports.begin(), ports.end());
    ports.erase(std::unique(ports.begin(), ports.end()), ports.end());
    return ports;
}

void PortScanTool::start()
{
    if (m_running || m_target.isEmpty())
        return;
    const QList<int> ports = parsePorts(m_portsText);
    if (ports.isEmpty()) {
        m_status = QStringLiteral("no valid ports");
        emit changed();
        return;
    }
    m_running = true;
    m_status = QStringLiteral("scanning %1 ports on %2").arg(ports.size()).arg(m_target);
    m_report = {};
    emit changed();

    const QString target = m_target;
    Tools::onClient(m_client, [client = m_client, target, ports] { client->portScan(target, ports); });
}

void PortScanTool::clear()
{
    m_report = {};
    m_status.clear();
    emit changed();
}

void PortScanTool::reset()
{
    m_running = false;
    clear();
}

void PortScanTool::onFinished(const Ubus::PortScanReport &report)
{
    m_running = false;
    m_report = report;
    m_status = QStringLiteral("%1 open of %2 probed").arg(report.openCount()).arg(report.probes.size());
    emit changed();
}

void PortScanTool::onFailed(const Ubus::Error &error)
{
    m_running = false;
    m_status = QStringLiteral("failed: %1").arg(error.message);
    emit changed();
}
