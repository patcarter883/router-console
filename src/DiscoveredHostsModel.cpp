// SPDX-FileCopyrightText: 2026 the router-console authors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "DiscoveredHostsModel.h"

#include <algorithm>

namespace
{
// Consumer/IoT responders are numerous on a home LAN; a router running umdns
// publishes dropbear's _ssh._tcp and none of these.
const QStringList &consumerServices()
{
    static const QStringList services = {
        QStringLiteral("_googlecast._tcp"),   QStringLiteral("_matter._tcp"),
        QStringLiteral("_matterc._udp"),      QStringLiteral("_esphomelib._tcp"),
        QStringLiteral("_androidtvremote2._tcp"),
        QStringLiteral("_home-assistant._tcp"),
        QStringLiteral("_ipp._tcp"),          QStringLiteral("_printer._tcp"),
        QStringLiteral("_airplay._tcp"),      QStringLiteral("_raop._tcp"),
    };
    return services;
}

int rank(const Mdns::Host &host)
{
    int score = host.services.contains(QStringLiteral("_ssh._tcp")) ? 10 : 0;
    for (const QString &service : host.services) {
        if (consumerServices().contains(service))
            score -= 3;
    }
    return score;
}

bool before(const Mdns::Host &left, const Mdns::Host &right)
{
    const int leftRank = rank(left);
    const int rightRank = rank(right);
    if (leftRank != rightRank)
        return leftRank > rightRank;
    const int byName = QString::compare(left.hostname, right.hostname, Qt::CaseInsensitive);
    if (byName != 0)
        return byName < 0;
    return left.address < right.address;
}
} // namespace

DiscoveredHostsModel::DiscoveredHostsModel(QObject *parent) : QAbstractListModel(parent) {}

int DiscoveredHostsModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_hosts.size();
}

QVariant DiscoveredHostsModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_hosts.size())
        return {};
    const Mdns::Host &host = m_hosts.at(index.row());
    switch (role) {
    case HostnameRole: return host.hostname;
    case AddressRole: return host.address;
    case PortRole: return host.port;
    case TargetRole: return QStringLiteral("%1:%2").arg(host.address).arg(host.port);
    case ServicesRole: return host.services.join(QStringLiteral(", "));
    case LastSeenRole: return host.lastSeen;
    default: return {};
    }
}

QHash<int, QByteArray> DiscoveredHostsModel::roleNames() const
{
    return {{HostnameRole, "hostname"},  {AddressRole, "address"},
            {PortRole, "port"},          {TargetRole, "target"},
            {ServicesRole, "services"},  {LastSeenRole, "lastSeen"}};
}

void DiscoveredHostsModel::upsert(const Mdns::Host &host)
{
    for (int row = 0; row < m_hosts.size(); ++row) {
        Mdns::Host &existing = m_hosts[row];
        if (existing.hostname.compare(host.hostname, Qt::CaseInsensitive) != 0
            && existing.address != host.address)
            continue;
        if (existing == host)
            return;
        existing = host;
        reorder();
        const QModelIndex changed = index(row, 0);
        emit dataChanged(changed, changed);
        return;
    }

    m_hosts.append(host);
    reorder();
    emit countChanged();
}

// Ranking is part of the presentation, so ordering changes are a model reset:
// the row set is small and routers have to stay at the top as records arrive.
void DiscoveredHostsModel::reorder()
{
    QVector<Mdns::Host> sorted = m_hosts;
    std::stable_sort(sorted.begin(), sorted.end(), before);
    if (sorted == m_hosts)
        return;
    beginResetModel();
    m_hosts = sorted;
    endResetModel();
}
