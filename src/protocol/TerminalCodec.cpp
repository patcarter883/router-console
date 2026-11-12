// SPDX-FileCopyrightText: 2026 the router-console authors
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "TerminalCodec.h"

#include <QJsonArray>
#include <QStringList>

namespace Ubus::TerminalCodec {

namespace {

QString shellQuote(const QString &text)
{
    QString quoted = text;
    quoted.replace(QLatin1Char('\''), QLatin1String("'\\''"));
    return QLatin1Char('\'') + quoted + QLatin1Char('\'');
}

} // namespace

QJsonObject execArgs(const QString &command, const QString &cwd)
{
    // The command text sits ungrouped between the cd and the marker so heredocs,
    // multi-line input and comment-only lines parse as typed.
    const QString frame = QStringLiteral("cd -- %1 2>/dev/null || cd /\n"
                                         "%2\n"
                                         "__rc_term_rc=$?\n"
                                         "printf '\\n%3%s %s\\n' \"$__rc_term_rc\" \"$PWD\"\n")
                              .arg(shellQuote(cwd.isEmpty() ? QStringLiteral("/") : cwd), command,
                                   kMarkerPrefix.toString());

    return QJsonObject{{QStringLiteral("command"), QStringLiteral("/bin/sh")},
                       {QStringLiteral("params"), QJsonArray{QStringLiteral("-c"), frame}}};
}

TerminalResult parse(const QString &command, const QJsonObject &reply, const QString &previousCwd)
{
    TerminalResult result;
    result.command = command;
    result.code = reply.value(QStringLiteral("code")).toInt(-1);
    result.stdoutText = reply.value(QStringLiteral("stdout")).toString();
    result.stderrText = reply.value(QStringLiteral("stderr")).toString();
    result.cwd = previousCwd;

    const qsizetype markerAt = result.stdoutText.lastIndexOf(QLatin1Char('\n') + kMarkerPrefix);
    if (markerAt < 0)
        return result;

    const QString marker = result.stdoutText.mid(markerAt + 1 + kMarkerPrefix.size());
    result.stdoutText.truncate(markerAt);

    const qsizetype space = marker.indexOf(QLatin1Char(' '));
    if (space > 0) {
        bool ok = false;
        const int status = marker.left(space).toInt(&ok);
        if (ok) {
            result.code = status;
            result.statusKnown = true;
            const QString cwd = marker.mid(space + 1).trimmed();
            if (!cwd.isEmpty())
                result.cwd = cwd;
        }
    }
    return result;
}

} // namespace Ubus::TerminalCodec
