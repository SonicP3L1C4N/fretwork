// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "zipreader.h"

#include <QFile>
#include <QFileInfo>
#include <QtEndian>

#include <zlib.h>

namespace
{
constexpr quint32 EndOfCentralDirectorySignature = 0x06054b50;
constexpr quint32 CentralEntrySignature = 0x02014b50;
constexpr quint32 LocalHeaderSignature = 0x04034b50;

constexpr int EndOfCentralDirectorySize = 22;
constexpr int CentralEntrySize = 46;
constexpr int LocalHeaderSize = 30;

/** A comment can be 64 KiB, and the record sits just before it. */
constexpr qint64 MaximumCommentSize = 0xFFFF;

/**
 * Tablature is small. The largest file in any corpus this has met is under a
 * megabyte, and refusing to read a whole disk into memory because someone
 * renamed something to .gp costs nothing.
 */
constexpr qint64 MaximumArchiveSize = 256LL * 1024 * 1024;

/** Reading past the end of the buffer is the whole risk here, so it goes through these. */
bool has(const QByteArray &data, qint64 offset, qint64 length)
{
    return offset >= 0 && length >= 0 && offset + length <= data.size();
}

quint16 u16(const QByteArray &data, qint64 offset)
{
    return qFromLittleEndian<quint16>(
        reinterpret_cast<const uchar *>(data.constData()) + offset);
}

quint32 u32(const QByteArray &data, qint64 offset)
{
    return qFromLittleEndian<quint32>(
        reinterpret_cast<const uchar *>(data.constData()) + offset);
}

bool fail(QString *error, const QString &reason)
{
    if (error) {
        *error = reason;
    }
    return false;
}

/** The last End of Central Directory record in the file, or -1. */
qint64 findEndOfCentralDirectory(const QByteArray &data)
{
    const qint64 earliest =
        std::max<qint64>(0, data.size() - EndOfCentralDirectorySize - MaximumCommentSize);
    for (qint64 offset = data.size() - EndOfCentralDirectorySize; offset >= earliest; --offset) {
        if (u32(data, offset) == EndOfCentralDirectorySignature) {
            return offset;
        }
    }
    return -1;
}

struct Entry {
    QString name;
    quint16 method = 0;
    quint32 compressedSize = 0;
    quint32 uncompressedSize = 0;
    quint32 localOffset = 0;
};

/**
 * The central directory, which is the index a ZIP writer is obliged to get
 * right even when it did not know the sizes while writing the data.
 */
bool readCentralDirectory(const QByteArray &data, QList<Entry> &entries, QString *error)
{
    const qint64 end = findEndOfCentralDirectory(data);
    if (end < 0) {
        return fail(error, QStringLiteral("not a ZIP container"));
    }

    const quint16 count = u16(data, end + 10);
    const quint32 size = u32(data, end + 12);
    const quint32 start = u32(data, end + 16);

    if (count == 0xFFFF || size == 0xFFFFFFFF || start == 0xFFFFFFFF) {
        return fail(error, QStringLiteral("a Zip64 archive, which this cannot read"));
    }
    if (!has(data, start, size)) {
        return fail(error, QStringLiteral("the central directory is outside the file"));
    }

    qint64 offset = start;
    entries.reserve(count);
    for (int index = 0; index < count; ++index) {
        if (!has(data, offset, CentralEntrySize) || u32(data, offset) != CentralEntrySignature) {
            return fail(error, QStringLiteral("the central directory is damaged at entry %1")
                                   .arg(index + 1));
        }

        Entry entry;
        const quint16 flags = u16(data, offset + 8);
        entry.method = u16(data, offset + 10);
        entry.compressedSize = u32(data, offset + 20);
        entry.uncompressedSize = u32(data, offset + 24);
        const quint16 nameLength = u16(data, offset + 28);
        const quint16 extraLength = u16(data, offset + 30);
        const quint16 commentLength = u16(data, offset + 32);
        entry.localOffset = u32(data, offset + 42);

        if (!has(data, offset + CentralEntrySize, nameLength)) {
            return fail(error, QStringLiteral("a file name runs past the end of the archive"));
        }
        // Bit 11 says the name is UTF-8. Guitar Pro 8.1.3 sets it, 8.1.4 does
        // not, and neither has ever used a byte above 127 in these names.
        const QByteArray raw = data.mid(offset + CentralEntrySize, nameLength);
        entry.name = (flags & 0x0800) ? QString::fromUtf8(raw) : QString::fromLatin1(raw);

        if (flags & 0x0001) {
            return fail(error, QStringLiteral("the archive is encrypted"));
        }

        entries.append(entry);
        offset += CentralEntrySize + nameLength + extraLength + commentLength;
    }
    return true;
}

/**
 * Where an entry's data starts.
 *
 * The name and extra fields are re-read from the local header rather than
 * taken from the central directory, because a writer is allowed to give them
 * different lengths in the two places, and several do.
 */
qint64 dataOffset(const QByteArray &data, const Entry &entry, QString *error)
{
    if (!has(data, entry.localOffset, LocalHeaderSize)
        || u32(data, entry.localOffset) != LocalHeaderSignature) {
        fail(error, QStringLiteral("%1: its local header is missing").arg(entry.name));
        return -1;
    }
    const quint16 nameLength = u16(data, entry.localOffset + 26);
    const quint16 extraLength = u16(data, entry.localOffset + 28);
    const qint64 offset = entry.localOffset + LocalHeaderSize + nameLength + extraLength;

    if (!has(data, offset, entry.compressedSize)) {
        fail(error, QStringLiteral("%1: its contents run past the end of the archive")
                        .arg(entry.name));
        return -1;
    }
    return offset;
}

/** Raw deflate -- no zlib or gzip wrapper, which is what the negative window means. */
QByteArray inflate(const QByteArray &compressed, quint32 expected, const QString &name,
                   QString *error)
{
    QByteArray out;
    out.resize(int(expected));

    z_stream stream = {};
    stream.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(compressed.constData()));
    stream.avail_in = uInt(compressed.size());
    stream.next_out = reinterpret_cast<Bytef *>(out.data());
    stream.avail_out = uInt(out.size());

    if (inflateInit2(&stream, -MAX_WBITS) != Z_OK) {
        fail(error, QStringLiteral("%1: cannot start decompressing").arg(name));
        return {};
    }
    const int result = ::inflate(&stream, Z_FINISH);
    const uLong written = stream.total_out;
    inflateEnd(&stream);

    if (result != Z_STREAM_END || written != expected) {
        fail(error, QStringLiteral("%1: its contents are damaged").arg(name));
        return {};
    }
    return out;
}

QByteArray slurp(const QString &path, QString *error)
{
    const QFileInfo info(path);
    if (!info.exists()) {
        fail(error, QStringLiteral("no such file"));
        return {};
    }
    if (info.size() > MaximumArchiveSize) {
        fail(error, QStringLiteral("larger than any tablature file: %1 MB")
                        .arg(info.size() / 1024 / 1024));
        return {};
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        fail(error, file.errorString());
        return {};
    }
    return file.readAll();
}
}

QStringList Zip::entries(const QString &path, QString *error)
{
    const QByteArray data = slurp(path, error);
    if (data.isNull()) {
        return {};
    }

    QList<Entry> found;
    if (!readCentralDirectory(data, found, error)) {
        return {};
    }

    QStringList names;
    names.reserve(int(found.size()));
    for (const Entry &entry : std::as_const(found)) {
        names.append(entry.name);
    }
    return names;
}

QByteArray Zip::readEntry(const QString &path, const QString &name, QString *error)
{
    const QByteArray data = slurp(path, error);
    if (data.isNull()) {
        return {};
    }

    QList<Entry> found;
    if (!readCentralDirectory(data, found, error)) {
        return {};
    }

    for (const Entry &entry : std::as_const(found)) {
        if (entry.name != name) {
            continue;
        }

        const qint64 offset = dataOffset(data, entry, error);
        if (offset < 0) {
            return {};
        }
        const QByteArray raw = data.mid(offset, entry.compressedSize);

        switch (entry.method) {
        case 0:     // stored
            if (uint(raw.size()) != entry.uncompressedSize) {
                fail(error, QStringLiteral("%1: its length does not match the index")
                                .arg(name));
                return {};
            }
            // An entry that is genuinely empty must not read as a failure.
            return raw.isNull() ? QByteArray("") : raw;
        case 8:     // deflate
            return inflate(raw, entry.uncompressedSize, name, error);
        default:
            fail(error, QStringLiteral("%1: compressed in a way this cannot read (method %2)")
                            .arg(name)
                            .arg(entry.method));
            return {};
        }
    }

    fail(error, QStringLiteral("no %1 inside").arg(name));
    return {};
}
