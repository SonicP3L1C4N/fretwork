// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "notation.h"

#include "notevalue.h"
#include "timeline.h"

#include <QJsonArray>

namespace
{
/** The version this file writes, which is the version the sample carries. */
constexpr int NotationVersion = 1;

/**
 * A written value as musicians number it: 4 is a crotchet, 8 a quaver.
 *
 * The model counts in quarters, so a crotchet is 1 and a quaver a half, and
 * the denominator is four divided by that. Nought for anything that is not a
 * written value at all -- a tuplet, most obviously -- which the caller treats
 * as a beat it cannot describe rather than as a beat lasting no time.
 */
int denominatorOf(const Rational &value)
{
    if (value.numerator <= 0) {
        return 0;
    }
    const Rational quarters = Rational(4) / value;
    return quarters.denominator == 1 ? int(quarters.numerator) : 0;
}

/** Whether this track's beats are worth writing down as notation at all. */
bool isWritable(const Score &score, int track)
{
    return track >= 0 && track < score.tracks.size()
        && !score.tracks.at(track).isPercussion();
}
}

QString Notation::clefFor(const Track &track)
{
    // The bass clef for the instruments that are written in it. A comparison
    // against the instrument name rather than the tuning, because the tunings
    // overlap -- see the header.
    if (track.instrumentType.contains(QStringLiteral("ass"), Qt::CaseSensitive)) {
        return QStringLiteral("F4");
    }
    return QStringLiteral("G2");
}

QJsonObject Notation::documentFor(const Score &score, int track,
                                  const QList<int> &order, const QString &staffId)
{
    QJsonObject document;
    if (!isWritable(score, track)) {
        return document;
    }
    const Track &part = score.tracks.at(track);

    document.insert(QStringLiteral("version"), NotationVersion);
    document.insert(QStringLiteral("instrument"), part.instrumentType);

    QJsonObject staff;
    staff.insert(QStringLiteral("id"), staffId);
    staff.insert(QStringLiteral("clef"), clefFor(part));
    staff.insert(QStringLiteral("label"), part.name);
    document.insert(QStringLiteral("staves"), QJsonArray{staff});

    const Timeline::Clock clock(score, order);
    QJsonArray measures;

    Rational position(0);
    int lastNumerator = 0;
    int lastDenominator = 0;
    double lastTempo = 0.0;

    for (int played = 0; played < order.size(); ++played) {
        const MasterBar &master = score.masterBars.at(order.at(played));

        QJsonObject measure;
        measure.insert(QStringLiteral("idx"), played + 1);
        measure.insert(QStringLiteral("t"), clock.secondsAt(position));

        // A time signature and a tempo are written where they change, which is
        // what the sample does and what a reader of printed music expects: a
        // signature on every bar is not a score, it is a spreadsheet.
        if (master.numerator != lastNumerator || master.denominator != lastDenominator) {
            measure.insert(QStringLiteral("ts"),
                           QJsonArray{master.numerator, master.denominator});
            lastNumerator = master.numerator;
            lastDenominator = master.denominator;
        }
        const double tempo = Timeline::tempoAtBar(score, order.at(played));
        if (tempo > 0.0 && tempo != lastTempo) {
            measure.insert(QStringLiteral("tempo"), tempo);
            lastTempo = tempo;
        }

        QJsonArray voices;
        if (track < master.bars.size()) {
            const Bar bar = score.bars.value(master.bars.at(track));
            for (int slot = 0; slot < bar.voices.size(); ++slot) {
                const auto voice = score.voices.constFind(bar.voices.at(slot));
                if (voice == score.voices.constEnd() || voice->beats.isEmpty()) {
                    continue;
                }

                QJsonArray beats;
                Rational offset(0);
                for (const int beatId : voice->beats) {
                    const auto beat = score.beats.constFind(beatId);
                    if (beat == score.beats.constEnd()) {
                        continue;
                    }
                    const Rational duration =
                        score.rhythms.value(beat->rhythm, Rational(1));

                    // A rest is a beat with no notes. It is written rather
                    // than skipped, which is the sample file's one real
                    // failing: a reader given only the sounding notes cannot
                    // tell a bar of silence from a bar somebody forgot.
                    QJsonArray notes;
                    for (const int noteId : beat->notes) {
                        const auto note = score.notes.constFind(noteId);
                        if (note == score.notes.constEnd() || note->midi < 0) {
                            continue;
                        }
                        QJsonObject written;
                        written.insert(QStringLiteral("midi"), note->midi);
                        if (note->tieDestination) {
                            written.insert(QStringLiteral("tied"), true);
                        }
                        notes.append(written);
                    }

                    // A tuplet is written as the value it is notated with,
                    // because that is what a reader draws: a triplet quaver is
                    // a quaver with a 3 over it, and the 3 has nowhere to go.
                    // The sample pack's beats carry `t`, `dur`, `dot` and
                    // `notes` and nothing else, so the format has no field for
                    // one.
                    //
                    // The consequence is worth stating in numbers rather than
                    // leaving to be discovered: a bar holding tuplets adds up
                    // to more than it lasts. Across the seven files here that
                    // is 296 voice-bars out of 5,478, every one of them over
                    // rather than under, and concentrated in the two most
                    // rhythmically ornate scores. Nothing is dropped and no
                    // time is lost -- `t` is on every beat and is the truth
                    // about when it sounds, which is also how the sample file
                    // works, since its beats do not tile its bars either.
                    const NoteValue::Written value = NoteValue::of(duration);
                    const int denominator = denominatorOf(value.value);
                    if (denominator > 0) {
                        QJsonObject written;
                        written.insert(QStringLiteral("t"),
                                       clock.secondsAt(position + offset));
                        written.insert(QStringLiteral("dur"), denominator);
                        written.insert(QStringLiteral("dot"), value.dots);
                        written.insert(QStringLiteral("notes"), notes);
                        beats.append(written);
                    }
                    offset = offset + duration;
                }

                QJsonObject written;
                written.insert(QStringLiteral("v"), slot + 1);
                written.insert(QStringLiteral("beats"), beats);
                voices.append(written);
            }
        }

        QJsonObject staves;
        QJsonObject one;
        one.insert(QStringLiteral("voices"), voices);
        staves.insert(staffId, one);
        measure.insert(QStringLiteral("staves"), staves);

        measures.append(measure);
        position = position + master.length();
    }

    document.insert(QStringLiteral("measures"), measures);
    return document;
}
