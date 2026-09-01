// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#pragma once

#include <QByteArray>
#include <QString>

/**
 * The C++ side of the Rust importer.
 *
 * Guitar Pro 7 and 8 are a ZIP holding XML, and `Gpif` reads them with KArchive
 * and Qt. The older formats are not: GP3 to GP5 and GP6 are hand-rolled binary
 * with no specification, described only by the programs that worked them out,
 * and reading one means walking a byte stream taking every length and offset on
 * trust from a file somebody downloaded off the internet. That is the shape of
 * problem worth a foreign function boundary, and the architecture committed to
 * this one long before there was anything behind it.
 *
 * **Optional, and the whole file works without it.** A build with no cargo has
 * no Rust half, `isAvailable()` says so, and everything here answers `Unknown`
 * -- which is what this program knew about those formats yesterday. Adding a
 * second toolchain to a build is a real cost to whoever packages it, and a hard
 * requirement is how a project stops being packaged.
 *
 * **What is behind it today is detection and nothing more.** That is deliberate
 * rather than unfinished. There is not one `.gpx`, `.gp3`, `.gp4` or `.gp5`
 * file on the machine this was written on, and a binary parser checked only
 * against fixtures written by the same hand as the parser is a parser that
 * agrees with itself. Detection can be honest without one: it reads the bytes a
 * file opens with, and the seven real transcriptions here are enough to prove
 * it does not mistake them for something older.
 *
 * The immediate use is a better refusal. `Gpif::read` already declines a file
 * it cannot read; with this it can say *which* format the file actually is,
 * which is the difference between a bug report and a shrug.
 */
namespace Gpbinary
{
/**
 * Which format a file announces itself to be.
 *
 * Mirrors the `Format` enum in `rust/gpbinary/src/lib.rs`, value for value.
 * The two are checked against each other at run time rather than trusted: see
 * `isAvailable()`.
 */
enum class Format {
    Unknown = 0,    //< not recognised, which most files are not
    Gp3 = 1,
    Gp4 = 2,
    Gp5 = 3,
    Gpx = 4,        //< Guitar Pro 6
    Gp7 = 5,        //< Guitar Pro 7 or 8, which `Gpif` reads
};

/**
 * Whether the Rust half was built in *and* agrees about the ABI.
 *
 * One check rather than two, because a caller has no use for a library that is
 * present and disagrees. A mismatch means the two halves were built from
 * different revisions, which is a packaging mistake rather than a runtime
 * condition, and the honest response is to behave as though the half were
 * missing rather than to interpret its answers as if it were not.
 */
bool isAvailable();

/** What the bytes say they are. `Unknown` where there is no Rust half. */
Format formatOf(const QByteArray &bytes);

/** The same, for a file, reading only as much of it as the question needs. */
Format formatOf(const QString &path);

/** "Guitar Pro 5", for a message to somebody who has just been refused. */
QString nameOf(Format format);
}
