// SPDX-FileCopyrightText: 2026 the router-console authors
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ToolCodec.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonValue>
#include <QLocale>
#include <QRegularExpression>

#include <algorithm>

namespace Ubus {

QString PortProbe::stateText() const
{
    switch (state) {
    case State::Open: return QStringLiteral("open");
    case State::Closed: return QStringLiteral("closed");
    case State::Filtered: return QStringLiteral("filtered");
    }
    return QStringLiteral("?");
}

int PortScanReport::openCount() const
{
    return int(std::count_if(probes.cbegin(), probes.cend(), [](const PortProbe &probe) {
        return probe.state == PortProbe::State::Open;
    }));
}

QString ConntrackFlow::tupleKey() const
{
    return QStringLiteral("%1/%2 %3:%4>%5:%6")
        .arg(layer3, layer4, srcAddress)
        .arg(srcPort)
        .arg(dstAddress)
        .arg(dstPort);
}

QString LogEntry::timeText() const
{
    if (!time.isValid())
        return QStringLiteral("-");
    return time.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
}

QString LogEntry::levelText() const
{
    if (facility.isEmpty())
        return level;
    if (level.isEmpty())
        return facility;
    return facility + QLatin1Char('.') + level;
}

QString DhcpLease::kindText() const
{
    switch (kind) {
    case Kind::DynamicV4: return QStringLiteral("dynamic");
    case Kind::DynamicV6: return QStringLiteral("dynamic v6");
    case Kind::Reservation: return QStringLiteral("reservation");
    }
    return QStringLiteral("?");
}

QString DhcpLease::remainingText() const
{
    if (!isDynamic())
        return QStringLiteral("static");
    if (unlimited || remainingSeconds < 0)
        return QStringLiteral("unlimited");
    if (remainingSeconds == 0)
        return QStringLiteral("expired");

    qint64 seconds = remainingSeconds;
    const qint64 days = seconds / 86400;
    seconds %= 86400;
    const qint64 hours = seconds / 3600;
    seconds %= 3600;
    const qint64 minutes = seconds / 60;
    if (days > 0)
        return QStringLiteral("%1d %2h").arg(days).arg(hours);
    if (hours > 0)
        return QStringLiteral("%1h %2m").arg(hours).arg(minutes);
    return QStringLiteral("%1m %2s").arg(minutes).arg(seconds % 60);
}

} // namespace Ubus

namespace Ubus::ToolCodec {
namespace {

QString modeOctal(int mode)
{
    return QStringLiteral("%1").arg(mode & 07777, 4, 8, QLatin1Char('0'));
}

} // namespace

QJsonObject execArgs(const QString &command, const QStringList &params)
{
    QJsonArray array;
    for (const QString &param : params)
        array.append(param);
    return QJsonObject{{QStringLiteral("command"), command},
                       {QStringLiteral("params"), array}};
}

QJsonObject fileListArgs(const QString &path)
{
    return QJsonObject{{QStringLiteral("path"), path}};
}

QJsonObject fileReadArgs(const QString &path)
{
    return QJsonObject{{QStringLiteral("path"), path},
                       {QStringLiteral("base64"), true}};
}

QJsonObject fileWriteArgs(const QString &path, const QByteArray &data)
{
    return QJsonObject{{QStringLiteral("path"), path},
                       {QStringLiteral("data"), QString::fromLatin1(data.toBase64())},
                       {QStringLiteral("base64"), true}};
}

QJsonObject fileRemoveArgs(const QString &path)
{
    return QJsonObject{{QStringLiteral("path"), path}};
}

ExecResult parseExec(const QString &command, const QJsonObject &result)
{
    ExecResult exec;
    exec.command = command;
    exec.code = result.value(QStringLiteral("code")).toInt(-1);
    exec.stdoutText = result.value(QStringLiteral("stdout")).toString();
    exec.stderrText = result.value(QStringLiteral("stderr")).toString();
    return exec;
}

FileListing parseFileListing(const QString &path, const QJsonObject &result)
{
    FileListing listing;
    listing.path = path;

    for (const QJsonValue &value : result.value(QStringLiteral("entries")).toArray()) {
        const QJsonObject entry = value.toObject();
        FileEntry file;
        file.name = entry.value(QStringLiteral("name")).toString();
        if (file.name.isEmpty() || file.name == QLatin1String(".") || file.name == QLatin1String(".."))
            continue;
        file.path = path.endsWith(QLatin1Char('/')) ? path + file.name
                                                    : path + QLatin1Char('/') + file.name;
        file.type = entry.value(QStringLiteral("type")).toString();
        file.size = static_cast<quint64>(entry.value(QStringLiteral("size")).toDouble());
        file.modified = QDateTime::fromSecsSinceEpoch(
            static_cast<qint64>(entry.value(QStringLiteral("mtime")).toDouble()));
        file.owner = entry.value(QStringLiteral("user")).toString();
        file.group = entry.value(QStringLiteral("group")).toString();
        file.mode = modeOctal(entry.value(QStringLiteral("mode")).toInt());
        listing.entries.append(file);
    }

    std::sort(listing.entries.begin(), listing.entries.end(),
              [](const FileEntry &a, const FileEntry &b) {
                  if (a.isDirectory() != b.isDirectory())
                      return a.isDirectory();
                  return a.name.localeAwareCompare(b.name) < 0;
              });
    return listing;
}

QByteArray parseFileData(const QJsonObject &result, bool base64)
{
    const QString data = result.value(QStringLiteral("data")).toString();
    if (base64)
        return QByteArray::fromBase64(data.toLatin1());
    return data.toUtf8();
}

PingReport parsePing(const QString &target, const QString &stdoutText, int requestedCount)
{
    PingReport report;
    report.target = target;
    report.rawText = stdoutText;

    static const QRegularExpression replyPattern(
        QStringLiteral("^(\\d+) bytes from (\\S+): seq=(\\d+)(?: ttl=(\\d+))? time=([\\d.]+) ms"));
    static const QRegularExpression summaryPattern(
        QStringLiteral("(\\d+) packets transmitted, (\\d+) (?:packets )?received, ([\\d.]+)% packet loss"));
    static const QRegularExpression rttPattern(
        QStringLiteral("round-trip min/avg/max = ([\\d.]+)/([\\d.]+)/([\\d.]+)"));

    const QStringList lines = stdoutText.split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        QRegularExpressionMatch match = replyPattern.match(line);
        if (match.hasMatch()) {
            PingReply reply;
            reply.address = match.captured(2);
            reply.sequence = match.captured(3).toInt();
            reply.rttMs = match.captured(5).toDouble();
            report.replies.append(reply);
            continue;
        }
        match = summaryPattern.match(line);
        if (match.hasMatch()) {
            report.transmitted = match.captured(1).toInt();
            report.received = match.captured(2).toInt();
            report.lossPercent = match.captured(3).toDouble();
            continue;
        }
        match = rttPattern.match(line);
        if (match.hasMatch()) {
            report.minMs = match.captured(1).toDouble();
            report.avgMs = match.captured(2).toDouble();
            report.maxMs = match.captured(3).toDouble();
        }
    }

    if (report.transmitted == 0 && requestedCount > 0)
        report.transmitted = requestedCount;
    if (report.received == 0 && !report.replies.isEmpty())
        report.received = int(report.replies.size());
    return report;
}

TracerouteReport parseTraceroute(const QString &target, const QString &stdoutText)
{
    TracerouteReport report;
    report.target = target;
    report.rawText = stdoutText;

    static const QRegularExpression hopPattern(
        QStringLiteral("^\\s*(\\d+)\\s+(\\S+)\\s+(.*)$"));
    static const QRegularExpression rttPattern(QStringLiteral("([\\d.]+) ms"));
    static const QRegularExpression annotationPattern(QStringLiteral("!(\\w+)"));

    for (const QString &line : stdoutText.split(QLatin1Char('\n'))) {
        const QRegularExpressionMatch match = hopPattern.match(line);
        if (!match.hasMatch())
            continue;

        TracerouteHop hop;
        hop.ttl = match.captured(1).toInt();
        hop.address = match.captured(2);
        const QString rest = match.captured(3);

        const QRegularExpressionMatch rtt = rttPattern.match(rest);
        if (rtt.hasMatch())
            hop.rttMs = rtt.captured(1).toDouble();

        const QRegularExpressionMatch annotation = annotationPattern.match(rest);
        if (annotation.hasMatch())
            hop.annotation = annotation.captured(1);

        hop.reached = hop.address != QLatin1String("*") && hop.address != QLatin1String("?")
                      && hop.annotation.isEmpty();
        report.hops.append(hop);
    }
    return report;
}

bool isSafeTarget(const QString &target)
{
    static const QRegularExpression pattern(QStringLiteral("^[A-Za-z0-9_.:-]{1,64}$"));
    return pattern.match(target).hasMatch();
}

QString portScanScript(const QString &target, const QList<int> &ports)
{
    QStringList list;
    for (int port : ports) {
        if (port > 0 && port <= 65535)
            list.append(QString::number(port));
    }
    if (list.isEmpty() || !isSafeTarget(target))
        return {};

    // One second per probe: nc connects, stays connected on an open port, and
    // exits non-zero on a refusal. A port that neither connects nor refuses is
    // reported filtered.
    return QStringLiteral(
               "for p in %1; do\n"
               "  nc %2 \"$p\" </dev/null >/dev/null 2>&1 &\n"
               "  pid=$!\n"
               "  n=0\n"
               "  while kill -0 \"$pid\" 2>/dev/null && [ $n -lt 1 ]; do sleep 1; n=1; done\n"
               "  if kill -0 \"$pid\" 2>/dev/null; then\n"
               "    kill \"$pid\" 2>/dev/null; wait \"$pid\" 2>/dev/null; echo \"$p filtered\"\n"
               "  else\n"
               "    wait \"$pid\"; rc=$?\n"
               "    if [ $rc -eq 0 ]; then echo \"$p open\"; else echo \"$p closed\"; fi\n"
               "  fi\n"
               "done\n")
        .arg(list.join(QLatin1Char(' ')), target);
}

PortScanReport parsePortScan(const QString &target, const QString &stdoutText)
{
    PortScanReport report;
    report.target = target;

    static const QRegularExpression linePattern(QStringLiteral("^\\s*(\\d+)\\s+(open|closed|filtered)\\s*$"));
    for (const QString &line : stdoutText.split(QLatin1Char('\n'))) {
        const QRegularExpressionMatch match = linePattern.match(line);
        if (!match.hasMatch())
            continue;
        PortProbe probe;
        probe.port = match.captured(1).toInt();
        const QString state = match.captured(2);
        if (state == QLatin1String("open"))
            probe.state = PortProbe::State::Open;
        else if (state == QLatin1String("filtered"))
            probe.state = PortProbe::State::Filtered;
        else
            probe.state = PortProbe::State::Closed;
        report.probes.append(probe);
    }
    return report;
}

QList<QdiscStats> parseQdiscs(const QString &tcOutput)
{
    QList<QdiscStats> qdiscs;

    static const QRegularExpression headerPattern(
        QStringLiteral("^qdisc (\\S+) (\\S+): dev (\\S+) (.*)$"));
    static const QRegularExpression sentPattern(
        QStringLiteral("^\\s*Sent (\\d+) bytes (\\d+) pkt \\(dropped (\\d+), overlimits (\\d+) requeues (\\d+)\\)"));
    static const QRegularExpression backlogPattern(
        QStringLiteral("^\\s*backlog (\\S+) (\\d+)p requeues \\d+"));
    static const QRegularExpression memoryPattern(
        QStringLiteral("^\\s*memory used: (\\S+) of (\\S+)"));
    static const QRegularExpression capacityPattern(
        QStringLiteral("^\\s*capacity estimate: (\\S+)"));
    static const QRegularExpression bandwidthPattern(QStringLiteral("bandwidth (\\S+)"));
    static const QRegularExpression tinPattern(QStringLiteral("^\\s+Tin (\\d+)"));
    static const QRegularExpression tinFieldPattern(
        QStringLiteral("^\\s+(thresh|target|interval|pk_delay|av_delay|sp_delay|backlog|pkts|bytes|drops|marks|ack_drop|sp_flows|bk_flows|un_flows|max_len|quantum)\\s+(\\S+)"));

    QdiscStats *current = nullptr;
    CakeTin *tin = nullptr;

    auto parseQuantity = [](const QString &text) -> quint64 {
        // "4Mb", "4450000b", "0b"
        static const QRegularExpression sizePattern(QStringLiteral("^(\\d+)([kKmMgG]?)[bB]?$"));
        const QRegularExpressionMatch match = sizePattern.match(text);
        if (!match.hasMatch())
            return text.toULongLong();
        quint64 value = match.captured(1).toULongLong();
        const QString unit = match.captured(2).toLower();
        if (unit == QLatin1String("k"))
            value *= 1024;
        else if (unit == QLatin1String("m"))
            value *= 1024 * 1024;
        else if (unit == QLatin1String("g"))
            value *= 1024ull * 1024 * 1024;
        return value;
    };

    const QStringList lines = tcOutput.split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        QRegularExpressionMatch match = headerPattern.match(line);
        if (match.hasMatch()) {
            QdiscStats stats;
            stats.kind = match.captured(1);
            stats.handle = match.captured(2);
            stats.device = match.captured(3);
            stats.detail = match.captured(4).trimmed();
            qdiscs.append(stats);
            current = &qdiscs.last();
            tin = nullptr;
            continue;
        }
        if (!current)
            continue;

        match = sentPattern.match(line);
        if (match.hasMatch()) {
            current->sentBytes = match.captured(1).toULongLong();
            current->sentPackets = match.captured(2).toULongLong();
            current->dropped = match.captured(3).toULongLong();
            current->overlimits = match.captured(4).toULongLong();
            current->requeues = match.captured(5).toULongLong();
            continue;
        }
        match = backlogPattern.match(line);
        if (match.hasMatch()) {
            current->backlogBytes = parseQuantity(match.captured(1));
            continue;
        }
        match = memoryPattern.match(line);
        if (match.hasMatch()) {
            current->memoryUsed = parseQuantity(match.captured(1));
            current->memoryLimit = parseQuantity(match.captured(2));
            continue;
        }
        match = capacityPattern.match(line);
        if (match.hasMatch()) {
            current->capacity = match.captured(1);
            continue;
        }
        if (current->detail.isEmpty()) {
            const QRegularExpressionMatch bandwidth = bandwidthPattern.match(line);
            if (bandwidth.hasMatch())
                current->detail = QStringLiteral("bandwidth ") + bandwidth.captured(1);
        }

        match = tinPattern.match(line);
        if (match.hasMatch()) {
            CakeTin entry;
            entry.index = match.captured(1).toInt();
            current->tins.append(entry);
            tin = &current->tins.last();
            continue;
        }
        if (!tin)
            continue;

        match = tinFieldPattern.match(line);
        if (!match.hasMatch())
            continue;
        const QString key = match.captured(1);
        const QString value = match.captured(2);
        if (key == QLatin1String("thresh"))
            tin->threshold = value;
        else if (key == QLatin1String("pkts"))
            tin->packets = value.toULongLong();
        else if (key == QLatin1String("bytes"))
            tin->bytes = value.toULongLong();
        else if (key == QLatin1String("drops"))
            tin->drops = value.toULongLong();
        else if (key == QLatin1String("marks"))
            tin->marks = value.toULongLong();
        else if (key == QLatin1String("backlog"))
            tin->backlogBytes = parseQuantity(value);
        else if (key == QLatin1String("pk_delay"))
            tin->peakDelayUs = value.chopped(2).toDouble();
        else if (key == QLatin1String("av_delay"))
            tin->averageDelayUs = value.chopped(2).toDouble();
        else if (key == QLatin1String("quantum"))
            tin->quantum = value.toUInt();
    }

    return qdiscs;
}

QList<ConntrackFlow> parseConntrack(const QString &nfConntrack)
{
    QList<ConntrackFlow> flows;

    for (const QString &line : nfConntrack.split(QLatin1Char('\n'))) {
        const QString trimmed = line.trimmed();
        if (!trimmed.startsWith(QLatin1String("ipv4")) && !trimmed.startsWith(QLatin1String("ipv6")))
            continue;

        const QStringList tokens = trimmed.split(QRegularExpression(QStringLiteral("\\s+")),
                                                 Qt::SkipEmptyParts);
        if (tokens.size() < 8)
            continue;

        ConntrackFlow flow;
        flow.layer3 = tokens.at(0);
        flow.layer4 = tokens.at(2);
        flow.state = tokens.at(5);

        // Tuples are two runs of key=value fields; everything after the second
        // run (ASSURED, mark=, zone=, use=) is not part of either.
        QList<QMap<QString, QString>> tuples;
        QMap<QString, QString> fields;
        for (int i = 6; i < tokens.size(); ++i) {
            const QString &token = tokens.at(i);
            if (token.startsWith(QLatin1Char('['))) {
                if (token == QLatin1String("[ASSURED]"))
                    flow.assured = true;
                continue;
            }
            const int equals = token.indexOf(QLatin1Char('='));
            if (equals <= 0)
                continue;
            const QString key = token.left(equals);
            if (key == QLatin1String("src") && fields.contains(QStringLiteral("src"))) {
                tuples.append(fields);
                fields.clear();
            } else if (key == QLatin1String("mark") || key == QLatin1String("zone")
                       || key == QLatin1String("use")) {
                continue;
            }
            fields.insert(key, token.mid(equals + 1));
        }
        if (!fields.isEmpty())
            tuples.append(fields);
        if (tuples.isEmpty())
            continue;

        const QMap<QString, QString> &original = tuples.first();
        flow.srcAddress = original.value(QStringLiteral("src"));
        flow.dstAddress = original.value(QStringLiteral("dst"));
        flow.srcPort = original.value(QStringLiteral("sport")).toInt();
        flow.dstPort = original.value(QStringLiteral("dport")).toInt();
        flow.packets = original.value(QStringLiteral("packets")).toULongLong();
        flow.bytes = original.value(QStringLiteral("bytes")).toULongLong();

        if (tuples.size() > 1) {
            const QMap<QString, QString> &reply = tuples.at(1);
            const QString replySrc = reply.value(QStringLiteral("src"));
            // The reply tuple repeats the same two addresses in swapped roles.
            if (replySrc == flow.dstAddress || replySrc.isEmpty()) {
                flow.replyPackets = reply.value(QStringLiteral("packets")).toULongLong();
                flow.replyBytes = reply.value(QStringLiteral("bytes")).toULongLong();
                flow.hasReply = !reply.isEmpty();
            }
        }

        if (flow.srcAddress.isEmpty())
            continue;
        flows.append(flow);
    }
    return flows;
}

SysupgradeCheck parseSysupgradeCheck(const QJsonObject &result)
{
    SysupgradeCheck check;
    check.code = result.value(QStringLiteral("code")).toInt(-1);
    const QString stdoutText = result.value(QStringLiteral("stdout")).toString();
    const QString stderrText = result.value(QStringLiteral("stderr")).toString();
    check.output = (stdoutText + stderrText).trimmed();
    check.accepted = check.code == 0;
    return check;
}

QJsonObject logReadArgs(int lines)
{
    return execArgs(QStringLiteral("/sbin/logread"),
                    {QStringLiteral("-l"), QString::number(qBound(1, lines, 5000))});
}

QList<LogEntry> parseLogEntries(const QString &stdoutText)
{
    // logread prints ctime-style headers ("Thu Nov 12 14:43:32 2026"), then the
    // syslog "<facility>.<level>" token; kernel lines carry it too. Anything that
    // does not match that shape is kept whole as the message.
    static const QRegularExpression headerPattern(
        QStringLiteral("^([a-z][a-z0-9_-]*)\\.(emerg|alert|crit|err|warn|notice|info|debug)$"));
    static const QString timeFormat = QStringLiteral("ddd MMM d HH:mm:ss yyyy");

    QList<LogEntry> entries;
    const QStringList lines = stdoutText.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    entries.reserve(lines.size());

    for (const QString &line : lines) {
        if (line.trimmed().isEmpty())
            continue;

        LogEntry entry;
        entry.raw = line;
        QString rest = line;
        if (line.size() > 24) {
            const QDateTime time = QLocale::c().toDateTime(line.left(24), timeFormat);
            if (time.isValid()) {
                entry.time = time;
                rest = line.mid(25);
            }
        }

        const qsizetype space = rest.indexOf(QLatin1Char(' '));
        const QString first = space < 0 ? rest : rest.left(space);
        const QRegularExpressionMatch match = headerPattern.match(first);
        if (match.hasMatch()) {
            entry.facility = match.captured(1);
            entry.level = match.captured(2);
            rest = space < 0 ? QString() : rest.mid(space + 1);
            const qsizetype colon = rest.indexOf(QStringLiteral(": "));
            if (colon > 0) {
                entry.source = rest.left(colon).trimmed();
                rest = rest.mid(colon + 2);
            }
        }
        entry.message = rest.trimmed();
        entries.append(entry);
    }
    return entries;
}

QList<DhcpLease> parseDhcpLeases(const QJsonObject &result)
{
    auto leaseFrom = [](const QJsonObject &object, DhcpLease::Kind kind) {
        DhcpLease lease;
        lease.kind = kind;
        lease.hostname = object.value(QStringLiteral("hostname")).toString();
        lease.mac = object.value(QStringLiteral("macaddr")).toString();
        lease.duid = object.value(QStringLiteral("duid")).toString();
        lease.interfaceName = object.value(QStringLiteral("interface")).toString();

        if (kind == DhcpLease::Kind::DynamicV6) {
            QStringList addresses;
            const QJsonArray listed = object.value(QStringLiteral("ip6addrs")).toArray();
            for (const QJsonValue &value : listed)
                addresses.append(value.toString());
            if (addresses.isEmpty())
                addresses.append(object.value(QStringLiteral("ip6addr")).toString());
            lease.address = addresses.join(QStringLiteral(", "));
        } else {
            lease.address = object.value(QStringLiteral("ipaddr")).toString();
        }

        // LuCI sends false for "never expires" and the remaining seconds otherwise.
        const QJsonValue expires = object.value(QStringLiteral("expires"));
        if (expires.isBool() && !expires.toBool())
            lease.unlimited = true;
        else if (expires.isDouble())
            lease.remainingSeconds = static_cast<qint64>(expires.toDouble());
        return lease;
    };

    QList<DhcpLease> leases;
    const QJsonArray v4 = result.value(QStringLiteral("dhcp_leases")).toArray();
    for (const QJsonValue &value : v4)
        leases.append(leaseFrom(value.toObject(), DhcpLease::Kind::DynamicV4));
    const QJsonArray v6 = result.value(QStringLiteral("dhcp6_leases")).toArray();
    for (const QJsonValue &value : v6)
        leases.append(leaseFrom(value.toObject(), DhcpLease::Kind::DynamicV6));
    return leases;
}

QList<DhcpLease> parseLeaseHosts(const QJsonObject &result)
{
    QList<DhcpLease> leases;
    const QJsonObject values = result.value(QStringLiteral("values")).toObject();
    for (auto it = values.begin(); it != values.end(); ++it) {
        const QJsonObject section = it.value().toObject();
        DhcpLease lease;
        lease.kind = DhcpLease::Kind::Reservation;
        lease.hostname = section.value(QStringLiteral("name")).toString();
        lease.address = section.value(QStringLiteral("ip")).toString();
        const QJsonValue mac = section.value(QStringLiteral("mac"));
        if (mac.isArray())
            lease.mac = mac.toArray().at(0).toString();
        else
            lease.mac = mac.toString();
        if (lease.hostname.isEmpty())
            lease.hostname = it.key();
        leases.append(lease);
    }
    std::sort(leases.begin(), leases.end(), [](const DhcpLease &a, const DhcpLease &b) {
        return a.address < b.address;
    });
    return leases;
}

QStringList parseConfigNames(const QJsonObject &result)
{
    QStringList names;
    const QJsonArray configs = result.value(QStringLiteral("configs")).toArray();
    for (const QJsonValue &value : configs) {
        const QString name = value.toString();
        if (!name.isEmpty())
            names.append(name);
    }
    names.sort();
    return names;
}

} // namespace Ubus::ToolCodec
