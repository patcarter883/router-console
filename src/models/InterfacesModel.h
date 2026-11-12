#pragma once

#include <QAbstractListModel>
#include <QElapsedTimer>
#include <QVariantMap>
#include <QTimer>
#include <QVector>

#include "protocol/UbusTypes.h"

class UbusClient;

// Interfaces view model: one row per network.interface.dump entry, refreshed
// from network.device.status once per second. All requests are queued onto the
// ubus client's thread; this object and its timer stay on the UI thread, and
// QML reads rates and rolling history as roles.
class InterfacesModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    enum Role {
        IfNameRole = Qt::UserRole + 1,
        DeviceRole,
        ProtocolRole,
        UpRole,
        AddressesRole,
        PresentRole,
        RxBytesRole,
        TxBytesRole,
        RxRateRole,
        TxRateRole,
        RxRateTextRole,
        TxRateTextRole,
        RxHistoryRole,
        TxHistoryRole,
    };

    explicit InterfacesModel(UbusClient *client, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // Row index for an interface name, -1 when absent.
    Q_INVOKABLE int rowFor(const QString &name) const;
    // All roles of one row, keyed by role name; {} when the name is absent.
    Q_INVOKABLE QVariantMap interfaceData(const QString &name) const;

    void start();
    void stop();
    bool isPolling() const { return m_polling; }

    static constexpr int historyPoints = 120;

signals:
    void countChanged();
    // One device-status sample has been applied; used by the traffic test.
    void sampled();
    void pollError(const QString &message);

private:
    struct Row {
        Ubus::InterfaceInfo info;
        QString device;
        bool devicePresent = false;
        bool hasCounters = false;
        quint64 rxBytes = 0;
        quint64 txBytes = 0;
        quint64 previousRxBytes = 0;
        quint64 previousTxBytes = 0;
        double rxRate = 0;
        double txRate = 0;
        QVector<double> rxHistory;
        QVector<double> txHistory;
    };

    void onInterfacesReceived(const QList<Ubus::InterfaceInfo> &interfaces);
    void onDeviceStatusReceived(const Ubus::DeviceStatusSnapshot &snapshot);
    void onRequestFailed(const Ubus::Error &error);
    void requestStatus();
    void appendHistory(Row &row, double rxRate, double txRate) const;

    UbusClient *m_client = nullptr;
    QTimer m_timer;
    QElapsedTimer m_clock;
    QVector<Row> m_rows;
    qint64 m_lastSampleMs = -1;
    bool m_polling = false;
    bool m_statusInFlight = false;
    bool m_dumpInFlight = false;
    int m_ticksSinceDump = 0;
};
