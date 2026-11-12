// SPDX-FileCopyrightText: 2026 the router-console authors
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "TorchModel.h"

#include <QMetaObject>

#include <algorithm>

#include "ToolFormat.h"
#include "protocol/UbusClient.h"

namespace {

constexpr qint64 kIdleRetentionMs = 15000;

} // namespace

TorchModel::TorchModel(UbusClient *client, QObject *parent)
    : QAbstractListModel(parent), m_client(client)
{
    connect(m_client, &UbusClient::conntrackReceived, this, &TorchModel::onConntrack);
    connect(m_client, &UbusClient::conntrackFailed, this, &TorchModel::onError);
    connect(m_client, &UbusClient::localAddressesReceived, this, &TorchModel::onLocalAddresses);

    m_timer.setInterval(1000);
    m_timer.setTimerType(Qt::PreciseTimer);
    connect(&m_timer, &QTimer::timeout, this, &TorchModel::requestSample);
}

int TorchModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_rows.size();
}

QVariant TorchModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
        return {};

    const HostRow &row = m_rows.at(index.row());
    switch (role) {
    case AddressRole: return row.address;
    case RxRateRole: return row.rxRate;
    case TxRateRole: return row.txRate;
    case RxRateTextRole: return Tools::rateText(row.rxRate);
    case TxRateTextRole: return Tools::rateText(row.txRate);
    case RxBytesRole: return QVariant::fromValue(row.rxBytes);
    case TxBytesRole: return QVariant::fromValue(row.txBytes);
    case ConnectionsRole: return row.connections;
    case LastSeenRole: {
        if (row.lastSeenMs <= 0)
            return QStringLiteral("-");
        const qint64 ageSeconds = (m_clock.elapsed() - row.lastSeenMs) / 1000;
        return ageSeconds <= 0 ? QStringLiteral("now")
                               : QStringLiteral("%1 s ago").arg(ageSeconds);
    }
    case ActiveRole: return row.active;
    default: return {};
    }
}

QHash<int, QByteArray> TorchModel::roleNames() const
{
    return {
        {AddressRole, "address"},
        {RxRateRole, "rxRate"},
        {TxRateRole, "txRate"},
        {RxRateTextRole, "rxRateText"},
        {TxRateTextRole, "txRateText"},
        {RxBytesRole, "rxBytes"},
        {TxBytesRole, "txBytes"},
        {ConnectionsRole, "connections"},
        {LastSeenRole, "lastSeen"},
        {ActiveRole, "isActive"},
    };
}

QString TorchModel::totalRxText() const
{
    return Tools::rateText(m_totalRxRate);
}

QString TorchModel::totalTxText() const
{
    return Tools::rateText(m_totalTxRate);
}

int TorchModel::rowFor(const QString &address) const
{
    for (int i = 0; i < m_rows.size(); ++i) {
        if (m_rows.at(i).address == address)
            return i;
    }
    return -1;
}

double TorchModel::rateFor(const QString &address) const
{
    const int row = rowFor(address);
    if (row < 0)
        return 0.0;
    return m_rows.at(row).rxRate + m_rows.at(row).txRate;
}

void TorchModel::clear()
{
    beginResetModel();
    m_rows.clear();
    endResetModel();
    m_previous.clear();
    m_lastSampleMs = -1;
    m_samples = 0;
    m_flowCount = 0;
    m_totalRxRate = 0.0;
    m_totalTxRate = 0.0;
    emit countChanged();
    emit sampled();
}

void TorchModel::start()
{
    if (m_sampling)
        return;
    m_sampling = true;
    m_inFlight = false;
    m_lastSampleMs = -1;
    m_clock.start();
    setStatus(QStringLiteral("resolving router addresses"));
    emit samplingChanged();

    Tools::onClient(m_client, [client = m_client] { client->fetchLocalAddresses(); });
    m_timer.start();
}

void TorchModel::stop()
{
    m_sampling = false;
    m_timer.stop();
    m_inFlight = false;
    setStatus(QStringLiteral("stopped"));
    emit samplingChanged();
}

void TorchModel::requestSample()
{
    if (!m_sampling || m_inFlight)
        return;
    m_inFlight = true;
    Tools::onClient(m_client, [client = m_client] { client->fetchConntrack(); });
}

void TorchModel::onLocalAddresses(const QStringList &addresses)
{
    if (!m_sampling)
        return;
    m_localAddresses = addresses;
    m_haveAddresses = !addresses.isEmpty();
    if (!m_haveAddresses) {
        setStatus(QStringLiteral("router reported no interface addresses"));
        return;
    }
    setStatus(QStringLiteral("%1 local addresses").arg(addresses.size()));
    requestSample();
}

bool TorchModel::isLocal(const QString &address) const
{
    return m_localAddresses.contains(address);
}

void TorchModel::onConntrack(const QList<Ubus::ConntrackFlow> &flows)
{
    m_inFlight = false;
    if (!m_sampling)
        return;

    if (!m_haveAddresses) {
        Tools::onClient(m_client, [client = m_client] { client->fetchLocalAddresses(); });
        return;
    }

    const qint64 nowMs = m_clock.elapsed();
    const double elapsedSeconds =
        m_lastSampleMs >= 0 ? (nowMs - m_lastSampleMs) / 1000.0 : 0.0;
    const bool comparable = elapsedSeconds > 0.05;

    struct Bucket {
        quint64 rx = 0;
        quint64 tx = 0;
        int connections = 0;
    };
    QHash<QString, Bucket> buckets;
    QHash<QString, QPair<quint64, quint64>> previous;
    previous.reserve(flows.size());

    for (const Ubus::ConntrackFlow &flow : flows) {
        const bool srcLocal = isLocal(flow.srcAddress);
        const bool dstLocal = isLocal(flow.dstAddress);
        if (srcLocal == dstLocal)
            continue; // loopback or a flow between two router addresses

        const QString key = flow.tupleKey();
        const auto before = m_previous.constFind(key);
        quint64 rxDelta = 0;
        quint64 txDelta = 0;
        if (comparable && before != m_previous.constEnd()) {
            const quint64 bytes = flow.bytes >= before->first ? flow.bytes - before->first : 0;
            const quint64 reply = flow.replyBytes >= before->second ? flow.replyBytes - before->second : 0;
            if (dstLocal) {
                rxDelta = bytes;
                txDelta = reply;
            } else {
                txDelta = bytes;
                rxDelta = reply;
            }
        }
        previous.insert(key, {flow.bytes, flow.replyBytes});

        const QString peer = dstLocal ? flow.srcAddress : flow.dstAddress;
        Bucket &bucket = buckets[peer];
        bucket.rx += rxDelta;
        bucket.tx += txDelta;
        ++bucket.connections;
    }
    m_previous = previous;
    m_flowCount = int(buckets.size());

    QHash<QString, qint64> seen;
    m_totalRxRate = 0.0;
    m_totalTxRate = 0.0;

    for (HostRow &row : m_rows) {
        const auto bucket = buckets.constFind(row.address);
        if (bucket != buckets.constEnd()) {
            const double rxInstant = comparable ? bucket->rx / elapsedSeconds : 0.0;
            const double txInstant = comparable ? bucket->tx / elapsedSeconds : 0.0;
            row.rxRate = row.active ? row.rxRate * 0.4 + rxInstant * 0.6 : rxInstant;
            row.txRate = row.active ? row.txRate * 0.4 + txInstant * 0.6 : txInstant;
            row.rxBytes += bucket->rx;
            row.txBytes += bucket->tx;
            row.connections = bucket->connections;
            row.lastSeenMs = nowMs;
            row.active = true;
            seen.insert(row.address, nowMs);
        } else {
            row.rxRate *= 0.5;
            row.txRate *= 0.5;
            if (row.rxRate < 200.0)
                row.rxRate = 0.0;
            if (row.txRate < 200.0)
                row.txRate = 0.0;
            row.connections = 0;
            row.active = false;
        }
        m_totalRxRate += row.rxRate;
        m_totalTxRate += row.txRate;
    }

    for (auto it = buckets.constBegin(); it != buckets.constEnd(); ++it) {
        if (seen.contains(it.key()))
            continue;
        HostRow row;
        row.address = it.key();
        row.connections = it.value().connections;
        row.rxBytes = it.value().rx;
        row.txBytes = it.value().tx;
        row.lastSeenMs = nowMs;
        row.active = true;
        m_rows.append(row);
    }

    // Idle peers stay visible for a while so a burst does not vanish instantly.
    for (int i = m_rows.size() - 1; i >= 0; --i) {
        const HostRow &row = m_rows.at(i);
        if (!row.active && row.lastSeenMs > 0 && nowMs - row.lastSeenMs > kIdleRetentionMs)
            m_rows.removeAt(i);
    }

    std::sort(m_rows.begin(), m_rows.end(), [](const HostRow &a, const HostRow &b) {
        return (a.rxRate + a.txRate) > (b.rxRate + b.txRate);
    });

    m_lastSampleMs = nowMs;
    ++m_samples;
    setStatus(QStringLiteral("%1 flows, %2 peers").arg(flows.size()).arg(m_flowCount));

    beginResetModel();
    endResetModel();
    emit countChanged();
    emit sampled();
}

void TorchModel::onError(const Ubus::Error &error)
{
    m_inFlight = false;
    if (!m_sampling)
        return;
    if (error.status == Ubus::Status::CertificateRequired)
        return;
    setStatus(QStringLiteral("failed: %1").arg(error.message));
    emit pollError(error.message);
}

void TorchModel::setStatus(const QString &status)
{
    if (m_status == status)
        return;
    m_status = status;
    emit sampled();
}
