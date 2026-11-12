// SPDX-FileCopyrightText: 2026 the router-console authors
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "FrrCodec.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QStringList>

#include <algorithm>

namespace {

QStringList splitWords(const QString &line)
{
    return line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
}

// "30.538s" -> 30 (dead interval is printed with millisecond precision).
int secondsFromText(const QString &text)
{
    QString digits;
    for (const QChar c : text) {
        if (c.isDigit() || c == QLatin1Char('.'))
            digits.append(c);
        else if (!digits.isEmpty())
            break;
    }
    if (digits.isEmpty())
        return 0;
    return qRound(digits.toDouble());
}

bool isBlockHeader(const QString &firstWord)
{
    static const QSet<QString> keywords{
        QStringLiteral("router"),          QStringLiteral("interface"),
        QStringLiteral("line"),            QStringLiteral("route-map"),
        QStringLiteral("ip"),              QStringLiteral("ipv6"),
        QStringLiteral("vrf"),             QStringLiteral("access-list"),
        QStringLiteral("ipv6-access-list"), QStringLiteral("community-list"),
        QStringLiteral("key"),             QStringLiteral("pbr"),
        QStringLiteral("bfd"),             QStringLiteral("rpki"),
        QStringLiteral("segment-routing"), QStringLiteral("mpls"),
        QStringLiteral("bgp"),             QStringLiteral("table"),
        QStringLiteral("vlan"),            QStringLiteral("zebra"),
    };
    return keywords.contains(firstWord);
}

bool isDaemonName(const QString &name)
{
    static const QSet<QString> daemons{
        QStringLiteral("mgmtd"),   QStringLiteral("zebra"),  QStringLiteral("bgpd"),
        QStringLiteral("ripd"),    QStringLiteral("ripngd"), QStringLiteral("ospfd"),
        QStringLiteral("ospf6d"),  QStringLiteral("isisd"),  QStringLiteral("babeld"),
        QStringLiteral("pimd"),    QStringLiteral("pim6d"),  QStringLiteral("ldpd"),
        QStringLiteral("nhrpd"),   QStringLiteral("eigrpd"), QStringLiteral("sharpd"),
        QStringLiteral("pbrd"),    QStringLiteral("staticd"), QStringLiteral("bfdd"),
        QStringLiteral("fabricd"), QStringLiteral("vrrpd"),  QStringLiteral("pathd"),
    };
    return daemons.contains(name);
}

QString statementType(const QString &header)
{
    const QString first = header.section(QLatin1Char(' '), 0, 0);
    if (first == QLatin1String("interface") || first == QLatin1String("line"))
        return first;
    if (first == QLatin1String("router")) {
        const QString second = header.section(QLatin1Char(' '), 1, 1);
        return second.isEmpty() ? QStringLiteral("router") : QStringLiteral("router %1").arg(second);
    }
    return first;
}

} // namespace

namespace Ubus::FrrCodec {

QList<Routing::RouteEntry> parseRoutes(const QString &jsonText, const QString &family)
{
    QList<Routing::RouteEntry> routes;
    const QJsonDocument doc = QJsonDocument::fromJson(jsonText.toUtf8());
    if (!doc.isObject())
        return routes;

    const QJsonObject table = doc.object();
    for (auto it = table.begin(); it != table.end(); ++it) {
        if (!it.value().isArray())
            continue;
        for (const QJsonValue &value : it.value().toArray()) {
            const QJsonObject entry = value.toObject();
            if (entry.value(QStringLiteral("protocol")).toString().isEmpty())
                continue;

            Routing::RouteEntry route;
            route.prefix = it.key();
            route.prefixLen = entry.value(QStringLiteral("prefixLen")).toInt(-1);
            route.family = family;
            route.protocol = entry.value(QStringLiteral("protocol")).toString();
            route.distance = entry.value(QStringLiteral("distance")).toInt();
            route.metric = entry.value(QStringLiteral("metric")).toInt();
            route.selected = entry.value(QStringLiteral("selected")).toBool();
            route.installed = entry.value(QStringLiteral("installed")).toBool();
            route.uptime = entry.value(QStringLiteral("uptime")).toString();
            route.source = QStringLiteral("frr");

            for (const QJsonValue &nextHop : entry.value(QStringLiteral("nexthops")).toArray()) {
                const QJsonObject hop = nextHop.toObject();
                QString text;
                if (hop.value(QStringLiteral("directlyConnected")).toBool())
                    text = QStringLiteral("directly connected");
                else if (!hop.value(QStringLiteral("ip")).toString().isEmpty())
                    text = QStringLiteral("via %1").arg(hop.value(QStringLiteral("ip")).toString());
                const QString ifname = hop.value(QStringLiteral("interfaceName")).toString();
                if (!ifname.isEmpty())
                    text += text.isEmpty() ? ifname : QStringLiteral(" dev %1").arg(ifname);
                if (!text.isEmpty())
                    route.nexthops.append(text);
            }
            routes.append(route);
        }
    }

    std::sort(routes.begin(), routes.end(),
              [](const Routing::RouteEntry &a, const Routing::RouteEntry &b) {
                  if (a.prefix != b.prefix)
                      return a.prefix < b.prefix;
                  return a.protocol < b.protocol;
              });
    return routes;
}

QList<Routing::RouteEntry> parseKernelRoutes6(const QString &text)
{
    QList<Routing::RouteEntry> routes;
    for (const QString &line : text.split(QLatin1Char('\n'))) {
        const QStringList words = splitWords(line.trimmed());
        if (words.isEmpty())
            continue;

        int index = 0;
        const QString type = words.first();
        if (type == QLatin1String("unreachable") || type == QLatin1String("blackhole")
            || type == QLatin1String("prohibit") || type == QLatin1String("throw")) {
            index = 1;
        }
        if (index >= words.size())
            continue;

        Routing::RouteEntry route;
        route.prefix = words.at(index++) == QLatin1String("default") ? QStringLiteral("::/0")
                                                                     : words.at(index - 1);
        route.family = QStringLiteral("ipv6");
        route.protocol = QStringLiteral("kernel");
        route.source = QStringLiteral("kernel");
        const int slash = route.prefix.indexOf(QLatin1Char('/'));
        if (slash > 0)
            route.prefixLen = route.prefix.mid(slash + 1).toInt();

        QStringList nexthop;
        for (; index < words.size(); ++index) {
            const QString word = words.at(index);
            if (word == QLatin1String("via") && index + 1 < words.size())
                nexthop.append(QStringLiteral("via %1").arg(words.at(++index)));
            else if (word == QLatin1String("dev") && index + 1 < words.size())
                nexthop.append(QStringLiteral("dev %1").arg(words.at(++index)));
            else if (word == QLatin1String("proto") && index + 1 < words.size())
                route.protocol = words.at(++index);
            else if (word == QLatin1String("metric") && index + 1 < words.size())
                route.metric = words.at(++index).toInt();
        }
        route.nexthops.append(nexthop.join(QLatin1Char(' ')));
        routes.append(route);
    }
    return routes;
}

QList<Routing::RouteEntry> mergeKernelRoutes(QList<Routing::RouteEntry> routes,
                                             const QList<Routing::RouteEntry> &kernelRoutes)
{
    QSet<QString> known;
    for (const Routing::RouteEntry &route : routes)
        known.insert(route.prefix + QLatin1Char('|') + route.protocol);

    for (const Routing::RouteEntry &route : kernelRoutes) {
        const QString key = route.prefix + QLatin1Char('|') + route.protocol;
        if (known.contains(key))
            continue;
        known.insert(key);
        routes.append(route);
    }
    return routes;
}

QList<Routing::OspfNeighbor> parseOspfNeighbors(const QString &jsonText)
{
    QList<Routing::OspfNeighbor> neighbors;
    const QJsonDocument doc = QJsonDocument::fromJson(jsonText.toUtf8());
    if (!doc.isObject())
        return neighbors;

    const QJsonObject table = doc.object().value(QStringLiteral("neighbors")).toObject();
    for (auto it = table.begin(); it != table.end(); ++it) {
        for (const QJsonValue &value : it.value().toArray()) {
            const QJsonObject entry = value.toObject();
            Routing::OspfNeighbor neighbor;
            neighbor.neighborId = it.key();
            neighbor.address = entry.value(QStringLiteral("ifaceAddress")).toString();
            neighbor.interface =
                entry.value(QStringLiteral("ifaceName")).toString().section(QLatin1Char(':'), 0, 0);
            neighbor.state = entry.value(QStringLiteral("nbrState")).toString();
            neighbor.priority = entry.value(QStringLiteral("nbrPriority")).toInt();
            neighbor.upTime = entry.value(QStringLiteral("upTime")).toString();
            neighbor.deadTimeSeconds =
                secondsFromText(entry.value(QStringLiteral("deadTime")).toString());
            neighbors.append(neighbor);
        }
    }
    return neighbors;
}

QList<Routing::OspfInterface> parseOspfInterfaces(const QString &jsonText)
{
    QList<Routing::OspfInterface> interfaces;
    const QJsonDocument doc = QJsonDocument::fromJson(jsonText.toUtf8());
    if (!doc.isObject())
        return interfaces;

    const QJsonObject table = doc.object().value(QStringLiteral("interfaces")).toObject();
    for (auto it = table.begin(); it != table.end(); ++it) {
        const QJsonObject entry = it.value().toObject();
        Routing::OspfInterface interface;
        interface.name = it.key();
        interface.area = entry.value(QStringLiteral("area")).toString();
        interface.routerId = entry.value(QStringLiteral("routerId")).toString();
        interface.networkType = entry.value(QStringLiteral("networkType")).toString();
        interface.state = entry.value(QStringLiteral("state")).toString();
        interface.cost = entry.value(QStringLiteral("cost")).toInt();
        interface.priority = entry.value(QStringLiteral("priority")).toInt();
        interface.neighborCount = entry.value(QStringLiteral("nbrCount")).toInt();
        interface.adjacentCount = entry.value(QStringLiteral("nbrAdjacentCount")).toInt();
        interface.up = entry.value(QStringLiteral("ifUp")).toBool();
        const QString address = entry.value(QStringLiteral("ipAddress")).toString();
        const int prefixLen = entry.value(QStringLiteral("ipAddressPrefixlen")).toInt();
        if (!address.isEmpty())
            interface.address = prefixLen > 0 ? QStringLiteral("%1/%2").arg(address).arg(prefixLen)
                                              : address;
        interfaces.append(interface);
    }
    return interfaces;
}

Routing::OspfStatus parseOspfDatabase(const QString &text, Routing::OspfStatus status)
{
    // "Router Link States (Area 0.0.0.0)" opens a block; the first non-empty
    // line after it is the column header, every following non-empty line is an
    // LSA until the next header. Blank lines separate them and are skipped.
    enum class Phase { Idle, SkipHeader, Count };
    Phase phase = Phase::Idle;
    int blockIndex = -1;
    for (const QString &line : text.split(QLatin1Char('\n'))) {
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty())
            continue;

        const int routerIdStart = trimmed.indexOf(QStringLiteral("with ID ("));
        if (status.routerId.isEmpty() && routerIdStart >= 0) {
            const int open = trimmed.indexOf(QLatin1Char('('), routerIdStart);
            const int close = trimmed.indexOf(QLatin1Char(')'), open);
            if (open >= 0 && close > open)
                status.routerId = trimmed.mid(open + 1, close - open - 1);
            continue;
        }

        const int areaOpen = trimmed.lastIndexOf(QStringLiteral("(Area "));
        if (areaOpen >= 0 && trimmed.endsWith(QLatin1Char(')'))) {
            Routing::OspfAreaDatabase area;
            area.kind = trimmed.left(areaOpen).trimmed();
            area.area = trimmed.mid(areaOpen + 6);
            area.area.chop(1);
            status.database.append(area);
            blockIndex = int(status.database.size()) - 1;
            phase = Phase::SkipHeader;
            continue;
        }

        if (phase == Phase::SkipHeader) {
            phase = Phase::Count;
            continue;
        }
        if (phase == Phase::Count && blockIndex >= 0)
            ++status.database[blockIndex].count;
    }
    return status;
}

Routing::BgpSummary parseBgpSummary(const QString &jsonText)
{
    Routing::BgpSummary summary;
    const QJsonDocument doc = QJsonDocument::fromJson(jsonText.toUtf8());
    if (!doc.isObject())
        return summary;

    QJsonObject root = doc.object();
    // FRR 10 keys the answer by address family.
    if (root.contains(QStringLiteral("ipv4Unicast")))
        root = root.value(QStringLiteral("ipv4Unicast")).toObject();
    if (root.contains(QStringLiteral("routerId")))
        summary.routerId = root.value(QStringLiteral("routerId")).toString();
    summary.localAs = quint32(root.value(QStringLiteral("as")).toDouble());
    summary.ribEntries = root.value(QStringLiteral("ribCount")).toInt();
    summary.peerCount = root.value(QStringLiteral("peerCount")).toInt();

    const QJsonObject peers = root.value(QStringLiteral("peers")).toObject();
    for (auto it = peers.begin(); it != peers.end(); ++it) {
        const QJsonObject entry = it.value().toObject();
        Routing::BgpPeer peer;
        peer.address = it.key();
        peer.remoteAs = quint32(entry.value(QStringLiteral("remoteAs")).toDouble());
        peer.localAs = quint32(entry.value(QStringLiteral("localAs")).toDouble());
        peer.state = entry.value(QStringLiteral("state")).toString();
        peer.uptime = entry.value(QStringLiteral("peerUptime")).toString();
        peer.pfxRcvd = entry.value(QStringLiteral("pfxRcd")).toInt();
        peer.pfxSnt = entry.value(QStringLiteral("pfxSnt")).toInt();
        peer.msgRcvd = entry.value(QStringLiteral("msgRcvd")).toInt();
        peer.msgSent = entry.value(QStringLiteral("msgSent")).toInt();
        summary.peers.append(peer);
    }
    return summary;
}

Routing::Daemons parseDaemons(const QString &showDaemonsText, const QString &daemonsFileText)
{
    Routing::Daemons daemons;
    daemons.configText = daemonsFileText;
    daemons.installed = !daemonsFileText.isEmpty();
    for (const QString &word : splitWords(showDaemonsText.trimmed()))
        daemons.running.append(word);

    for (const QString &line : daemonsFileText.split(QLatin1Char('\n'))) {
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty() || trimmed.startsWith(QLatin1Char('#')))
            continue;
        const QString name = trimmed.section(QLatin1Char('='), 0, 0).trimmed();
        const QString value = trimmed.section(QLatin1Char('='), 1, 1).trimmed();
        if (isDaemonName(name) && value == QLatin1String("yes"))
            daemons.enabled.append(name);
    }
    return daemons;
}

Routing::FrrConfig parseConfig(const QString &text)
{
    Routing::FrrConfig config;
    config.rawText = text;

    Routing::FrrBlock global;
    global.header = QStringLiteral("global");
    global.type = QStringLiteral("global");
    global.name = QStringLiteral("global");
    config.blocks.append(global);

    Routing::FrrBlock *current = &config.blocks.first();
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty() || trimmed.startsWith(QLatin1Char('!')))
            continue;

        const bool indented = line.startsWith(QLatin1Char(' ')) || line.startsWith(QLatin1Char('\t'));
        if (!indented && isBlockHeader(trimmed.section(QLatin1Char(' '), 0, 0))) {
            Routing::FrrBlock block;
            block.header = trimmed;
            block.type = statementType(trimmed);
            block.name = trimmed;
            config.blocks.append(block);
            current = &config.blocks.last();
            continue;
        }

        Routing::FrrStatement statement;
        const int lastSpace = trimmed.lastIndexOf(QLatin1Char(' '));
        if (lastSpace <= 0) {
            statement.keyword = trimmed;
        } else {
            statement.keyword = trimmed.left(lastSpace);
            statement.value = trimmed.mid(lastSpace + 1);
        }
        if (!indented)
            current = &config.blocks.first(); // top-level one-liner belongs to global
        current->statements.append(statement);
    }

    for (Routing::FrrBlock &block : config.blocks) {
        QMap<QString, int> seen;
        for (const Routing::FrrStatement &statement : block.statements)
            ++seen[statement.keyword];
        for (Routing::FrrStatement &statement : block.statements)
            statement.duplicate = seen.value(statement.keyword) > 1;
    }
    return config;
}

QString renderConfig(const Routing::FrrConfig &config,
                     const QMap<int, QMap<QString, QString>> &stagedByBlock)
{
    QStringList out;
    for (int index = 0; index < config.blocks.size(); ++index) {
        const Routing::FrrBlock &block = config.blocks.at(index);
        const bool global = block.header == QLatin1String("global");
        if (!global)
            out.append(block.header);

        const QMap<QString, QString> staged = stagedByBlock.value(index);
        QStringList emitted;
        for (const Routing::FrrStatement &statement : block.statements) {
            QString value = statement.value;
            if (staged.contains(statement.keyword))
                value = staged.value(statement.keyword);
            const QString text = value.isEmpty() ? statement.keyword
                                                 : QStringLiteral("%1 %2").arg(statement.keyword, value);
            out.append(global ? text : QStringLiteral(" %1").arg(text));
            emitted.append(statement.keyword);
        }
        // A field the user added and the block did not carry yet.
        for (auto it = staged.begin(); it != staged.end(); ++it) {
            if (emitted.contains(it.key()) || it.value().isEmpty())
                continue;
            const QString text = QStringLiteral("%1 %2").arg(it.key(), it.value());
            out.append(global ? text : QStringLiteral(" %1").arg(text));
        }
        if (!global)
            out.append(QStringLiteral("!"));
    }
    return out.join(QLatin1Char('\n')) + QLatin1Char('\n');
}

} // namespace Ubus::FrrCodec
