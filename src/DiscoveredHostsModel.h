// SPDX-FileCopyrightText: 2026 the router-console authors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <QAbstractListModel>
#include <QVector>

#include "protocol/MdnsTypes.h"

// Discovered routers for the connect window. Lives on the UI thread; the
// discovery worker only hands it typed hosts through a queued signal.
class DiscoveredHostsModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Roles {
        HostnameRole = Qt::UserRole + 1,
        AddressRole,
        PortRole,
        TargetRole,
        ServicesRole,
        LastSeenRole,
    };

    explicit DiscoveredHostsModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return m_hosts.size(); }
    void upsert(const Mdns::Host &host);

signals:
    void countChanged();

private:
    void reorder();

    QVector<Mdns::Host> m_hosts;
};
