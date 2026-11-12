// SPDX-FileCopyrightText: 2026 the router-console authors
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "RoutingModels.h"

namespace Routing {

RoutesModel::RoutesModel(QObject *parent) : QAbstractListModel(parent) {}

int RoutesModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_visible.size());
}

QVariant RoutesModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_visible.size())
        return {};
    const RouteEntry &route = m_routes.at(m_visible.at(index.row()));

    switch (role) {
    case PrefixRole: return route.prefix;
    case FamilyRole: return route.family;
    case ProtocolRole: return route.protocol;
    case DistanceRole: return route.distance;
    case MetricRole: return route.metric;
    case SelectedRole: return route.selected;
    case InstalledRole: return route.installed;
    case UptimeRole: return route.uptime;
    case NexthopRole: return route.nexthops.join(QStringLiteral(", "));
    case SourceRole: return route.source;
    default: return {};
    }
}

QHash<int, QByteArray> RoutesModel::roleNames() const
{
    return {{PrefixRole, "prefix"},         {FamilyRole, "family"},
            {ProtocolRole, "protocol"},     {DistanceRole, "distance"},
            {MetricRole, "metric"},         {SelectedRole, "selected"},
            {InstalledRole, "installed"},   {UptimeRole, "uptime"},
            {NexthopRole, "nexthop"},       {SourceRole, "source"}};
}

void RoutesModel::setRoutes(const QList<RouteEntry> &routes)
{
    m_routes = routes;
    rebuild();
}

void RoutesModel::setProtocolFilter(const QStringList &protocols)
{
    m_filter = protocols;
    rebuild();
}

int RoutesModel::countFor(const QString &protocol) const
{
    int count = 0;
    for (const RouteEntry &route : m_routes) {
        if (route.protocol == protocol)
            ++count;
    }
    return count;
}

QStringList RoutesModel::protocols() const
{
    QStringList names;
    for (const RouteEntry &route : m_routes) {
        if (!names.contains(route.protocol))
            names.append(route.protocol);
    }
    names.sort();
    return names;
}

void RoutesModel::rebuild()
{
    beginResetModel();
    m_visible.clear();
    for (int index = 0; index < m_routes.size(); ++index) {
        if (m_filter.isEmpty() || m_filter.contains(m_routes.at(index).protocol))
            m_visible.append(index);
    }
    endResetModel();
}

OspfNeighborsModel::OspfNeighborsModel(QObject *parent) : QAbstractListModel(parent) {}

int OspfNeighborsModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_neighbors.size());
}

QVariant OspfNeighborsModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_neighbors.size())
        return {};
    const OspfNeighbor &neighbor = m_neighbors.at(index.row());

    switch (role) {
    case NeighborIdRole: return neighbor.neighborId;
    case AddressRole: return neighbor.address;
    case InterfaceRole: return neighbor.interface;
    case StateRole: return neighbor.state;
    case PriorityRole: return neighbor.priority;
    case UpTimeRole: return neighbor.upTime;
    case DeadTimeRole: return neighbor.deadTimeSeconds;
    case FullRole: return neighbor.isFull();
    default: return {};
    }
}

QHash<int, QByteArray> OspfNeighborsModel::roleNames() const
{
    return {{NeighborIdRole, "neighborId"}, {AddressRole, "address"},
            {InterfaceRole, "iface"},   {StateRole, "ospfState"},
            {PriorityRole, "priority"},     {UpTimeRole, "uptime"},
            {DeadTimeRole, "deadTime"},     {FullRole, "full"}};
}

void OspfNeighborsModel::setNeighbors(const QList<OspfNeighbor> &neighbors)
{
    beginResetModel();
    m_neighbors = neighbors;
    endResetModel();
}

OspfInterfacesModel::OspfInterfacesModel(QObject *parent) : QAbstractListModel(parent) {}

int OspfInterfacesModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_interfaces.size());
}

QVariant OspfInterfacesModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_interfaces.size())
        return {};
    const OspfInterface &interface = m_interfaces.at(index.row());

    switch (role) {
    case NameRole: return interface.name;
    case AreaRole: return interface.area;
    case AddressRole: return interface.address;
    case NetworkTypeRole: return interface.networkType;
    case StateRole: return interface.state;
    case CostRole: return interface.cost;
    case PriorityRole: return interface.priority;
    case NeighborCountRole: return interface.neighborCount;
    case AdjacentCountRole: return interface.adjacentCount;
    case UpRole: return interface.up;
    default: return {};
    }
}

QHash<int, QByteArray> OspfInterfacesModel::roleNames() const
{
    return {{NameRole, "name"},         {AreaRole, "area"},
            {AddressRole, "address"},   {NetworkTypeRole, "networkType"},
            {StateRole, "ospfState"},       {CostRole, "cost"},
            {PriorityRole, "priority"}, {NeighborCountRole, "neighborCount"},
            {AdjacentCountRole, "adjacentCount"}, {UpRole, "up"}};
}

void OspfInterfacesModel::setInterfaces(const QList<OspfInterface> &interfaces)
{
    beginResetModel();
    m_interfaces = interfaces;
    endResetModel();
}

BgpPeersModel::BgpPeersModel(QObject *parent) : QAbstractListModel(parent) {}

int BgpPeersModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_peers.size());
}

QVariant BgpPeersModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_peers.size())
        return {};
    const BgpPeer &peer = m_peers.at(index.row());

    switch (role) {
    case AddressRole: return peer.address;
    case RemoteAsRole: return peer.remoteAs;
    case LocalAsRole: return peer.localAs;
    case StateRole: return peer.state;
    case UptimeRole: return peer.uptime;
    case PfxRcvdRole: return peer.pfxRcvd;
    case PfxSntRole: return peer.pfxSnt;
    case MsgRcvdRole: return peer.msgRcvd;
    case MsgSentRole: return peer.msgSent;
    case EstablishedRole: return peer.isEstablished();
    default: return {};
    }
}

QHash<int, QByteArray> BgpPeersModel::roleNames() const
{
    return {{AddressRole, "address"},   {RemoteAsRole, "remoteAs"},
            {LocalAsRole, "localAs"},   {StateRole, "bgpState"},
            {UptimeRole, "uptime"},     {PfxRcvdRole, "pfxRcvd"},
            {PfxSntRole, "pfxSnt"},     {MsgRcvdRole, "msgRcvd"},
            {MsgSentRole, "msgSent"},   {EstablishedRole, "established"}};
}

void BgpPeersModel::setPeers(const QList<BgpPeer> &peers)
{
    beginResetModel();
    m_peers = peers;
    endResetModel();
}

} // namespace Routing
