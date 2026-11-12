// SPDX-FileCopyrightText: 2026 the router-console authors
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>

#include "protocol/ToolTypes.h"
#include "protocol/UbusTypes.h"

class UbusClient;

// Port scan window state. The probe runs on the router (`nc` per port, one
// second each), so the answers describe the router's view of the target.
class PortScanTool : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString target READ target WRITE setTarget NOTIFY changed)
    Q_PROPERTY(QString portsText READ portsText WRITE setPortsText NOTIFY changed)
    Q_PROPERTY(bool running READ isRunning NOTIFY changed)
    Q_PROPERTY(QString statusText READ statusText NOTIFY changed)
    Q_PROPERTY(QVariantList results READ results NOTIFY changed)

public:
    explicit PortScanTool(UbusClient *client, QObject *parent = nullptr);

    QString target() const { return m_target; }
    void setTarget(const QString &target);
    QString portsText() const { return m_portsText; }
    void setPortsText(const QString &text);
    bool isRunning() const { return m_running; }
    QString statusText() const { return m_status; }
    QVariantList results() const;

    Q_INVOKABLE void start();
    Q_INVOKABLE void clear();
    void reset();

    // "22,80,443,8000-8010" -> sorted, de-duplicated port list.
    static QList<int> parsePorts(const QString &text);

signals:
    void changed();

private:
    void onFinished(const Ubus::PortScanReport &report);
    void onFailed(const Ubus::Error &error);

    UbusClient *m_client = nullptr;
    QString m_target = QStringLiteral("127.0.0.1");
    QString m_portsText = QStringLiteral("22,53,80,443,8080");
    bool m_running = false;
    QString m_status;
    Ubus::PortScanReport m_report;
};
