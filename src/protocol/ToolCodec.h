// SPDX-FileCopyrightText: 2026 the router-console authors
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

#include "ToolTypes.h"

// Command output <-> typed structs for the tool tier, plus the argument shapes
// of the rpcd `file` object. Only this translation unit knows the text and JSON
// shapes; callers get Ubus::* structs.
namespace Ubus::ToolCodec {

// rpcd argument objects.
QJsonObject execArgs(const QString &command, const QStringList &params);
QJsonObject fileListArgs(const QString &path);
QJsonObject fileReadArgs(const QString &path);
QJsonObject fileWriteArgs(const QString &path, const QByteArray &data);
QJsonObject fileRemoveArgs(const QString &path);

// `file list` -> entries sorted directories first, then by name.
FileListing parseFileListing(const QString &path, const QJsonObject &result);

// `file read` with base64=1 -> raw bytes. The answer carries only `data`, so the
// caller states how it asked.
QByteArray parseFileData(const QJsonObject &result, bool base64);

// `file exec` -> code/stdout/stderr.
ExecResult parseExec(const QString &command, const QJsonObject &result);

// ping/traceroute stdout.
PingReport parsePing(const QString &target, const QString &stdoutText, int requestedCount);
TracerouteReport parseTraceroute(const QString &target, const QString &stdoutText);

// One probe per port, so an unreachable port cannot hang the scan: nc is given
// one second, then reported filtered. The script only ever contains a validated
// target and digits.
QString portScanScript(const QString &target, const QList<int> &ports);
bool isSafeTarget(const QString &target);
PortScanReport parsePortScan(const QString &target, const QString &stdoutText);

// `tc -s qdisc show`.
QList<QdiscStats> parseQdiscs(const QString &tcOutput);

// /proc/net/nf_conntrack.
QList<ConntrackFlow> parseConntrack(const QString &nfConntrack);

// `rpc-sys upgrade_test` -> the sysupgrade(8) verdict in stdout/stderr.
SysupgradeCheck parseSysupgradeCheck(const QJsonObject &result);

} // namespace Ubus::ToolCodec
