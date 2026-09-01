// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "gpbinary.h"

#include <KLocalizedString>

#include <QFile>

#ifdef FRETWORK_HAVE_GPBINARY
extern "C" {
int gpbinary_format(const unsigned char *data, size_t length);
int gpbinary_abi_version();
}
#endif

namespace
{
/**
 * The ABI this side was written against.
 *
 * Kept beside the declarations it describes rather than in a shared header,
 * because a shared header is a thing two halves can both be wrong about
 * together.
 */
constexpr int ExpectedAbi = 1;

/**
 * How much of a file has to be read to answer the question.
 *
 * The longest announcement any of these formats makes is GP3-to-GP5's Pascal
 * string, which is a length byte and thirty of text. Sixty-four is that with
 * room, and is a great deal less than a score.
 */
constexpr qint64 Enough = 64;
}

bool Gpbinary::isAvailable()
{
#ifdef FRETWORK_HAVE_GPBINARY
    return gpbinary_abi_version() == ExpectedAbi;
#else
    return false;
#endif
}

Gpbinary::Format Gpbinary::formatOf(const QByteArray &bytes)
{
#ifdef FRETWORK_HAVE_GPBINARY
    if (!isAvailable() || bytes.isEmpty()) {
        return Format::Unknown;
    }
    const int answer =
        gpbinary_format(reinterpret_cast<const unsigned char *>(bytes.constData()),
                        size_t(bytes.size()));
    // A value this side has never heard of is a newer library against an older
    // program, and the safe reading of it is the one that claims nothing.
    if (answer < int(Format::Unknown) || answer > int(Format::Gp7)) {
        return Format::Unknown;
    }
    return Format(answer);
#else
    Q_UNUSED(bytes);
    return Format::Unknown;
#endif
}

Gpbinary::Format Gpbinary::formatOf(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return Format::Unknown;
    }
    return formatOf(file.read(Enough));
}

QString Gpbinary::nameOf(Format format)
{
    switch (format) {
    case Format::Gp3: return i18n("Guitar Pro 3");
    case Format::Gp4: return i18n("Guitar Pro 4");
    case Format::Gp5: return i18n("Guitar Pro 5");
    case Format::Gpx: return i18n("Guitar Pro 6");
    case Format::Gp7: return i18n("Guitar Pro 7 or 8");
    case Format::Unknown: break;
    }
    return QString();
}
