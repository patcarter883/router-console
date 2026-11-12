// SPDX-FileCopyrightText: 2026 the router-console authors
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <QList>
#include <QMetaType>
#include <QString>
#include <QStringList>

// Typed view of the routing surface (docs/frr-routing.md): the kernel routing
// table as FRR reports it, FRR's daemon state, OSPF/BGP protocol state and the
// frr.conf document the config editor edits. Nothing above the protocol layer
// sees vtysh text or JSON.
namespace Routing {

// One candidate of one prefix. `show ip route json` reports every protocol that
// offers a prefix, so a connected and an OSPF entry for the same network are two
// rows; `selected` marks the one installed in the FIB.
struct RouteEntry {
    QString prefix;    // "192.168.77.0/24"
    int prefixLen = 0;
    QString family;    // "ipv4", "ipv6"
    QString protocol;  // connected, static, ospf, bgp, kernel, local
    int distance = 0;
    int metric = 0;
    bool selected = false;
    bool installed = false;
    QString uptime;
    QStringList nexthops; // "via 10.0.2.2 dev eth1" / "directly connected, eth1"
    QString source;       // "frr" or "kernel" (ip -6 route filled the gap)

    bool isConnectedOrLocal() const
    {
        return protocol == QLatin1String("connected") || protocol == QLatin1String("local");
    }
};

struct RouteTable {
    QList<RouteEntry> routes;

    int countFor(const QString &protocol) const
    {
        int count = 0;
        for (const RouteEntry &route : routes) {
            if (route.protocol == protocol)
                ++count;
        }
        return count;
    }
};

// `show daemons` (running) plus /etc/frr/daemons (enabled at start).
struct Daemons {
    bool installed = false;
    QStringList running; // zebra, ospfd, bgpd, staticd, mgmtd, watchfrr
    QStringList enabled; // daemon names whose <name>d=yes flag is set
    QString configText;  // /etc/frr/daemons, for the "not installed" case

    bool isRunning(const QString &daemon) const { return running.contains(daemon); }
    bool ospfRunning() const { return isRunning(QStringLiteral("ospfd")); }
    bool bgpRunning() const { return isRunning(QStringLiteral("bgpd")); }
};

struct OspfNeighbor {
    QString neighborId;
    QString address;
    QString interface;
    QString state; // Full/DR, 2-Way/DROther, ExStart, ...
    int priority = 0;
    QString upTime;
    int deadTimeSeconds = 0;

    bool isFull() const { return state.startsWith(QLatin1String("Full")); }
};

struct OspfInterface {
    QString name;
    QString area;
    QString routerId;
    QString networkType;
    QString state; // DR, Backup, DROther, Point-To-Point
    int cost = 0;
    int priority = 0;
    int neighborCount = 0;
    int adjacentCount = 0;
    bool up = false;
    QString address; // Interface Address
};

// One area block of `show ip ospf database`.
struct OspfAreaDatabase {
    QString area;   // "0.0.0.0"
    QString kind;   // "Router Link States"
    int count = 0;
};

struct OspfStatus {
    QString routerId;
    QList<OspfNeighbor> neighbors;
    QList<OspfInterface> interfaces;
    QList<OspfAreaDatabase> database;

    int fullNeighborCount() const
    {
        int count = 0;
        for (const OspfNeighbor &neighbor : neighbors) {
            if (neighbor.isFull())
                ++count;
        }
        return count;
    }
};

struct BgpPeer {
    QString address;
    quint32 remoteAs = 0;
    quint32 localAs = 0;
    QString state; // Established, Active, Idle, Connect
    QString uptime;
    int pfxRcvd = 0;
    int pfxSnt = 0;
    int msgRcvd = 0;
    int msgSent = 0;

    bool isEstablished() const { return state == QLatin1String("Established"); }
};

struct BgpSummary {
    QString routerId;
    quint32 localAs = 0;
    int ribEntries = 0;
    int peerCount = 0;
    QList<BgpPeer> peers;

    int establishedCount() const
    {
        int count = 0;
        for (const BgpPeer &peer : peers) {
            if (peer.isEstablished())
                ++count;
        }
        return count;
    }
};

// --- frr.conf ---------------------------------------------------------------

// One statement of a config block. FRR statements are "<prefix> <value>": the
// editor shows the prefix as the row label and rewrites "<prefix> <value>", so
// a statement round-trips byte for byte (measured grammar, docs/frr-routing.md).
struct FrrStatement {
    QString keyword; // "ospf router-id", "neighbor 10.99.0.2 remote-as"
    QString value;   // "192.168.77.1", "65001"
    bool duplicate = false; // keyword repeats in this block: shown read-only
};

struct FrrBlock {
    QString header; // "router ospf", "interface br-lan", "global"
    QString type;   // "router ospf", "interface", "global", "line"
    QString name;   // "router bgp 65001" style instance name, informational
    QList<FrrStatement> statements;
};

struct FrrConfig {
    QString rawText;
    QList<FrrBlock> blocks;

    const FrrBlock *block(int index) const
    {
        if (index < 0 || index >= blocks.size())
            return nullptr;
        return &blocks.at(index);
    }
};

// Result of one apply (write + restart + probe).
struct FrrApplyResult {
    bool ok = false;
    int restartCode = -1;
    QString restartOutput;
    QStringList runningDaemons;
};

} // namespace Routing

Q_DECLARE_METATYPE(Routing::RouteEntry)
Q_DECLARE_METATYPE(Routing::RouteTable)
Q_DECLARE_METATYPE(Routing::Daemons)
Q_DECLARE_METATYPE(Routing::OspfNeighbor)
Q_DECLARE_METATYPE(Routing::OspfInterface)
Q_DECLARE_METATYPE(Routing::OspfStatus)
Q_DECLARE_METATYPE(Routing::BgpPeer)
Q_DECLARE_METATYPE(Routing::BgpSummary)
Q_DECLARE_METATYPE(Routing::FrrStatement)
Q_DECLARE_METATYPE(Routing::FrrBlock)
Q_DECLARE_METATYPE(Routing::FrrConfig)
Q_DECLARE_METATYPE(Routing::FrrApplyResult)
