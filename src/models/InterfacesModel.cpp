#include "InterfacesModel.h"

#include <QMetaObject>

#include "protocol/UbusClient.h"

namespace {

QString formatRateText(double bytesPerSecond)
{
    if (bytesPerSecond < 0)
        return QStringLiteral("0 bps");
    const double bits = bytesPerSecond * 8.0;
    if (bits >= 1e9)
        return QStringLiteral("%1 Gbps").arg(bits / 1e9, 0, 'f', 2);
    if (bits >= 1e6)
        return QStringLiteral("%1 Mbps").arg(bits / 1e6, 0, 'f', 2);
    if (bits >= 1e3)
        return QStringLiteral("%1 kbps").arg(bits / 1e3, 0, 'f', 1);
    return QStringLiteral("%1 bps").arg(bits, 0, 'f', 0);
}

QString addressesText(const Ubus::InterfaceInfo &info)
{
    QStringList addresses = info.ipv4Addresses;
    addresses.append(info.ipv6Addresses);
    return addresses.join(QStringLiteral(", "));
}

} // namespace

InterfacesModel::InterfacesModel(UbusClient *client, QObject *parent)
    : QAbstractListModel(parent), m_client(client)
{
    connect(m_client, &UbusClient::interfacesReceived, this,
            &InterfacesModel::onInterfacesReceived);
    connect(m_client, &UbusClient::interfacesFailed, this, &InterfacesModel::onRequestFailed);
    connect(m_client, &UbusClient::deviceStatusReceived, this,
            &InterfacesModel::onDeviceStatusReceived);
    connect(m_client, &UbusClient::deviceStatusFailed, this, &InterfacesModel::onRequestFailed);

    m_timer.setInterval(1000);
    m_timer.setTimerType(Qt::PreciseTimer);
    connect(&m_timer, &QTimer::timeout, this, &InterfacesModel::requestStatus);
}

int InterfacesModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_rows.size();
}

QVariant InterfacesModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
        return {};

    const Row &row = m_rows.at(index.row());
    switch (role) {
    case IfNameRole: return row.info.name;
    case DeviceRole: return row.device;
    case ProtocolRole: return row.info.protocol;
    case UpRole: return row.info.up;
    case AddressesRole: return addressesText(row.info);
    case PresentRole: return row.devicePresent;
    case RxBytesRole: return QVariant::fromValue(row.rxBytes);
    case TxBytesRole: return QVariant::fromValue(row.txBytes);
    case RxRateRole: return row.rxRate;
    case TxRateRole: return row.txRate;
    case RxRateTextRole: return formatRateText(row.rxRate);
    case TxRateTextRole: return formatRateText(row.txRate);
    case RxHistoryRole: {
        QVariantList history;
        history.reserve(row.rxHistory.size());
        for (double value : row.rxHistory)
            history.append(value);
        return history;
    }
    case TxHistoryRole: {
        QVariantList history;
        history.reserve(row.txHistory.size());
        for (double value : row.txHistory)
            history.append(value);
        return history;
    }
    default: return {};
    }
}

QHash<int, QByteArray> InterfacesModel::roleNames() const
{
    return {
        {IfNameRole, "ifName"},
        {DeviceRole, "device"},
        {ProtocolRole, "protocol"},
        {UpRole, "up"},
        {AddressesRole, "addresses"},
        {PresentRole, "present"},
        {RxBytesRole, "rxBytes"},
        {TxBytesRole, "txBytes"},
        {RxRateRole, "rxRate"},
        {TxRateRole, "txRate"},
        {RxRateTextRole, "rxRateText"},
        {TxRateTextRole, "txRateText"},
        {RxHistoryRole, "rxHistory"},
        {TxHistoryRole, "txHistory"},
    };
}

int InterfacesModel::rowFor(const QString &name) const
{
    for (int i = 0; i < m_rows.size(); ++i) {
        if (m_rows.at(i).info.name == name)
            return i;
    }
    // Accept the backing device too (br-lan for the lan interface).
    for (int i = 0; i < m_rows.size(); ++i) {
        if (m_rows.at(i).device == name)
            return i;
    }
    return -1;
}

QVariantMap InterfacesModel::interfaceData(const QString &name) const
{
    const int row = rowFor(name);
    if (row < 0)
        return {};
    QVariantMap values;
    const QHash<int, QByteArray> roles = roleNames();
    for (auto it = roles.begin(); it != roles.end(); ++it)
        values.insert(QString::fromLatin1(it.value()), data(index(row), it.key()));
    return values;
}

void InterfacesModel::start()
{
    if (m_polling)
        return;
    m_polling = true;
    m_statusInFlight = false;
    m_dumpInFlight = true;
    m_ticksSinceDump = 0;
    m_lastSampleMs = -1;
    m_clock.start();

    QMetaObject::invokeMethod(m_client, &UbusClient::fetchInterfaces, Qt::QueuedConnection);
    m_timer.start();
}

void InterfacesModel::stop()
{
    m_polling = false;
    m_timer.stop();
    m_statusInFlight = false;
    m_dumpInFlight = false;
    m_lastSampleMs = -1;

    if (!m_rows.isEmpty()) {
        beginResetModel();
        m_rows.clear();
        endResetModel();
        emit countChanged();
    }
}

void InterfacesModel::requestStatus()
{
    if (!m_polling)
        return;
    // A dump every fifth tick picks up interfaces created from the tree or UCI.
    if (++m_ticksSinceDump >= 5) {
        m_ticksSinceDump = 0;
        if (!m_dumpInFlight) {
            m_dumpInFlight = true;
            QMetaObject::invokeMethod(m_client, &UbusClient::fetchInterfaces,
                                      Qt::QueuedConnection);
        }
    }

    if (m_statusInFlight)
        return;
    m_statusInFlight = true;
    QMetaObject::invokeMethod(m_client, &UbusClient::fetchDeviceStatus, Qt::QueuedConnection);
}

void InterfacesModel::onInterfacesReceived(const QList<Ubus::InterfaceInfo> &interfaces)
{
    m_dumpInFlight = false;
    if (!m_polling)
        return;

    QVector<Row> rows;
    rows.reserve(interfaces.size());
    for (const Ubus::InterfaceInfo &info : interfaces) {
        Row row;
        row.info = info;
        row.device = info.trafficDevice();

        // Keep rates and history for interfaces that were already listed.
        for (const Row &existing : m_rows) {
            if (existing.info.name != info.name)
                continue;
            row.hasCounters = existing.hasCounters;
            row.rxBytes = existing.rxBytes;
            row.txBytes = existing.txBytes;
            row.previousRxBytes = existing.previousRxBytes;
            row.previousTxBytes = existing.previousTxBytes;
            row.rxRate = existing.rxRate;
            row.txRate = existing.txRate;
            row.rxHistory = existing.rxHistory;
            row.txHistory = existing.txHistory;
            break;
        }
        rows.append(row);
    }

    beginResetModel();
    m_rows = std::move(rows);
    endResetModel();
    emit countChanged();
}

void InterfacesModel::onDeviceStatusReceived(const Ubus::DeviceStatusSnapshot &snapshot)
{
    m_statusInFlight = false;
    if (!m_polling || m_rows.isEmpty())
        return;

    const qint64 nowMs = m_clock.elapsed();
    const double elapsedSeconds = m_lastSampleMs >= 0 ? (nowMs - m_lastSampleMs) / 1000.0 : 0.0;
    const bool countersComparable = elapsedSeconds > 0.0;

    for (Row &row : m_rows) {
        const Ubus::DeviceStatus device = snapshot.device(row.device);
        row.devicePresent = device.present;
        if (!device.present)
            continue;

        const quint64 rxBytes = device.counters.rxBytes;
        const quint64 txBytes = device.counters.txBytes;
        double rxRate = 0.0;
        double txRate = 0.0;

        if (row.hasCounters && countersComparable) {
            // A counter that went backwards means the device was recreated.
            if (rxBytes >= row.previousRxBytes)
                rxRate = (rxBytes - row.previousRxBytes) / elapsedSeconds;
            if (txBytes >= row.previousTxBytes)
                txRate = (txBytes - row.previousTxBytes) / elapsedSeconds;
            appendHistory(row, rxRate, txRate);
        } else {
            appendHistory(row, 0.0, 0.0);
        }

        row.hasCounters = true;
        row.rxBytes = rxBytes;
        row.txBytes = txBytes;
        row.previousRxBytes = rxBytes;
        row.previousTxBytes = txBytes;
        row.rxRate = rxRate;
        row.txRate = txRate;
    }

    m_lastSampleMs = nowMs;

    const QModelIndex first = index(0);
    const QModelIndex last = index(m_rows.size() - 1);
    emit dataChanged(first, last,
                     {RxBytesRole, TxBytesRole, RxRateRole, TxRateRole, RxRateTextRole,
                      TxRateTextRole, RxHistoryRole, TxHistoryRole, PresentRole});
    emit sampled();
}

void InterfacesModel::appendHistory(Row &row, double rxRate, double txRate) const
{
    row.rxHistory.append(rxRate);
    row.txHistory.append(txRate);
    if (row.rxHistory.size() > historyPoints)
        row.rxHistory.remove(0, row.rxHistory.size() - historyPoints);
    if (row.txHistory.size() > historyPoints)
        row.txHistory.remove(0, row.txHistory.size() - historyPoints);
}

void InterfacesModel::onRequestFailed(const Ubus::Error &error)
{
    m_statusInFlight = false;
    m_dumpInFlight = false;
    if (!m_polling)
        return;

    if (error.status == Ubus::Status::CertificateRequired)
        return;
    emit pollError(error.message);
}
