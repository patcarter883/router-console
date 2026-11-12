// SPDX-FileCopyrightText: 2026 the router-console authors
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <QList>
#include <QMap>
#include <QString>

#include "RoutingTypes.h"

// vtysh output -> typed structs (docs/frr-routing.md). `show ... json` shapes
// were measured on FRR 10.4.1 from the OpenWrt 25.12.6 packages; the text
// parsers cover the two surfaces that have no JSON (`show daemons`,
// `show ip ospf database`, `ip -6 route`) and the frr.conf grammar.
namespace Ubus::FrrCodec {

// `show ip route json` / `show ipv6 route json`: one row per (prefix, protocol).
QList<Routing::RouteEntry> parseRoutes(const QString &jsonText, const QString &family);

// `ip -6 route`: kernel routes FRR does not carry (unreachable, local policy).
QList<Routing::RouteEntry> parseKernelRoutes6(const QString &text);

// Kernel routes appended to the FRR table, skipping prefix+protocol that FRR
// already reports.
QList<Routing::RouteEntry> mergeKernelRoutes(QList<Routing::RouteEntry> routes,
                                             const QList<Routing::RouteEntry> &kernelRoutes);

// `show ip ospf neighbor json`.
QList<Routing::OspfNeighbor> parseOspfNeighbors(const QString &jsonText);

// `show ip ospf interface json`.
QList<Routing::OspfInterface> parseOspfInterfaces(const QString &jsonText);

// `show ip ospf database` (text): area blocks and their entry counts.
Routing::OspfStatus parseOspfDatabase(const QString &text, Routing::OspfStatus status);

// `show bgp summary json` (ipv4 unicast view).
Routing::BgpSummary parseBgpSummary(const QString &jsonText);

// `show daemons` + /etc/frr/daemons.
Routing::Daemons parseDaemons(const QString &showDaemonsText, const QString &daemonsFileText);

// frr.conf <-> typed blocks. A statement is "<keyword> <value>"; the last
// whitespace-separated token is the value, so editing one statement never
// rewrites its neighbours.
Routing::FrrConfig parseConfig(const QString &text);
QString renderConfig(const Routing::FrrConfig &config,
                     const QMap<int, QMap<QString, QString>> &stagedByBlock);

} // namespace Ubus::FrrCodec
