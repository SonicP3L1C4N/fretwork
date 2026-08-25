// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#pragma once

#include "score.h"

class QByteArray;
class QString;

/**
 * Guitar Pro 7 and 8: a ZIP container around `Content/score.gpif`, which is
 * XML.
 *
 * The friendly member of the family, and therefore the one to write first. GP6
 * wraps the same document in a bit-level compression of its own (BCFS/BCFZ),
 * and GP3 to GP5 are a different format entirely -- both later, and both
 * likely in Rust, because they are binary formats with no specification and
 * that is where a fuzzer earns its keep. This one is a ZIP and an XML
 * document, handled by KArchive and Qt, which have been read malformed input
 * by more people than this project will ever have users.
 *
 * Neither entry point throws. A file that cannot be read produces an empty
 * score and, where one is asked for, a sentence saying why.
 */
namespace Gpif
{
/** Reads a .gp file. Returns an empty Score if it cannot be understood. */
Score read(const QString &path, QString *error = nullptr);

/** The score.gpif document alone, already out of its container. */
Score parse(const QByteArray &xml, QString *error = nullptr);
}
