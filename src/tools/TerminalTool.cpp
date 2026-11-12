// SPDX-FileCopyrightText: 2026 the router-console authors
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "TerminalTool.h"

#include <QStringList>

#include "ToolFormat.h"
#include "protocol/UbusClient.h"

namespace {

// Scrollback bound: a command can print thousands of lines and the view keeps
// the tail, so a long session cannot grow without limit.
constexpr int kMaxLines = 4000;
constexpr int kMaxHistory = 200;

QString exitText(const Ubus::TerminalResult &result, qint64 elapsedMs)
{
    if (!result.statusKnown)
        return QStringLiteral("[exit %1 · shell ended before the status marker · %2 ms]")
            .arg(result.code)
            .arg(elapsedMs);
    return QStringLiteral("[exit %1 · %2 ms]").arg(result.code).arg(elapsedMs);
}

} // namespace

TerminalTool::TerminalTool(UbusClient *client, QObject *parent) : QObject(parent), m_client(client)
{
    connect(m_client, &UbusClient::terminalFinished, this, &TerminalTool::onFinished);
    connect(m_client, &UbusClient::terminalFailed, this, &TerminalTool::onFailed);
}

QString TerminalTool::prompt() const
{
    const QString host = m_hostname.isEmpty() ? QStringLiteral("router") : m_hostname;
    return QStringLiteral("root@%1:%2#").arg(host, m_cwd);
}

void TerminalTool::setHostname(const QString &hostname)
{
    if (m_hostname == hostname)
        return;
    m_hostname = hostname;
    append(QStringLiteral("note"),
           QStringLiteral("command-at-a-time shell on %1 over rpcd file exec: no tty, no stdin, "
                          "one /bin/sh -c per Enter")
               .arg(m_hostname.isEmpty() ? QStringLiteral("the router") : m_hostname));
    emit changed();
}

void TerminalTool::run(const QString &command)
{
    const QString text = command.trimmed();
    if (m_busy || text.isEmpty())
        return;

    m_busy = true;
    if (m_history.isEmpty() || m_history.last() != text)
        m_history.append(text);
    while (m_history.size() > kMaxHistory)
        m_history.removeFirst();

    append(QStringLiteral("prompt"), prompt() + QLatin1Char(' ') + text);
    m_status = QStringLiteral("running");
    m_started.start();
    emit changed();

    Tools::onClient(m_client,
                    [client = m_client, text, cwd = m_cwd] { client->terminalExec(text, cwd); });
}

void TerminalTool::clear()
{
    m_lines.clear();
    m_status = QStringLiteral("cleared");
    emit changed();
}

void TerminalTool::reset()
{
    m_lines.clear();
    m_history.clear();
    m_busy = false;
    m_lastExitCode = 0;
    m_cwd = QStringLiteral("/");
    m_status.clear();
    emit changed();
}

void TerminalTool::onFinished(const Ubus::TerminalResult &result)
{
    m_busy = false;
    if (!result.cwd.isEmpty())
        m_cwd = result.cwd;
    m_lastExitCode = result.code;

    appendBlock(QStringLiteral("out"), result.stdoutText);
    appendBlock(QStringLiteral("err"), result.stderrText);

    const qint64 elapsed = m_started.isValid() ? m_started.elapsed() : 0;
    append(result.code == 0 ? QStringLiteral("done") : QStringLiteral("failed"),
           exitText(result, elapsed));
    m_status = QStringLiteral("exit %1 · %2 ms").arg(result.code).arg(elapsed);
    emit changed();
}

void TerminalTool::onFailed(const Ubus::Error &error)
{
    m_busy = false;
    append(QStringLiteral("err"), error.message);
    m_status = QStringLiteral("no answer: %1").arg(error.message);
    emit changed();
}

void TerminalTool::append(const QString &kind, const QString &text)
{
    m_lines.append(QVariantMap{{QStringLiteral("kind"), kind}, {QStringLiteral("text"), text}});
    while (m_lines.size() > kMaxLines)
        m_lines.removeFirst();
}

// Output keeps its own line breaks; a trailing newline does not add an empty
// row, and text without one stays visible as it was printed.
void TerminalTool::appendBlock(const QString &kind, const QString &text)
{
    if (text.isEmpty())
        return;
    QStringList rows = text.split(QLatin1Char('\n'));
    if (rows.size() > 1 && rows.last().isEmpty())
        rows.removeLast();
    for (const QString &row : rows)
        append(kind, row);
}
