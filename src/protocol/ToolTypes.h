// SPDX-FileCopyrightText: 2026 the router-console authors
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QList>
#include <QMetaType>
#include <QString>
#include <QStringList>

// Typed view of the tool tier (docs/ubus-rpcd-read-surface.md, "Tool tier"):
// exec output, file listings, sysupgrade image checks, conntrack flows and
// traffic-control qdiscs. Everything below is the parser's output; no view ever
// sees raw command text or JSON.
namespace Ubus {

struct ExecResult {
    QString command;
    int code = -1;
    QString stdoutText;
    QString stderrText;

    bool isOk() const { return code == 0; }
};

// --- ping / traceroute -------------------------------------------------------

struct PingReply {
    int sequence = 0;
    QString address;
    double rttMs = 0.0;
};

struct PingReport {
    QString target;
    QList<PingReply> replies;
    int transmitted = 0;
    int received = 0;
    double lossPercent = 0.0;
    double minMs = 0.0;
    double avgMs = 0.0;
    double maxMs = 0.0;
    QString rawText;

    bool answered() const { return received > 0; }
};

struct TracerouteHop {
    int ttl = 0;
    QString address;
    double rttMs = 0.0;
    QString annotation; // !N, !H, ... empty when the hop answered plainly
    bool reached = false;
};

struct TracerouteReport {
    QString target;
    QList<TracerouteHop> hops;
    QString rawText;
};

// --- port scan ---------------------------------------------------------------

struct PortProbe {
    enum class State { Open, Closed, Filtered };

    int port = 0;
    State state = State::Closed;

    QString stateText() const;
};

struct PortScanReport {
    QString target;
    QList<PortProbe> probes;

    int openCount() const;
};

// --- traffic control ---------------------------------------------------------

struct CakeTin {
    int index = 0;
    QString threshold;
    quint64 packets = 0;
    quint64 bytes = 0;
    quint64 drops = 0;
    quint64 marks = 0;
    quint64 backlogBytes = 0;
    double peakDelayUs = 0.0;
    double averageDelayUs = 0.0;
    quint32 quantum = 0;
};

struct QdiscStats {
    QString kind;   // cake, fq_codel, noqueue, ...
    QString handle; // 8005:, ffff:, 0:
    QString device;
    QString detail; // the rest of the qdisc line (bandwidth, isolate, ...)

    quint64 sentBytes = 0;
    quint64 sentPackets = 0;
    quint64 dropped = 0;
    quint64 overlimits = 0;
    quint64 requeues = 0;
    quint64 backlogBytes = 0;

    quint64 memoryUsed = 0;
    quint64 memoryLimit = 0;
    QString capacity;
    QList<CakeTin> tins;

    bool isCake() const { return kind == QLatin1String("cake"); }
};

// --- conntrack ---------------------------------------------------------------

// One /proc/net/nf_conntrack entry. Both directions are kept: the original
// tuple (src/dst as the first packet saw them) and the reply tuple, each with
// its own packet and byte counters.
struct ConntrackFlow {
    QString layer3; // ipv4, ipv6
    QString layer4; // tcp, udp, icmp
    QString state;  // ESTABLISHED, TIME_WAIT, ...

    QString srcAddress;
    QString dstAddress;
    int srcPort = 0;
    int dstPort = 0;

    quint64 packets = 0;
    quint64 bytes = 0;
    quint64 replyPackets = 0;
    quint64 replyBytes = 0;

    bool assured = false;
    bool hasReply = false;

    // Direction-independent identity of the flow.
    QString tupleKey() const;
};

// --- files -------------------------------------------------------------------

struct FileEntry {
    QString name;
    QString path;
    QString type; // file, directory, link
    quint64 size = 0;
    QDateTime modified;
    QString owner;
    QString group;
    QString mode; // octal, e.g. 0644

    bool isDirectory() const { return type == QLatin1String("directory"); }
};

struct FileListing {
    QString path;
    QList<FileEntry> entries;
};

struct SysupgradeCheck {
    bool accepted = false;
    int code = -1;
    QString output;
};

} // namespace Ubus

Q_DECLARE_METATYPE(Ubus::ExecResult)
Q_DECLARE_METATYPE(Ubus::PingReply)
Q_DECLARE_METATYPE(Ubus::PingReport)
Q_DECLARE_METATYPE(Ubus::TracerouteHop)
Q_DECLARE_METATYPE(Ubus::TracerouteReport)
Q_DECLARE_METATYPE(Ubus::PortProbe)
Q_DECLARE_METATYPE(Ubus::PortScanReport)
Q_DECLARE_METATYPE(Ubus::CakeTin)
Q_DECLARE_METATYPE(Ubus::QdiscStats)
Q_DECLARE_METATYPE(QList<Ubus::QdiscStats>)
Q_DECLARE_METATYPE(Ubus::ConntrackFlow)
Q_DECLARE_METATYPE(QList<Ubus::ConntrackFlow>)
Q_DECLARE_METATYPE(Ubus::FileEntry)
Q_DECLARE_METATYPE(Ubus::FileListing)
Q_DECLARE_METATYPE(Ubus::SysupgradeCheck)
