// SPDX-FileCopyrightText: 2026 the router-console authors
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>

#include "protocol/ToolTypes.h"
#include "protocol/UbusTypes.h"

class UbusClient;

// Traceroute window state: one column per hop as the router's traceroute saw it.
class TracerouteTool : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString target READ target WRITE setTarget NOTIFY changed)
    Q_PROPERTY(int maxHops READ maxHops WRITE setMaxHops NOTIFY changed)
    Q_PROPERTY(bool running READ isRunning NOTIFY changed)
    Q_PROPERTY(QString statusText READ statusText NOTIFY changed)
    Q_PROPERTY(QVariantList hops READ hops NOTIFY changed)

public:
    explicit TracerouteTool(UbusClient *client, QObject *parent = nullptr);

    QString target() const { return m_target; }
    void setTarget(const QString &target);
    int maxHops() const { return m_maxHops; }
    void setMaxHops(int hops);
    bool isRunning() const { return m_running; }
    QString statusText() const { return m_status; }
    QVariantList hops() const;

    Q_INVOKABLE void start();
    Q_INVOKABLE void clear();
    void reset();

signals:
    void changed();

private:
    void onFinished(const Ubus::TracerouteReport &report);
    void onFailed(const Ubus::Error &error);

    UbusClient *m_client = nullptr;
    QString m_target = QStringLiteral("10.0.2.3");
    int m_maxHops = 12;
    bool m_running = false;
    QString m_status;
    Ubus::TracerouteReport m_report;
};
