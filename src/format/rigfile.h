// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#pragma once

#include <QList>
#include <QString>
#include <QStringList>

/**
 * The rig a score is played through, kept in a file beside the score.
 *
 * Beside it rather than inside it, and the reason is the same one that keeps
 * the sample library out of a `.fw`: a rig names plugins by URI and samples by
 * path, and both of those are facts about one machine. A `.fw` carrying them
 * would open on somebody else's computer as a score that refuses to play, and
 * the score itself is the part that travels.
 *
 * So `Horses.gp` gets `Horses.gp.rig`, and a person who copies one without the
 * other has copied a score, which is what a score is. The file is plain JSON
 * rather than a ZIP because there is nothing in it worth compressing and a
 * rig is a thing somebody might reasonably want to read, diff or hand-edit.
 *
 * **Knobs are stored by symbol, not by port index.** The index is where a
 * control happens to sit in one build of one plugin; the symbol is the name
 * the plugin publishes and the one the command line already speaks
 * (`--knob 0:0:Drive=0.8`). Storing the index would mean a rig that came back
 * subtly wrong after a plugin was rebuilt with a control added, which is the
 * quiet kind of wrong this project spends its effort avoiding.
 *
 * Unknown keys are ignored and missing ones default to a blank, on the same
 * terms as `Fw` -- a rig written by a later version loads in an earlier one
 * with whatever that one understands.
 */
namespace Rig
{
/**
 * Bumped only when an older reader could not make sense of a newer file, on
 * the same terms as `Fw::FormatVersion`. Adding an optional key is not that.
 */
constexpr int FormatVersion = 1;

/** One knob on one plugin along a track's chain. */
struct Knob {
    /** Which plugin along the chain, counted from the instrument. */
    int stage = 0;
    /** The control's LV2 symbol, which is what survives a plugin's rebuild. */
    QString symbol;
    float value = 0;
};

/** What one track is played through. */
struct Track {
    int track = 0;
    /** An `.sfz` to play from, or empty for a General MIDI programme. */
    QString sampler;
    /** LV2 URIs in order, the first nearest the instrument. */
    QStringList chain;
    QList<Knob> knobs;
};

struct Document {
    QList<Track> tracks;
    bool isEmpty() const;
};

/**
 * The rig file belonging beside `scorePath`.
 *
 * The whole name and then the suffix -- `Horses.gp.rig`, not `Horses.rig` --
 * so that a score and an import of it under another extension cannot end up
 * sharing one, and so the rig sorts next to the score it belongs to.
 */
QString pathFor(const QString &scorePath);

/** The suffix, without the dot. */
QString extension();

/**
 * Writes `rig` to `path`, or removes the file if the rig is empty.
 *
 * An empty rig is written as no file rather than as an empty one: a person who
 * takes every effect off a score has said they want it dry, and leaving an
 * empty document behind would be leaving litter that says nothing.
 */
bool write(const Document &rig, const QString &path, QString *error = nullptr);

/**
 * Reads the rig at `path`. A missing file is not an error -- it is a score
 * nobody has built a rig for yet, and comes back empty with no message.
 */
Document read(const QString &path, QString *error = nullptr);
}
