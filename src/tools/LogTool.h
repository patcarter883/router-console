// SPDX-FileCopyrightText: 2026 the router-console authors
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantList>

#include "protocol/ToolTypes.h"
#include "protocol/UbusTypes.h"

class UbusClient;

// Log window state: a bounded `logread -l N` window polled on a timer, with new
// lines appended by diffing successive windows. Pause freezes both the poller
// and the tail; the filter only hides rows, it never drops them.
class LogTool : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QVariantList entries READ entries NOTIFY changed)
    Q_PROPERTY(bool paused READ isPaused WRITE setPaused NOTIFY changed)
    Q_PROPERTY(QString filter READ filter WRITE setFilter NOTIFY changed)
    Q_PROPERTY(bool follow READ isFollow WRITE setFollow NOTIFY changed)
    Q_PROPERTY(bool busy READ isBusy NOTIFY changed)
    Q_PROPERTY(QString statusText READ statusText NOTIFY changed)
    Q_PROPERTY(int totalCount READ totalCount NOTIFY changed)

public:
    explicit LogTool(UbusClient *client, QObject *parent = nullptr);

    QVariantList entries() const;
    bool isPaused() const { return m_paused; }
    void setPaused(bool paused);
    QString filter() const { return m_filter; }
    void setFilter(const QString &filter);
    bool isFollow() const { return m_follow; }
    void setFollow(bool follow);
    bool isBusy() const { return m_busy; }
    QString statusText() const { return m_status; }
    int totalCount() const { return int(m_entries.size()); }

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void clear();
    void reset();

signals:
    void changed();

private:
    void onLog(const QList<Ubus::LogEntry> &window);
    void onFailed(const Ubus::Error &error);
    QStringList newLines(const QStringList &window) const;
    static QString severityOf(const Ubus::LogEntry &entry);

    UbusClient *m_client = nullptr;
    QTimer m_timer;
    QList<Ubus::LogEntry> m_entries;
    QStringList m_rawWindow;
    QString m_filter;
    QString m_status;
    bool m_paused = false;
    bool m_follow = true;
    bool m_busy = false;
};
