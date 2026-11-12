// SPDX-FileCopyrightText: 2026 the router-console authors
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "RoutingTools.h"

#include "protocol/UbusClient.h"
#include "tools/ToolFormat.h"

#include <algorithm>

namespace Routing {

RoutesTool::RoutesTool(UbusClient *client, QObject *parent) : QObject(parent), m_client(client)
{
    connect(m_client, &UbusClient::routesReceived, this, &RoutesTool::onRoutes);
    connect(m_client, &UbusClient::routingDaemonsReceived, this, &RoutesTool::onDaemons);
    connect(m_client, &UbusClient::routingFailed, this, &RoutesTool::onFailed);
    m_timer.setInterval(1000);
    connect(&m_timer, &QTimer::timeout, this, &RoutesTool::refresh);
}

void RoutesTool::setProtocolFilter(const QString &filter)
{
    if (m_filter == filter)
        return;
    m_filter = filter;
    m_model.setProtocolFilter(filter == QLatin1String("all") ? QStringList()
                                                             : QStringList{filter});
    updateDaemonText();
    emit changed();
}

QStringList RoutesTool::protocolChoices() const
{
    QStringList choices{QStringLiteral("all")};
    choices.append(m_model.protocols());
    return choices;
}

void RoutesTool::setAutoRefresh(bool enabled)
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

void RoutesTool::refresh()
{
    if (m_busy)
        return;
    m_busy = true;
    m_error.clear();
    emit changed();
    if (!m_daemonsRequested) {
        m_daemonsRequested = true;
        Tools::onClient(m_client, [client = m_client] { client->fetchRoutingDaemons(); });
    }
    Tools::onClient(m_client, [client = m_client] { client->fetchRoutes(); });
}

void RoutesTool::reset()
{
    m_timer.stop();
    m_autoRefresh = false;
    m_busy = false;
    m_daemonsRequested = false;
    m_daemons = DaemonState{};
    m_model.setRoutes({});
    m_status.clear();
    m_daemonText.clear();
    m_error.clear();
    emit changed();
}

void RoutesTool::onRoutes(const RouteTable &table)
{
    m_busy = false;
    m_model.setRoutes(table.routes);
    m_model.setProtocolFilter(m_filter == QLatin1String("all") ? QStringList()
                                                               : QStringList{m_filter});
    m_status = QStringLiteral("%1 routes, %2 selected")
                   .arg(table.routes.size())
                   .arg(std::count_if(table.routes.cbegin(), table.routes.cend(),
                                      [](const RouteEntry &route) { return route.selected; }));
    updateDaemonText();
    emit changed();
    emit loaded();
}

void RoutesTool::onDaemons(const Daemons &daemons)
{
    m_daemons.known = true;
    m_daemons.daemons = daemons;
    updateDaemonText();
    emit changed();
}

void RoutesTool::onFailed(const Ubus::Error &error)
{
    m_busy = false;
    m_error = error.message;
    m_status = QStringLiteral("failed: %1").arg(error.message);
    emit changed();
}

void RoutesTool::updateDaemonText()
{
    if (!m_daemons.known) {
        m_daemonText.clear();
        return;
    }
    const Daemons &daemons = m_daemons.daemons;
    if (!daemons.installed) {
        m_daemonText = QStringLiteral("frr not installed");
        return;
    }
    const QStringList running = daemons.running;
    m_daemonText = running.isEmpty() ? QStringLiteral("frr installed, no daemon running")
                                     : QStringLiteral("daemons: %1").arg(running.join(QLatin1Char(' ')));
}

OspfTool::OspfTool(UbusClient *client, QObject *parent) : QObject(parent), m_client(client)
{
    connect(m_client, &UbusClient::ospfStatusReceived, this, &OspfTool::onStatus);
    connect(m_client, &UbusClient::routingFailed, this, &OspfTool::onFailed);
}

void OspfTool::refresh()
{
    if (m_busy)
        return;
    m_busy = true;
    m_error.clear();
    emit changed();
    Tools::onClient(m_client, [client = m_client] { client->fetchOspfStatus(); });
}

void OspfTool::reset()
{
    m_busy = false;
    m_neighbors.setNeighbors({});
    m_interfaces.setInterfaces({});
    m_status_ = OspfStatus{};
    m_routerId.clear();
    m_statusText.clear();
    m_error.clear();
    emit changed();
}

QVariantList OspfTool::database() const
{
    QVariantList rows;
    for (const OspfAreaDatabase &area : m_status_.database) {
        rows.append(QVariantMap{{QStringLiteral("area"), area.area},
                                {QStringLiteral("kind"), area.kind},
                                {QStringLiteral("count"), area.count}});
    }
    return rows;
}

void OspfTool::onStatus(const OspfStatus &status)
{
    m_busy = false;
    m_status_ = status;
    m_routerId = status.routerId;
    m_neighbors.setNeighbors(status.neighbors);
    m_interfaces.setInterfaces(status.interfaces);
    m_statusText =
        QStringLiteral("router-id %1, %2 interface(s), %3 neighbor(s), %4 full")
            .arg(status.routerId.isEmpty() ? QStringLiteral("?") : status.routerId)
            .arg(status.interfaces.size())
            .arg(status.neighbors.size())
            .arg(status.fullNeighborCount());
    emit changed();
}

void OspfTool::onFailed(const Ubus::Error &error)
{
    m_busy = false;
    m_error = error.message;
    m_statusText = QStringLiteral("failed: %1").arg(error.message);
    emit changed();
}

BgpTool::BgpTool(UbusClient *client, QObject *parent) : QObject(parent), m_client(client)
{
    connect(m_client, &UbusClient::bgpStatusReceived, this, &BgpTool::onSummary);
    connect(m_client, &UbusClient::routingFailed, this, &BgpTool::onFailed);
}

void BgpTool::refresh()
{
    if (m_busy)
        return;
    m_busy = true;
    m_error.clear();
    emit changed();
    Tools::onClient(m_client, [client = m_client] { client->fetchBgpStatus(); });
}

void BgpTool::reset()
{
    m_busy = false;
    m_peers.setPeers({});
    m_summary = BgpSummary{};
    m_status.clear();
    m_error.clear();
    emit changed();
}

void BgpTool::onSummary(const BgpSummary &summary)
{
    m_busy = false;
    m_summary = summary;
    m_peers.setPeers(summary.peers);
    if (summary.routerId.isEmpty() && summary.peers.isEmpty()) {
        m_status = QStringLiteral("bgpd not running or no BGP instance configured");
        emit changed();
        return;
    }
    m_status = QStringLiteral("AS %1, router-id %2, %3 peer(s), %4 established, %5 RIB entries")
                   .arg(summary.localAs)
                   .arg(summary.routerId)
                   .arg(summary.peers.size())
                   .arg(summary.establishedCount())
                   .arg(summary.ribEntries);
    emit changed();
}

void BgpTool::onFailed(const Ubus::Error &error)
{
    m_busy = false;
    m_error = error.message;
    m_status = QStringLiteral("failed: %1").arg(error.message);
    emit changed();
}

} // namespace Routing
