// SPDX-FileCopyrightText: 2026 the router-console authors
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariantList>

#include "protocol/ToolTypes.h"
#include "protocol/UbusTypes.h"

class UbusClient;

// SQM/CAKE stats window state: `tc -s qdisc show` from the router, parsed into
// one row per qdisc, with the cake tins spelled out. Refresh is manual or 1 Hz.
class SqmStatsTool : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QVariantList qdiscs READ qdiscs NOTIFY changed)
    Q_PROPERTY(bool autoRefresh READ isAutoRefresh WRITE setAutoRefresh NOTIFY changed)
    Q_PROPERTY(bool busy READ isBusy NOTIFY changed)
    Q_PROPERTY(QString statusText READ statusText NOTIFY changed)
    Q_PROPERTY(QString cakeSummary READ cakeSummary NOTIFY changed)
    Q_PROPERTY(bool cakeActive READ isCakeActive NOTIFY changed)

public:
    explicit SqmStatsTool(UbusClient *client, QObject *parent = nullptr);

    QVariantList qdiscs() const;
    bool isAutoRefresh() const { return m_autoRefresh; }
    void setAutoRefresh(bool enabled);
    bool isBusy() const { return m_busy; }
    QString statusText() const { return m_status; }
    QString cakeSummary() const { return m_cakeSummary; }
    bool isCakeActive() const { return m_cakeActive; }

    Q_INVOKABLE void refresh();
    void reset();

signals:
    void changed();

private:
    void onQdiscs(const QList<Ubus::QdiscStats> &qdiscs);
    void onFailed(const Ubus::Error &error);

    UbusClient *m_client = nullptr;
    QTimer m_timer;
    QList<Ubus::QdiscStats> m_qdiscs;
    bool m_autoRefresh = false;
    bool m_busy = false;
    bool m_cakeActive = false;
    QString m_status;
    QString m_cakeSummary;
};
