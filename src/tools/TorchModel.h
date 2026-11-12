// SPDX-FileCopyrightText: 2026 the router-console authors
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <QAbstractListModel>
#include <QElapsedTimer>
#include <QHash>
#include <QStringList>
#include <QTimer>
#include <QVector>

#include "protocol/ToolTypes.h"
#include "protocol/UbusTypes.h"

class UbusClient;

// Torch view: per-remote-host bandwidth, attributed from conntrack tuple deltas.
// One row per peer address the router is talking to, refreshed once per second;
// direction is resolved against the router's own interface addresses, so a flow
// counts as rx when the peer sends and tx when the router sends.
class TorchModel : public QAbstractListModel
{
    Q_OBJECT

    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(bool sampling READ isSampling NOTIFY samplingChanged)
    Q_PROPERTY(QString totalRxText READ totalRxText NOTIFY sampled)
    Q_PROPERTY(QString totalTxText READ totalTxText NOTIFY sampled)
    Q_PROPERTY(QString statusText READ statusText NOTIFY sampled)
    Q_PROPERTY(int flowCount READ flowCount NOTIFY sampled)

public:
    enum Role {
        AddressRole = Qt::UserRole + 1,
        RxRateRole,
        TxRateRole,
        RxRateTextRole,
        TxRateTextRole,
        RxBytesRole,
        TxBytesRole,
        ConnectionsRole,
        LastSeenRole,
        ActiveRole,
    };

    explicit TorchModel(UbusClient *client, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    bool isSampling() const { return m_sampling; }
    QString totalRxText() const;
    QString totalTxText() const;
    QString statusText() const { return m_status; }
    int flowCount() const { return m_flowCount; }
    int sampleCount() const { return m_samples; }

    Q_INVOKABLE int rowFor(const QString &address) const;
    Q_INVOKABLE double rateFor(const QString &address) const;
    Q_INVOKABLE void clear();

    Q_INVOKABLE void start();
    Q_INVOKABLE void stop();

signals:
    void countChanged();
    void samplingChanged();
    void sampled();
    void pollError(const QString &message);

private:
    struct HostRow {
        QString address;
        quint64 rxBytes = 0;
        quint64 txBytes = 0;
        double rxRate = 0.0;
        double txRate = 0.0;
        int connections = 0;
        qint64 lastSeenMs = 0;
        bool active = false;
    };

    void requestSample();
    void onLocalAddresses(const QStringList &addresses);
    void onConntrack(const QList<Ubus::ConntrackFlow> &flows);
    void onError(const Ubus::Error &error);
    bool isLocal(const QString &address) const;
    void setStatus(const QString &status);

    UbusClient *m_client = nullptr;
    QTimer m_timer;
    QElapsedTimer m_clock;

    QStringList m_localAddresses;
    bool m_haveAddresses = false;
    bool m_sampling = false;
    bool m_inFlight = false;
    qint64 m_lastSampleMs = -1;
    int m_samples = 0;
    int m_flowCount = 0;
    QString m_status;

    QVector<HostRow> m_rows;
    QHash<QString, QPair<quint64, quint64>> m_previous;
    double m_totalRxRate = 0.0;
    double m_totalTxRate = 0.0;
};
