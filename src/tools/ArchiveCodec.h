// SPDX-FileCopyrightText: 2026 the router-console authors
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QList>
#include <QString>

// The config-backup container: a ustar archive inside a gzip wrapper, both
// written by this file so the client needs no tar/gzip binary on either side.
// The gzip stream uses deflate's stored-block encoding: every `tar -tzf`,
// `gzip -d` and `tar -xzf` reads it, and there is no compression step to get
// wrong. /etc/config is a few kilobytes, so the size cost is not worth a zlib
// dependency in the cross-platform build.
namespace Archive {

struct Member {
    QString name; // relative path, e.g. "etc/config/network"
    QByteArray data;
    quint32 mode = 0644;
    QDateTime modified;
};

// Rejects absolute paths, "..", empty segments and anything that is not a plain
// relative path: nothing outside the archive root can be addressed.
bool isSafeMemberName(const QString &name);

QByteArray buildTarGz(const QList<Member> &members);
QList<Member> parseTarGz(const QByteArray &blob, QString *error);

QByteArray gzipCompress(const QByteArray &data);
QByteArray gzipDecompress(const QByteArray &blob, QString *error);

} // namespace Archive
