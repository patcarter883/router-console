// SPDX-FileCopyrightText: 2026 the router-console authors
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <QAbstractItemModel>
#include <QList>
#include <QObject>
#include <QStringList>
#include <QTimer>

#include "protocol/RoutingTypes.h"
#include "protocol/UbusTypes.h"
#include "RoutingModels.h"

class UbusClient;

// Window state for the Routing section. Each object lives on the UI thread and
// only ever receives typed structs; every router call is queued onto the ubus
// client's thread. All three share the daemon detection: a window reports
// "frr not installed" / "ospfd not running" instead of an empty table when the
// protocol is not configured on the router.
namespace Routing {

struct DaemonState {
    bool known = false;
    Daemons daemons;
};

class RoutesTool : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QAbstractItemModel *routes READ routes CONSTANT)
    Q_PROPERTY(QString protocolFilter READ protocolFilter WRITE setProtocolFilter NOTIFY changed)
    Q_PROPERTY(QStringList protocolChoices READ protocolChoices NOTIFY changed)
    Q_PROPERTY(bool autoRefresh READ autoRefresh WRITE setAutoRefresh NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString statusText READ statusText NOTIFY changed)
    Q_PROPERTY(QString daemonText READ daemonText NOTIFY changed)
    Q_PROPERTY(QString errorText READ errorText NOTIFY changed)

public:
    explicit RoutesTool(UbusClient *client, QObject *parent = nullptr);

    QAbstractItemModel *routes() { return &m_model; }
    QString protocolFilter() const { return m_filter; }
    void setProtocolFilter(const QString &filter);
    QStringList protocolChoices() const;
    bool autoRefresh() const { return m_autoRefresh; }
    void setAutoRefresh(bool enabled);
    bool busy() const { return m_busy; }
    QString statusText() const { return m_status; }
    QString daemonText() const { return m_daemonText; }
    QString errorText() const { return m_error; }

    Q_INVOKABLE void refresh();
    Q_INVOKABLE int countFor(const QString &protocol) const { return m_model.countFor(protocol); }
    void reset();
    const RoutesModel *model() const { return &m_model; }

signals:
    void changed();
    void loaded();

private:
    void onRoutes(const RouteTable &table);
    void onDaemons(const Daemons &daemons);
    void onFailed(const Ubus::Error &error);
    void updateDaemonText();

    UbusClient *m_client = nullptr;
    QTimer m_timer;
    RoutesModel m_model;
    QString m_filter = QStringLiteral("all");
    bool m_autoRefresh = false;
    bool m_busy = false;
    bool m_daemonsRequested = false;
    DaemonState m_daemons;
    QString m_status;
    QString m_daemonText;
    QString m_error;
};

class OspfTool : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QAbstractItemModel *neighbors READ neighbors CONSTANT)
    Q_PROPERTY(QAbstractItemModel *interfaces READ interfaces CONSTANT)
    Q_PROPERTY(QString routerId READ routerId NOTIFY changed)
    Q_PROPERTY(int neighborCount READ neighborCount NOTIFY changed)
    Q_PROPERTY(int fullCount READ fullCount NOTIFY changed)
    Q_PROPERTY(QVariantList database READ database NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString statusText READ statusText NOTIFY changed)
    Q_PROPERTY(QString errorText READ errorText NOTIFY changed)

public:
    explicit OspfTool(UbusClient *client, QObject *parent = nullptr);

    QAbstractItemModel *neighbors() { return &m_neighbors; }
    QAbstractItemModel *interfaces() { return &m_interfaces; }
    QString routerId() const { return m_routerId; }
    int neighborCount() const { return int(m_status_.neighbors.size()); }
    int fullCount() const { return m_status_.fullNeighborCount(); }
    QVariantList database() const;
    bool busy() const { return m_busy; }
    QString statusText() const { return m_statusText; }
    QString errorText() const { return m_error; }

    Q_INVOKABLE void refresh();
    void reset();
    const OspfStatus &status() const { return m_status_; }

signals:
    void changed();

private:
    void onStatus(const OspfStatus &status);
    void onFailed(const Ubus::Error &error);

    UbusClient *m_client = nullptr;
    OspfNeighborsModel m_neighbors;
    OspfInterfacesModel m_interfaces;
    OspfStatus m_status_;
    QString m_routerId;
    QString m_statusText;
    QString m_error;
    bool m_busy = false;
};

class BgpTool : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QAbstractItemModel *peers READ peers CONSTANT)
    Q_PROPERTY(QString routerId READ routerId NOTIFY changed)
    Q_PROPERTY(uint localAs READ localAs NOTIFY changed)
    Q_PROPERTY(int ribEntries READ ribEntries NOTIFY changed)
    Q_PROPERTY(int peerCount READ peerCount NOTIFY changed)
    Q_PROPERTY(int establishedCount READ establishedCount NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString statusText READ statusText NOTIFY changed)
    Q_PROPERTY(QString errorText READ errorText NOTIFY changed)

public:
    explicit BgpTool(UbusClient *client, QObject *parent = nullptr);

    QAbstractItemModel *peers() { return &m_peers; }
    QString routerId() const { return m_summary.routerId; }
    uint localAs() const { return m_summary.localAs; }
    int ribEntries() const { return m_summary.ribEntries; }
    int peerCount() const { return int(m_summary.peers.size()); }
    int establishedCount() const { return m_summary.establishedCount(); }
    bool busy() const { return m_busy; }
    QString statusText() const { return m_status; }
    QString errorText() const { return m_error; }

    Q_INVOKABLE void refresh();
    void reset();
    const BgpSummary &summary() const { return m_summary; }

signals:
    void changed();

private:
    void onSummary(const BgpSummary &summary);
    void onFailed(const Ubus::Error &error);

    UbusClient *m_client = nullptr;
    BgpPeersModel m_peers;
    BgpSummary m_summary;
    QString m_status;
    QString m_error;
    bool m_busy = false;
};

} // namespace Routing
