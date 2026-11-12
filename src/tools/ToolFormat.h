// SPDX-FileCopyrightText: 2026 the router-console authors
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <QMetaObject>
#include <QString>
#include <QtGlobal>

#include <utility>

class UbusClient;

// Tool view objects live on the UI thread and only ever queue work onto the
// protocol thread.
namespace Tools {

template <typename Functor>
inline void onClient(UbusClient *client, Functor functor)
{
    QMetaObject::invokeMethod(client, std::move(functor), Qt::QueuedConnection);
}

inline QString rateText(double bytesPerSecond)
{
    if (bytesPerSecond <= 0.0)
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

inline QString bytesText(quint64 bytes)
{
    if (bytes >= 1000000000ull)
        return QStringLiteral("%1 GB").arg(bytes / 1000000000.0, 0, 'f', 2);
    if (bytes >= 1000000ull)
        return QStringLiteral("%1 MB").arg(bytes / 1000000.0, 0, 'f', 2);
    if (bytes >= 1000ull)
        return QStringLiteral("%1 kB").arg(bytes / 1000.0, 0, 'f', 1);
    return QStringLiteral("%1 B").arg(bytes);
}

inline QString rateSpeed(double bytesPerSecond)
{
    return QStringLiteral("%1/s").arg(bytesText(static_cast<quint64>(bytesPerSecond)));
}

} // namespace Tools
