// SPDX-FileCopyrightText: 2026 the router-console authors
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "LogTool.h"

#include "ToolFormat.h"
#include "protocol/UbusClient.h"

namespace {

// Window size per poll: enough to cover a burst of boot output, small enough
// that a 2 s poll stays a single short exec reply (~20 KB at 200 lines).
constexpr int kWindowLines = 500;
constexpr int kMaxEntries = 5000;

} // namespace

LogTool::LogTool(UbusClient *client, QObject *parent) : QObject(parent), m_client(client)
{
    connect(m_client, &UbusClient::logReceived, this, &LogTool::onLog);
    connect(m_client, &UbusClient::logFailed, this, &LogTool::onFailed);
    m_timer.setInterval(2000);
    connect(&m_timer, &QTimer::timeout, this, &LogTool::refresh);
}

QVariantList LogTool::entries() const
{
    QVariantList rows;
    rows.reserve(m_entries.size());
    const QString needle = m_filter.trimmed().toLower();

    for (const Ubus::LogEntry &entry : m_entries) {
        if (!needle.isEmpty()) {
            const QString haystack = (entry.levelText() + QLatin1Char(' ') + entry.source
                                      + QLatin1Char(' ') + entry.message)
                                         .toLower();
            if (!haystack.contains(needle))
                continue;
        }
        rows.append(QVariantMap{
            {QStringLiteral("time"), entry.timeText()},
            {QStringLiteral("level"), entry.levelText()},
            {QStringLiteral("severity"), severityOf(entry)},
            {QStringLiteral("source"), entry.source},
            {QStringLiteral("message"), entry.message},
            {QStringLiteral("raw"), entry.raw}});
    }
    return rows;
}

QString LogTool::severityOf(const Ubus::LogEntry &entry)
{
    const QString level = entry.level;
    if (level == QLatin1String("emerg") || level == QLatin1String("alert")
        || level == QLatin1String("crit") || level == QLatin1String("err"))
        return QStringLiteral("err");
    if (level == QLatin1String("warn") || level == QLatin1String("notice"))
        return QStringLiteral("warn");
    if (level == QLatin1String("debug"))
        return QStringLiteral("debug");
    return QStringLiteral("info");
}

void LogTool::setPaused(bool paused)
{
    if (m_paused == paused)
        return;
    m_paused = paused;
    if (paused)
        m_timer.stop();
    else
        m_timer.start();
    m_status = paused ? QStringLiteral("paused") : QStringLiteral("following logread");
    emit changed();
    if (!paused)
        refresh();
}

void LogTool::setFilter(const QString &filter)
{
    if (m_filter == filter)
        return;
    m_filter = filter;
    emit changed();
}

void LogTool::setFollow(bool follow)
{
    if (m_follow == follow)
        return;
    m_follow = follow;
    emit changed();
}

void LogTool::refresh()
{
    if (m_paused || m_busy)
        return;
    m_busy = true;
    Tools::onClient(m_client, [client = m_client] { client->fetchLog(kWindowLines); });
}

void LogTool::clear()
{
    m_entries.clear();
    m_rawWindow.clear();
    m_status = QStringLiteral("cleared (%1 s poll window)").arg(m_timer.interval() / 1000);
    emit changed();
}

void LogTool::reset()
{
    m_timer.stop();
    m_entries.clear();
    m_rawWindow.clear();
    m_busy = false;
    m_paused = false;
    m_filter.clear();
    m_status.clear();
    emit changed();
}

QStringList LogTool::newLines(const QStringList &window) const
{
    if (m_rawWindow.isEmpty())
        return window;

    // The router has no sequence numbers, so the new lines are the window tail
    // after the largest overlap with the previous window. A window that is
    // entirely new (log rolled, or a long gap) is taken whole.
    const qsizetype maxOverlap = qMin(m_rawWindow.size(), window.size());
    for (qsizetype overlap = maxOverlap; overlap > 0; --overlap) {
        if (m_rawWindow.mid(m_rawWindow.size() - overlap) == window.mid(0, overlap))
            return window.mid(overlap);
    }
    return window;
}

void LogTool::onLog(const QList<Ubus::LogEntry> &window)
{
    m_busy = false;
    if (window.isEmpty()) {
        m_status = QStringLiteral("log is empty");
        emit changed();
        return;
    }

    QStringList raw;
    raw.reserve(window.size());
    for (const Ubus::LogEntry &entry : window)
        raw.append(entry.raw);

    const QStringList fresh = newLines(raw);
    for (const Ubus::LogEntry &entry : window.mid(window.size() - fresh.size()))
        m_entries.append(entry);
    while (m_entries.size() > kMaxEntries)
        m_entries.removeFirst();

    m_rawWindow = raw;
    m_status = QStringLiteral("%1 entries, %2 lines window, %3 s poll")
                   .arg(m_entries.size())
                   .arg(raw.size())
                   .arg(m_timer.interval() / 1000);

    if (!m_timer.isActive() && !m_paused)
        m_timer.start();
    emit changed();
}

void LogTool::onFailed(const Ubus::Error &error)
{
    m_busy = false;
    m_status = QStringLiteral("logread failed: %1").arg(error.message);
    emit changed();
}
