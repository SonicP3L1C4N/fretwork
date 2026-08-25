// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

/**
 * Enough of a ZIP reader to open a `.gp` file, and no more.
 *
 * Writing one of these is normally a bad idea, and this exists because the
 * obvious alternatives did not read the files. KArchive's KZip walks local
 * file headers, and Guitar Pro 8.1.4 writes its archives in streaming mode --
 * bit 3 of the general purpose flags, which says "the compressed size, the
 * uncompressed size and the CRC are not in this header; they follow the data".
 * Every 8.1.3 file in the corpus opens; neither 8.1.4 file does, and 8.1.4 is
 * the version people are updating to.
 *
 * So this reads the central directory instead, which is where a ZIP's real
 * index lives and where those sizes are always recorded whatever the writer
 * did. That is how a ZIP is supposed to be read.
 *
 * What it deliberately does not do: Zip64, encryption, multi-part archives, or
 * any compression method beyond stored and deflate. A `.gp` uses none of them,
 * and each is refused with a sentence rather than guessed at.
 */
namespace Zip
{
/** The names in the archive, or empty with a reason set. */
QStringList entries(const QString &path, QString *error = nullptr);

/**
 * One entry's contents, decompressed.
 *
 * Returns a null QByteArray if the archive or the entry cannot be read, and
 * sets `error` to a sentence saying which. An entry that is genuinely empty
 * comes back as an empty-but-not-null array.
 */
QByteArray readEntry(const QString &path, const QString &name, QString *error = nullptr);
}
