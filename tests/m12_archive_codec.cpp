// SPDX-FileCopyrightText: 2026 the router-console authors
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QTextStream>

#include "protocol/ToolCodec.h"
#include "tools/ArchiveCodec.h"

// Codec checks with no router involved:
//  - the .tar.gz container round-trips bytes (text, empty and binary members),
//  - member names that could escape the archive root are rejected,
//  - real `logread` line shapes (captured on the dev VM) parse into typed fields,
//  - the lease JSON shapes LuCI's own consumer reads (40_dhcp.js: ipaddr,
//    ip6addrs, macaddr, hostname, expires, interface) parse without a VM.
int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QTextStream out(stdout);

    int failures = 0;
    auto check = [&](bool condition, const QString &what) {
        if (!condition) {
            ++failures;
            out << "FAILED: " << what << Qt::endl;
        } else {
            out << "ok: " << what << Qt::endl;
        }
    };

    // --- tar.gz container ---------------------------------------------------
    QByteArray binary(4096, Qt::Uninitialized);
    for (int i = 0; i < binary.size(); ++i)
        binary[i] = char(i * 7 + 3);

    QList<Archive::Member> members;
    Archive::Member network;
    network.name = QStringLiteral("etc/config/network");
    network.data = QByteArray("config interface 'lan'\n\toption ifname 'eth0'\n");
    members.append(network);
    Archive::Member empty;
    empty.name = QStringLiteral("etc/config/empty");
    members.append(empty);
    Archive::Member blob;
    blob.name = QStringLiteral("etc/config/binary");
    blob.data = binary;
    members.append(blob);

    const QByteArray archive = Archive::buildTarGz(members);
    check(archive.size() > 512, QStringLiteral("archive is %1 bytes").arg(archive.size()));
    check(static_cast<quint8>(archive[0]) == 0x1F && static_cast<quint8>(archive[1]) == 0x8B,
          QStringLiteral("gzip magic present"));

    QString error;
    const QList<Archive::Member> parsed = Archive::parseTarGz(archive, &error);
    check(parsed.size() == 3, QStringLiteral("3 members parsed (%1)").arg(parsed.size()));
    if (parsed.size() == 3) {
        check(parsed[0].name == QLatin1String("etc/config/network")
                  && parsed[0].data == network.data,
              QStringLiteral("network member round-trips"));
        check(parsed[1].name == QLatin1String("etc/config/empty") && parsed[1].data.isEmpty(),
              QStringLiteral("empty member round-trips"));
        check(parsed[2].data == binary, QStringLiteral("4 KiB binary member round-trips"));
    }

    QString truncationError;
    const QList<Archive::Member> truncated =
        Archive::parseTarGz(archive.left(archive.size() / 2), &truncationError);
    check(truncated.isEmpty(),
          QStringLiteral("truncated archive is rejected (%1)").arg(truncationError));
    QString notGzipError;
    const QList<Archive::Member> notGzip =
        Archive::parseTarGz(QByteArray("not an archive"), &notGzipError);
    check(notGzip.isEmpty(),
          QStringLiteral("non-gzip input is rejected (%1)").arg(notGzipError));

    // --- member names -------------------------------------------------------
    check(Archive::isSafeMemberName(QStringLiteral("etc/config/network")),
          QStringLiteral("plain relative name accepted"));
    for (const QString &bad : {QStringLiteral("../etc/config/network"),
                               QStringLiteral("/etc/config/network"),
                               QStringLiteral("etc/../../etc/shadow"),
                               QStringLiteral("etc//config"),
                               QStringLiteral("")}) {
        check(!Archive::isSafeMemberName(bad),
              QStringLiteral("unsafe name rejected: '%1'").arg(bad));
    }

    // --- logread lines (captured on the dev VM) -----------------------------
    const QString logText = QStringLiteral(
        "Thu Nov 12 14:42:57 2026 kern.notice kernel: [    0.000000] Linux version 6.12.112\n"
        "Thu Nov 12 14:43:32 2026 authpriv.info dropbear[5575]: Exit (root) from "
        "<192.168.77.2:36278>: Disconnect received\n"
        "Thu Nov 12 14:44:00 2026 authpriv.notice dropbear[6223]: Pubkey auth succeeded for "
        "'root' with ssh-ed25519 key SHA256:OLYrs\n"
        "a line without a syslog header\n");
    const QList<Ubus::LogEntry> entries = Ubus::ToolCodec::parseLogEntries(logText);
    check(entries.size() == 4, QStringLiteral("4 log lines parsed (%1)").arg(entries.size()));
    if (entries.size() == 4) {
        check(entries[0].time.isValid() && entries[0].facility == QLatin1String("kern")
                  && entries[0].level == QLatin1String("notice")
                  && entries[0].source == QLatin1String("kernel"),
              QStringLiteral("kernel line: %1 %2").arg(entries[0].timeText(), entries[0].levelText()));
        check(entries[1].facility == QLatin1String("authpriv")
                  && entries[1].level == QLatin1String("info")
                  && entries[1].source == QLatin1String("dropbear[5575]")
                  && entries[1].message.startsWith(QLatin1String("Exit (root)")),
              QStringLiteral("dropbear line: %1 | %2")
                  .arg(entries[1].levelText(), entries[1].message));
        check(entries[3].facility.isEmpty()
                  && entries[3].message == QLatin1String("a line without a syslog header"),
              QStringLiteral("header-less line kept whole: %1").arg(entries[3].message));
    }

    // --- lease JSON (LuCI luci-rpc shape) -----------------------------------
    QJsonObject leaseV4;
    leaseV4[QStringLiteral("ipaddr")] = QStringLiteral("192.168.77.130");
    leaseV4[QStringLiteral("macaddr")] = QStringLiteral("52:54:00:aa:bb:cc");
    leaseV4[QStringLiteral("hostname")] = QStringLiteral("laptop");
    leaseV4[QStringLiteral("interface")] = QStringLiteral("br-lan");
    leaseV4[QStringLiteral("expires")] = 43170;

    QJsonObject leaseV6;
    leaseV6[QStringLiteral("ip6addrs")] = QJsonArray{QStringLiteral("fd00::1/128")};
    leaseV6[QStringLiteral("macaddr")] = QStringLiteral("52:54:00:aa:bb:cc");
    leaseV6[QStringLiteral("hostname")] = QStringLiteral("laptop");
    leaseV6[QStringLiteral("duid")] = QStringLiteral("000100004d6c");
    leaseV6[QStringLiteral("expires")] = false;

    QJsonObject leaseResult;
    leaseResult[QStringLiteral("dhcp_leases")] = QJsonArray{leaseV4};
    leaseResult[QStringLiteral("dhcp6_leases")] = QJsonArray{leaseV6};

    const QList<Ubus::DhcpLease> leases = Ubus::ToolCodec::parseDhcpLeases(leaseResult);
    check(leases.size() == 2, QStringLiteral("2 dynamic leases parsed (%1)").arg(leases.size()));
    if (leases.size() == 2) {
        check(leases[0].address == QLatin1String("192.168.77.130")
                  && leases[0].mac == QLatin1String("52:54:00:aa:bb:cc")
                  && leases[0].hostname == QLatin1String("laptop")
                  && leases[0].interfaceName == QLatin1String("br-lan")
                  && leases[0].remainingText() == QLatin1String("11h 59m"),
              QStringLiteral("v4 lease: %1 %2").arg(leases[0].address, leases[0].remainingText()));
        check(leases[1].kind == Ubus::DhcpLease::Kind::DynamicV6
                  && leases[1].address == QLatin1String("fd00::1/128")
                  && leases[1].remainingText() == QLatin1String("unlimited"),
              QStringLiteral("v6 lease: %1 %2").arg(leases[1].address, leases[1].remainingText()));
    }

    // `uci get {config:"dhcp", type:"host"}` shape, as the rpcd uci object answers.
    QJsonObject reservation;
    reservation[QStringLiteral(".type")] = QStringLiteral("host");
    reservation[QStringLiteral(".name")] = QStringLiteral("rcm12lab");
    reservation[QStringLiteral("name")] = QStringLiteral("rc-m12-reserved");
    reservation[QStringLiteral("ip")] = QStringLiteral("192.168.77.240");
    reservation[QStringLiteral("mac")] = QJsonArray{QStringLiteral("52:54:00:12:34:56")};

    QJsonObject values;
    values[QStringLiteral("rcm12lab")] = reservation;

    QJsonObject hostResult;
    hostResult[QStringLiteral("values")] = values;
    const QList<Ubus::DhcpLease> reserved = Ubus::ToolCodec::parseLeaseHosts(hostResult);
    check(reserved.size() == 1 && reserved[0].kind == Ubus::DhcpLease::Kind::Reservation
              && reserved[0].address == QLatin1String("192.168.77.240")
              && reserved[0].mac == QLatin1String("52:54:00:12:34:56")
              && reserved[0].remainingText() == QLatin1String("static"),
          QStringLiteral("reservation parsed: %1 %2 %3")
              .arg(reserved.isEmpty() ? QStringLiteral("-") : reserved[0].hostname,
                   reserved.isEmpty() ? QStringLiteral("-") : reserved[0].address,
                   reserved.isEmpty() ? QStringLiteral("-") : reserved[0].remainingText()));

    // --- uci configs --------------------------------------------------------
    const QJsonObject configsResult{
        {QStringLiteral("configs"),
         QJsonArray{QStringLiteral("network"), QStringLiteral("dhcp"), QStringLiteral("system")}}};
    const QStringList configs = Ubus::ToolCodec::parseConfigNames(configsResult);
    check(configs == QStringList{QStringLiteral("dhcp"), QStringLiteral("network"),
                                 QStringLiteral("system")},
          QStringLiteral("config names sorted: %1").arg(configs.join(QLatin1Char(' '))));

    if (failures == 0) {
        out << "PASS: archive container, log parsing and lease/config shapes verified" << Qt::endl;
        return 0;
    }
    out << failures << " check(s) failed" << Qt::endl;
    return 1;
}
