// SPDX-FileCopyrightText: 2026 the router-console authors
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <QAbstractListModel>
#include <QList>
#include <QStringList>

#include "protocol/RoutingTypes.h"

// Typed list models for the Routing windows: one row per parsed struct, no
// QVariant maps in the view path.
namespace Routing {

class RoutesModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Role {
        PrefixRole = Qt::UserRole + 1,
        FamilyRole,
        ProtocolRole,
        DistanceRole,
        MetricRole,
        SelectedRole,
        InstalledRole,
        UptimeRole,
        NexthopRole,
        SourceRole,
    };

    explicit RoutesModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setRoutes(const QList<RouteEntry> &routes);
    // Empty list shows every protocol.
    void setProtocolFilter(const QStringList &protocols);
    Q_INVOKABLE int countFor(const QString &protocol) const;
    QStringList protocols() const;
    int totalCount() const { return int(m_routes.size()); }

private:
    void rebuild();

    QList<RouteEntry> m_routes;
    QList<int> m_visible;
    QStringList m_filter;
};

class OspfNeighborsModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Role {
        NeighborIdRole = Qt::UserRole + 1,
        AddressRole,
        InterfaceRole,
        StateRole,
        PriorityRole,
        UpTimeRole,
        DeadTimeRole,
        FullRole,
    };

    explicit OspfNeighborsModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setNeighbors(const QList<OspfNeighbor> &neighbors);
    QList<OspfNeighbor> neighbors() const { return m_neighbors; }

private:
    QList<OspfNeighbor> m_neighbors;
};

class OspfInterfacesModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Role {
        NameRole = Qt::UserRole + 1,
        AreaRole,
        AddressRole,
        NetworkTypeRole,
        StateRole,
        CostRole,
        PriorityRole,
        NeighborCountRole,
        AdjacentCountRole,
        UpRole,
    };

    explicit OspfInterfacesModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setInterfaces(const QList<OspfInterface> &interfaces);
    QList<OspfInterface> interfaces() const { return m_interfaces; }

private:
    QList<OspfInterface> m_interfaces;
};

class BgpPeersModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Role {
        AddressRole = Qt::UserRole + 1,
        RemoteAsRole,
        LocalAsRole,
        StateRole,
        UptimeRole,
        PfxRcvdRole,
        PfxSntRole,
        MsgRcvdRole,
        MsgSentRole,
        EstablishedRole,
    };

    explicit BgpPeersModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setPeers(const QList<BgpPeer> &peers);
    QList<BgpPeer> peers() const { return m_peers; }

private:
    QList<BgpPeer> m_peers;
};

} // namespace Routing
