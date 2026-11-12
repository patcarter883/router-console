// SPDX-FileCopyrightText: 2026 the router-console authors
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ArchiveCodec.h"

#include <QStringList>
#include <QTimeZone>

#include <array>
#include <cstring>

namespace Archive {
namespace {

constexpr int kBlockSize = 512;
constexpr int kMaxStoredBlock = 65535;

quint32 crcTableEntry(quint32 index)
{
    quint32 value = index;
    for (int bit = 0; bit < 8; ++bit)
        value = (value & 1u) ? 0xEDB88320u ^ (value >> 1) : value >> 1;
    return value;
}

const std::array<quint32, 256> &crcTable()
{
    static const std::array<quint32, 256> table = [] {
        std::array<quint32, 256> built{};
        for (quint32 i = 0; i < 256; ++i)
            built[i] = crcTableEntry(i);
        return built;
    }();
    return table;
}

quint32 crc32(const QByteArray &data)
{
    quint32 crc = 0xFFFFFFFFu;
    for (const char byte : data)
        crc = crcTable()[(crc ^ static_cast<quint8>(byte)) & 0xFFu] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

void appendLe16(QByteArray &out, quint16 value)
{
    out.append(char(value & 0xFF));
    out.append(char((value >> 8) & 0xFF));
}

void appendLe32(QByteArray &out, quint32 value)
{
    for (int shift = 0; shift < 32; shift += 8)
        out.append(char((value >> shift) & 0xFF));
}

bool readLe16(const QByteArray &blob, qsizetype offset, quint16 *value)
{
    if (offset + 2 > blob.size())
        return false;
    *value = static_cast<quint16>(static_cast<quint8>(blob[offset]))
             | static_cast<quint16>(static_cast<quint8>(blob[offset + 1])) << 8;
    return true;
}

bool readLe32(const QByteArray &blob, qsizetype offset, quint32 *value)
{
    if (offset + 4 > blob.size())
        return false;
    quint32 built = 0;
    for (int i = 0; i < 4; ++i)
        built |= static_cast<quint32>(static_cast<quint8>(blob[offset + i])) << (i * 8);
    *value = built;
    return true;
}

void writeOctal(char *field, int width, quint64 value)
{
    const QByteArray text = QByteArray::number(value, 8).rightJustified(width - 1, '0');
    std::memcpy(field, text.constData(), size_t(width - 1));
    field[width - 1] = '\0';
}

quint64 readOctal(const char *field, int width)
{
    quint64 value = 0;
    for (int i = 0; i < width; ++i) {
        const char c = field[i];
        if (c == '\0' || c == ' ')
            break;
        if (c < '0' || c > '7')
            return 0;
        value = value * 8 + static_cast<quint64>(c - '0');
    }
    return value;
}

QByteArray tarHeader(const Member &member)
{
    QByteArray header(kBlockSize, '\0');
    QByteArray name = member.name.toUtf8().left(99);
    std::memcpy(header.data(), name.constData(), size_t(name.size()));

    writeOctal(header.data() + 100, 8, member.mode & 07777);
    writeOctal(header.data() + 108, 8, 0);
    writeOctal(header.data() + 116, 8, 0);
    writeOctal(header.data() + 124, 12, quint64(member.data.size()));
    writeOctal(header.data() + 136, 12,
               quint64(member.modified.isValid() ? member.modified.toSecsSinceEpoch() : 0));
    header[156] = '0';
    std::memcpy(header.data() + 257, "ustar", 5);
    header[263] = '0';
    header[264] = '0';
    std::memcpy(header.data() + 265, "root", 4);
    std::memcpy(header.data() + 297, "root", 4);

    // The checksum is computed with the field itself read as eight spaces.
    std::memset(header.data() + 148, ' ', 8);
    quint64 sum = 0;
    for (const char byte : header)
        sum += static_cast<quint8>(byte);
    const QByteArray checksum = QByteArray::number(sum, 8).rightJustified(6, '0') + '\0';
    std::memcpy(header.data() + 148, checksum.constData(), 7);
    header[155] = ' ';
    return header;
}

} // namespace

bool isSafeMemberName(const QString &name)
{
    if (name.isEmpty() || name.startsWith(QLatin1Char('/')) || name.contains(QLatin1Char('\\')))
        return false;
    if (name.contains(QLatin1Char(':')))
        return false;
    const QStringList segments = name.split(QLatin1Char('/'));
    for (const QString &segment : segments) {
        if (segment.isEmpty() || segment == QLatin1String("..") || segment == QLatin1String("."))
            return false;
    }
    return true;
}

QByteArray gzipCompress(const QByteArray &data)
{
    QByteArray out;
    out.append("\x1f\x8b\x08\x00", 4);
    appendLe32(out, 0);
    out.append('\x00');
    out.append('\xff');

    qsizetype offset = 0;
    do {
        const qsizetype chunk = qMin<qsizetype>(kMaxStoredBlock, data.size() - offset);
        const bool last = offset + chunk >= data.size();
        out.append(char(last ? 0x01 : 0x00));
        appendLe16(out, static_cast<quint16>(chunk));
        appendLe16(out, static_cast<quint16>(~static_cast<quint16>(chunk)));
        out.append(data.constData() + offset, chunk);
        offset += chunk;
    } while (offset < data.size());

    appendLe32(out, crc32(data));
    appendLe32(out, static_cast<quint32>(data.size()));
    return out;
}

QByteArray gzipDecompress(const QByteArray &blob, QString *error)
{
    const auto fail = [error](const QString &message) {
        if (error)
            *error = message;
        return QByteArray();
    };

    if (blob.size() < 18 || static_cast<quint8>(blob[0]) != 0x1F
        || static_cast<quint8>(blob[1]) != 0x8B) {
        return fail(QStringLiteral("not a gzip stream"));
    }
    if (static_cast<quint8>(blob[2]) != 8)
        return fail(QStringLiteral("unsupported gzip compression method"));

    const quint8 flags = static_cast<quint8>(blob[3]);
    qsizetype offset = 10;
    if (flags & 0x04) { // FEXTRA
        quint16 length = 0;
        if (!readLe16(blob, offset, &length))
            return fail(QStringLiteral("truncated gzip header"));
        offset += 2 + length;
    }
    for (const quint8 flag : {quint8(0x08), quint8(0x10)}) { // FNAME, FCOMMENT
        if (!(flags & flag))
            continue;
        while (offset < blob.size() && blob[offset] != '\0')
            ++offset;
        ++offset;
    }
    if (flags & 0x02) // FHCRC
        offset += 2;
    if (offset + 8 > blob.size())
        return fail(QStringLiteral("truncated gzip header"));

    QByteArray out;
    bool final = false;
    while (!final) {
        if (offset + 5 > blob.size())
            return fail(QStringLiteral("truncated deflate stream"));
        const quint8 header = static_cast<quint8>(blob[offset]);
        final = (header & 0x01) != 0;
        if ((header >> 1) & 0x03) {
            return fail(QStringLiteral(
                "archive uses compressed deflate blocks; this client reads the "
                "stored-block archives it writes"));
        }
        quint16 length = 0;
        quint16 inverse = 0;
        readLe16(blob, offset + 1, &length);
        readLe16(blob, offset + 3, &inverse);
        if (static_cast<quint16>(~length) != inverse)
            return fail(QStringLiteral("corrupt deflate stored block"));
        offset += 5;
        if (offset + length > blob.size())
            return fail(QStringLiteral("truncated deflate stored block"));
        out.append(blob.constData() + offset, length);
        offset += length;
    }

    quint32 expectedCrc = 0;
    quint32 expectedSize = 0;
    if (!readLe32(blob, offset, &expectedCrc) || !readLe32(blob, offset + 4, &expectedSize))
        return fail(QStringLiteral("truncated gzip trailer"));
    if (expectedCrc != crc32(out) || expectedSize != static_cast<quint32>(out.size()))
        return fail(QStringLiteral("gzip checksum mismatch"));
    return out;
}

QByteArray buildTarGz(const QList<Member> &members)
{
    QByteArray tar;
    for (const Member &member : members) {
        if (!isSafeMemberName(member.name))
            continue;
        tar.append(tarHeader(member));
        tar.append(member.data);
        const int padding = (kBlockSize - (member.data.size() % kBlockSize)) % kBlockSize;
        if (padding > 0)
            tar.append(QByteArray(padding, '\0'));
    }
    tar.append(QByteArray(kBlockSize * 2, '\0'));
    return gzipCompress(tar);
}

QList<Member> parseTarGz(const QByteArray &blob, QString *error)
{
    const auto fail = [error](const QString &message) {
        if (error)
            *error = message;
        return QList<Member>();
    };

    if (error)
        error->clear();

    QString gunzipError;
    const QByteArray tar = gzipDecompress(blob, &gunzipError);
    if (tar.isEmpty())
        return fail(gunzipError.isEmpty() ? QStringLiteral("empty archive") : gunzipError);

    QList<Member> members;
    qsizetype offset = 0;
    while (offset + kBlockSize <= tar.size()) {
        const char *header = tar.constData() + offset;
        bool blank = true;
        for (int i = 0; i < kBlockSize && blank; ++i)
            blank = header[i] == '\0';
        if (blank)
            break;

        char nameField[101] = {};
        std::memcpy(nameField, header, 100);
        char prefixBuf[156] = {};
        std::memcpy(prefixBuf, header + 345, 155);
        QString name = QString::fromUtf8(nameField);
        const QString prefix = QString::fromUtf8(prefixBuf);
        if (!prefix.isEmpty())
            name = prefix + QLatin1Char('/') + name;

        const quint64 size = readOctal(header + 124, 12);
        const char type = header[156];
        offset += kBlockSize;

        if (type == '0' || type == '\0') {
            if (offset + qsizetype(size) > tar.size())
                return fail(QStringLiteral("truncated archive member: %1").arg(name));
            if (!isSafeMemberName(name))
                return fail(QStringLiteral("unsafe archive member: %1").arg(name));
            Member member;
            member.name = name;
            member.data = tar.mid(offset, qsizetype(size));
            member.mode = quint32(readOctal(header + 100, 8));
            member.modified = QDateTime::fromSecsSinceEpoch(readOctal(header + 136, 12),
                                                            QTimeZone::UTC);
            members.append(member);
        }

        offset += qsizetype((size + kBlockSize - 1) / kBlockSize) * kBlockSize;
    }

    if (members.isEmpty())
        return fail(QStringLiteral("archive contains no files"));
    return members;
}

} // namespace Archive
