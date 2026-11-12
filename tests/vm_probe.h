// SPDX-FileCopyrightText: 2026 the router-console authors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <QString>
#include <QTcpSocket>
#include <QTextStream>

// The integration tests assert router behaviour against the dev VM
// (docs/dev-vm-recipe.md). With no VM there is nothing to assert, so they
// report a skip (exit 3, the ctest SKIP_RETURN_CODE) instead of a failure.
inline bool devVmReachable(const QString &endpoint, int timeoutMs = 1500)
{
    QString host = endpoint;
    quint16 port = 443;
    const qsizetype colon = endpoint.lastIndexOf(QLatin1Char(':'));
    if (colon > 0) {
        host = endpoint.left(colon);
        port = static_cast<quint16>(endpoint.mid(colon + 1).toUInt());
    }

    QTcpSocket socket;
    socket.connectToHost(host, port);
    return socket.waitForConnected(timeoutMs);
}

inline int skipWithoutDevVm(QTextStream &out, const QString &endpoint)
{
    if (devVmReachable(endpoint))
        return 0;
    out << "SKIP: dev VM not reachable at " << endpoint << Qt::endl;
    out.flush();
    return 3;
}
