// SPDX-FileCopyrightText: 2026 the router-console authors
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>

#include "protocol/ToolTypes.h"
#include "protocol/UbusTypes.h"

class UbusClient;

// Ping window state: a bounded run of ICMP echoes issued by the router itself
// and the parsed replies.
class PingTool : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString target READ target WRITE setTarget NOTIFY changed)
    Q_PROPERTY(int count READ count WRITE setCount NOTIFY changed)
    Q_PROPERTY(bool running READ isRunning NOTIFY changed)
    Q_PROPERTY(QString statusText READ statusText NOTIFY changed)
    Q_PROPERTY(QString summaryText READ summaryText NOTIFY changed)
    Q_PROPERTY(QVariantList replies READ replies NOTIFY changed)

public:
    explicit PingTool(UbusClient *client, QObject *parent = nullptr);

    QString target() const { return m_target; }
    void setTarget(const QString &target);
    int count() const { return m_count; }
    void setCount(int count);
    bool isRunning() const { return m_running; }
    QString statusText() const { return m_status; }
    QString summaryText() const { return m_summary; }
    QVariantList replies() const;

    Q_INVOKABLE void start();
    Q_INVOKABLE void clear();
    void reset();

signals:
    void changed();

private:
    void onFinished(const Ubus::PingReport &report);
    void onFailed(const Ubus::Error &error);

    UbusClient *m_client = nullptr;
    QString m_target = QStringLiteral("10.0.2.3");
    int m_count = 5;
    bool m_running = false;
    QString m_status;
    QString m_summary;
    Ubus::PingReport m_report;
};
