// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "musicxml.h"

#include "key.h"
#include "notevalue.h"
#include "timeline.h"

#include <QDate>
#include <QXmlStreamWriter>

#include <numeric>

namespace
{
/**
 * The version claimed in the document, and the DTD that goes with it.
 *
 * 4.0 rather than the newest, because what is written here uses nothing later
 * and a reader refusing a version it does not know is a worse outcome than a
 * reader accepting one it does.
 */
const QString Version = QStringLiteral("4.0");

/**
 * A ceiling on divisions, so a pathological score cannot ask for a number that
 * overflows the durations computed against it.
 *
 * 3840 covers everything ordinary -- it is divisible by 2 to the eighth, by 3
 * and by 5, so demisemiquaver triplets and quintuplets are both whole numbers
 * -- and a score that wants more is one whose durations are rounded rather
 * than refused, since a rounded rhythm is still readable and no rhythm is not.
 */
constexpr int MaximumDivisions = 3840;

/** C D E F G A B, which is the order `Key::Spelling::step` counts in. */
const char *const Steps[] = {"C", "D", "E", "F", "G", "A", "B"};

/**
 * Every distinct note a percussion part uses, in order.
 *
 * A kit's note number is not a pitch, it is which drum. MusicXML says that by
 * declaring one unpitched instrument per drum in the part list and having each
 * note point at one, so the set has to be known before any of the notes are
 * written -- which is why this is gathered first rather than as it goes.
 */
QList<int> drumsOf(const Score &score, int track)
{
    QList<int> drums;
    for (const MasterBar &master : score.masterBars) {
        if (track >= master.bars.size()) {
            continue;
        }
        const Bar bar = score.bars.value(master.bars.at(track));
        for (const int voiceId : bar.voices) {
            const auto voice = score.voices.constFind(voiceId);
            if (voice == score.voices.constEnd()) {
                continue;
            }
            for (const int beatId : voice->beats) {
                const auto beat = score.beats.constFind(beatId);
                if (beat == score.beats.constEnd()) {
                    continue;
                }
                for (const int noteId : beat->notes) {
                    const auto note = score.notes.constFind(noteId);
                    if (note != score.notes.constEnd() && note->midi >= 0
                        && !drums.contains(note->midi)) {
                        drums.append(note->midi);
                    }
                }
            }
        }
    }
    std::sort(drums.begin(), drums.end());
    return drums;
}

/** What MusicXML calls a note value, by the denominator musicians name it by. */
QString typeOf(int denominator)
{
    switch (denominator) {
    case 1:   return QStringLiteral("whole");
    case 2:   return QStringLiteral("half");
    case 4:   return QStringLiteral("quarter");
    case 8:   return QStringLiteral("eighth");
    case 16:  return QStringLiteral("16th");
    case 32:  return QStringLiteral("32nd");
    case 64:  return QStringLiteral("64th");
    case 128: return QStringLiteral("128th");
    default:  return QString();
    }
}

/**
 * MusicXML numbers strings from the highest sounding down, and this program
 * numbers them from the lowest up.
 *
 * Both conventions are in use and neither is wrong; what would be wrong is
 * having the two meet without saying so. A six-string guitar's low E is string
 * 0 here and string 6 there.
 */
int stringNumberOf(const Track &track, int string)
{
    return int(track.tuning.size()) - string;
}

/** Sharps or flats, and whether the key is minor: the two things a key is. */
void writeKey(QXmlStreamWriter &xml, const Key::Signature &key)
{
    xml.writeStartElement(QStringLiteral("key"));
    xml.writeTextElement(QStringLiteral("fifths"), QString::number(key.accidentals));
    xml.writeTextElement(QStringLiteral("mode"),
                         key.minor ? QStringLiteral("minor") : QStringLiteral("major"));
    xml.writeEndElement();
}

/**
 * The clef, and the tuning under it.
 *
 * A guitar is written in a treble clef that sounds an octave lower than it
 * reads, which is what `clef-octave-change` of -1 says. It matters here in a
 * way it does not in a pack: the pitches this file writes are the ones that
 * sound, so without that line every guitar part would be read an octave high.
 */
void writeClefAndStaff(QXmlStreamWriter &xml, const Track &track)
{
    if (track.isPercussion()) {
        // A kit has no pitches, so it has no clef in the ordinary sense: the
        // percussion clef says only that the lines mean instruments rather
        // than notes. Nothing about a tuning belongs under it.
        xml.writeStartElement(QStringLiteral("clef"));
        xml.writeTextElement(QStringLiteral("sign"), QStringLiteral("percussion"));
        xml.writeTextElement(QStringLiteral("line"), QStringLiteral("2"));
        xml.writeEndElement();
        return;
    }

    const bool bass = track.instrumentType.contains(QStringLiteral("ass"));

    xml.writeStartElement(QStringLiteral("clef"));
    xml.writeTextElement(QStringLiteral("sign"),
                         bass ? QStringLiteral("F") : QStringLiteral("G"));
    xml.writeTextElement(QStringLiteral("line"), bass ? QStringLiteral("4")
                                                      : QStringLiteral("2"));
    if (!bass) {
        xml.writeTextElement(QStringLiteral("clef-octave-change"), QStringLiteral("-1"));
    }
    xml.writeEndElement();

    if (track.tuning.isEmpty()) {
        return;
    }
    xml.writeStartElement(QStringLiteral("staff-details"));
    xml.writeTextElement(QStringLiteral("staff-lines"),
                         QString::number(track.tuning.size()));
    // Written from the lowest line upwards, which is line 1 in MusicXML and
    // string 0 here: the two agree at this end and disagree at the other.
    for (int string = 0; string < track.tuning.size(); ++string) {
        const Key::Spelling spelling = Key::spell(track.tuning.at(string));
        xml.writeStartElement(QStringLiteral("staff-tuning"));
        xml.writeAttribute(QStringLiteral("line"), QString::number(string + 1));
        xml.writeTextElement(QStringLiteral("tuning-step"),
                             QString::fromLatin1(Steps[spelling.step]));
        if (spelling.alteration != 0) {
            xml.writeTextElement(QStringLiteral("tuning-alter"),
                                 QString::number(spelling.alteration));
        }
        xml.writeTextElement(QStringLiteral("tuning-octave"),
                             QString::number(spelling.octave));
        xml.writeEndElement();
    }
    if (track.capo > 0) {
        xml.writeTextElement(QStringLiteral("capo"), QString::number(track.capo));
    }
    xml.writeEndElement();
}

/** The techniques that have a place in the specification, and only those. */
void writeNotations(QXmlStreamWriter &xml, const Note &note, const Track &track,
                    bool hammerUp)
{
    const bool anything = note.string >= 0 || note.isHarmonic() || note.bended
        || note.slide != SlideType::None || note.hammerOrigin
        || note.hammerDestination;
    if (!anything) {
        return;
    }

    xml.writeStartElement(QStringLiteral("notations"));

    if (note.slide != SlideType::None) {
        // Only the two that connect two notes are slides in the MusicXML
        // sense: the element carries a number and a start, and a slide out of
        // a note into nothing has neither. The rest are left unwritten rather
        // than turned into something they are not.
        if (note.slide == SlideType::Legato || note.slide == SlideType::Shift) {
            xml.writeStartElement(QStringLiteral("slide"));
            xml.writeAttribute(QStringLiteral("type"), QStringLiteral("start"));
            xml.writeAttribute(QStringLiteral("number"), QStringLiteral("1"));
            xml.writeEndElement();
        }
    }

    xml.writeStartElement(QStringLiteral("technical"));
    if (note.string >= 0 && !track.tuning.isEmpty()) {
        xml.writeTextElement(QStringLiteral("string"),
                             QString::number(stringNumberOf(track, note.string)));
        xml.writeTextElement(QStringLiteral("fret"), QString::number(note.fret));
    }
    if (note.isHarmonic()) {
        xml.writeStartElement(QStringLiteral("harmonic"));
        // Natural where the string rings open, artificial otherwise. The
        // specification has no third answer, and the kinds this program keeps
        // -- pinch, tap, semi -- are all a finger stopping the string, which
        // is what artificial means.
        xml.writeEmptyElement(note.harmonic == Harmonic::Type::Natural
                                  ? QStringLiteral("natural")
                                  : QStringLiteral("artificial"));
        xml.writeEndElement();
    }
    if (note.hammerOrigin) {
        xml.writeStartElement(hammerUp ? QStringLiteral("hammer-on")
                                       : QStringLiteral("pull-off"));
        xml.writeAttribute(QStringLiteral("type"), QStringLiteral("start"));
        xml.writeAttribute(QStringLiteral("number"), QStringLiteral("1"));
        xml.writeEndElement();
    }
    if (note.hammerDestination) {
        xml.writeStartElement(hammerUp ? QStringLiteral("hammer-on")
                                       : QStringLiteral("pull-off"));
        xml.writeAttribute(QStringLiteral("type"), QStringLiteral("stop"));
        xml.writeAttribute(QStringLiteral("number"), QStringLiteral("1"));
        xml.writeEndElement();
    }
    if (note.bended) {
        xml.writeStartElement(QStringLiteral("bend"));
        // In semitones, where gpif counts hundredths of one.
        xml.writeTextElement(QStringLiteral("bend-alter"),
                             QString::number(note.bendDestinationValue / 100.0));
        xml.writeEndElement();
    }
    xml.writeEndElement();

    xml.writeEndElement();
}
}

int Musicxml::divisionsFor(const Score &score)
{
    long long divisions = 1;
    for (const Rational &rhythm : score.rhythms) {
        if (rhythm.denominator <= 0) {
            continue;
        }
        divisions = std::lcm(divisions, static_cast<long long>(rhythm.denominator));
        if (divisions >= MaximumDivisions) {
            return MaximumDivisions;
        }
    }
    return int(divisions);
}

QByteArray Musicxml::documentFor(const Score &score)
{
    QByteArray output;
    QXmlStreamWriter xml(&output);
    xml.setAutoFormatting(true);
    xml.writeStartDocument();
    xml.writeDTD(QStringLiteral(
        "<!DOCTYPE score-partwise PUBLIC \"-//Recordare//DTD MusicXML %1 Partwise//EN\" "
        "\"http://www.musicxml.org/dtds/partwise.dtd\">").arg(Version));

    xml.writeStartElement(QStringLiteral("score-partwise"));
    xml.writeAttribute(QStringLiteral("version"), Version);

    if (!score.title.isEmpty()) {
        xml.writeStartElement(QStringLiteral("work"));
        xml.writeTextElement(QStringLiteral("work-title"), score.title);
        xml.writeEndElement();
    }

    xml.writeStartElement(QStringLiteral("identification"));
    if (!score.artist.isEmpty()) {
        xml.writeStartElement(QStringLiteral("creator"));
        xml.writeAttribute(QStringLiteral("type"), QStringLiteral("composer"));
        xml.writeCharacters(score.artist);
        xml.writeEndElement();
    }
    xml.writeStartElement(QStringLiteral("encoding"));
    xml.writeTextElement(QStringLiteral("software"), QStringLiteral("Fretwork"));
    xml.writeTextElement(QStringLiteral("encoding-date"),
                         QDate::currentDate().toString(Qt::ISODate));
    xml.writeEndElement();
    xml.writeEndElement();

    // The part list first, in full, because a reader is entitled to know what
    // parts exist before it meets any of their notes.
    xml.writeStartElement(QStringLiteral("part-list"));
    for (int track = 0; track < score.tracks.size(); ++track) {
        const Track &part = score.tracks.at(track);
        const QString id = QStringLiteral("P%1").arg(track + 1);
        xml.writeStartElement(QStringLiteral("score-part"));
        xml.writeAttribute(QStringLiteral("id"), id);
        xml.writeTextElement(QStringLiteral("part-name"),
                             part.name.isEmpty() ? id : part.name);
        if (part.isPercussion()) {
            // One unpitched instrument per drum the part actually uses. The
            // note number is the drum's identity rather than its pitch, and
            // this is where MusicXML keeps that.
            //
            // `midi-unpitched` is one greater than the MIDI note number. That
            // is the specification's definition rather than something read off
            // a file, and it is the single place in this exporter where being
            // wrong would be silent -- every drum would move by one, which
            // reads as a kit mapped slightly oddly rather than as a bug.
            const QList<int> drums = drumsOf(score, track);
            for (const int drum : drums) {
                xml.writeStartElement(QStringLiteral("score-instrument"));
                xml.writeAttribute(QStringLiteral("id"),
                                   QStringLiteral("%1-I%2").arg(id).arg(drum));
                xml.writeTextElement(QStringLiteral("instrument-name"),
                                     QStringLiteral("Percussion %1").arg(drum));
                xml.writeEndElement();
            }
            for (const int drum : drums) {
                xml.writeStartElement(QStringLiteral("midi-instrument"));
                xml.writeAttribute(QStringLiteral("id"),
                                   QStringLiteral("%1-I%2").arg(id).arg(drum));
                xml.writeTextElement(QStringLiteral("midi-channel"), QStringLiteral("10"));
                xml.writeTextElement(QStringLiteral("midi-unpitched"),
                                     QString::number(drum + 1));
                xml.writeEndElement();
            }
        } else {
            xml.writeStartElement(QStringLiteral("score-instrument"));
            xml.writeAttribute(QStringLiteral("id"), id + QStringLiteral("-I1"));
            xml.writeTextElement(QStringLiteral("instrument-name"), part.instrumentType);
            xml.writeEndElement();
        }
        xml.writeEndElement();
    }
    xml.writeEndElement();

    const int divisions = divisionsFor(score);

    for (int track = 0; track < score.tracks.size(); ++track) {
        const Track &part = score.tracks.at(track);
        xml.writeStartElement(QStringLiteral("part"));
        xml.writeAttribute(QStringLiteral("id"), QStringLiteral("P%1").arg(track + 1));

        Key::Signature lastKey;
        int lastNumerator = 0;
        int lastDenominator = 0;
        double lastTempo = 0.0;

        for (int index = 0; index < score.masterBars.size(); ++index) {
            const MasterBar &master = score.masterBars.at(index);
            xml.writeStartElement(QStringLiteral("measure"));
            xml.writeAttribute(QStringLiteral("number"), QString::number(index + 1));

            const bool newKey = index == 0 || master.key != lastKey;
            const bool newTime = master.numerator != lastNumerator
                || master.denominator != lastDenominator;
            if (index == 0 || newKey || newTime) {
                xml.writeStartElement(QStringLiteral("attributes"));
                if (index == 0) {
                    xml.writeTextElement(QStringLiteral("divisions"),
                                         QString::number(divisions));
                }
                if (newKey) {
                    writeKey(xml, master.key);
                    lastKey = master.key;
                }
                if (newTime) {
                    xml.writeStartElement(QStringLiteral("time"));
                    xml.writeTextElement(QStringLiteral("beats"),
                                         QString::number(master.numerator));
                    xml.writeTextElement(QStringLiteral("beat-type"),
                                         QString::number(master.denominator));
                    xml.writeEndElement();
                    lastNumerator = master.numerator;
                    lastDenominator = master.denominator;
                }
                if (index == 0) {
                    writeClefAndStaff(xml, part);
                }
                xml.writeEndElement();
            }

            if (master.repeatStart) {
                xml.writeStartElement(QStringLiteral("barline"));
                xml.writeAttribute(QStringLiteral("location"), QStringLiteral("left"));
                xml.writeStartElement(QStringLiteral("repeat"));
                xml.writeAttribute(QStringLiteral("direction"), QStringLiteral("forward"));
                xml.writeEndElement();
                xml.writeEndElement();
            }

            const double tempo = Timeline::tempoAtBar(score, index);
            if (tempo > 0.0 && tempo != lastTempo) {
                xml.writeStartElement(QStringLiteral("direction"));
                xml.writeAttribute(QStringLiteral("placement"), QStringLiteral("above"));
                xml.writeStartElement(QStringLiteral("direction-type"));
                xml.writeStartElement(QStringLiteral("metronome"));
                xml.writeTextElement(QStringLiteral("beat-unit"), QStringLiteral("quarter"));
                xml.writeTextElement(QStringLiteral("per-minute"),
                                     QString::number(tempo));
                xml.writeEndElement();
                xml.writeEndElement();
                xml.writeStartElement(QStringLiteral("sound"));
                xml.writeAttribute(QStringLiteral("tempo"), QString::number(tempo));
                xml.writeEndElement();
                xml.writeEndElement();
                lastTempo = tempo;
            }

            if (!master.section.isEmpty()) {
                xml.writeStartElement(QStringLiteral("direction"));
                xml.writeAttribute(QStringLiteral("placement"), QStringLiteral("above"));
                xml.writeStartElement(QStringLiteral("direction-type"));
                xml.writeTextElement(QStringLiteral("rehearsal"), master.section);
                xml.writeEndElement();
                xml.writeEndElement();
            }

            if (track < master.bars.size()) {
                const Bar bar = score.bars.value(master.bars.at(track));
                bool firstVoice = true;
                for (int slot = 0; slot < bar.voices.size(); ++slot) {
                    const auto voice = score.voices.constFind(bar.voices.at(slot));
                    if (voice == score.voices.constEnd() || voice->beats.isEmpty()) {
                        continue;
                    }
                    if (!firstVoice) {
                        // Every voice after the first starts where the last
                        // one did, and MusicXML says so by winding back.
                        xml.writeStartElement(QStringLiteral("backup"));
                        xml.writeTextElement(
                            QStringLiteral("duration"),
                            QString::number(master.length().numerator * divisions
                                            / master.length().denominator));
                        xml.writeEndElement();
                    }
                    firstVoice = false;

                    for (const int beatId : voice->beats) {
                        const auto beat = score.beats.constFind(beatId);
                        if (beat == score.beats.constEnd()) {
                            continue;
                        }
                        const Rational length =
                            score.rhythms.value(beat->rhythm, Rational(1));
                        const long long duration =
                            length.numerator * divisions / length.denominator;
                        const NoteValue::Written written = NoteValue::of(length);
                        const QString type =
                            typeOf(NoteValue::denominatorOf(written.value));

                        if (beat->notes.isEmpty()) {
                            xml.writeStartElement(QStringLiteral("note"));
                            xml.writeEmptyElement(QStringLiteral("rest"));
                            xml.writeTextElement(QStringLiteral("duration"),
                                                 QString::number(duration));
                            xml.writeTextElement(QStringLiteral("voice"),
                                                 QString::number(slot + 1));
                            if (!type.isEmpty()) {
                                xml.writeTextElement(QStringLiteral("type"), type);
                            }
                            xml.writeEndElement();
                            continue;
                        }

                        bool firstOfChord = true;
                        for (const int noteId : beat->notes) {
                            const auto note = score.notes.constFind(noteId);
                            if (note == score.notes.constEnd() || note->midi < 0) {
                                continue;
                            }
                            const Key::Spelling spelling =
                                Key::spell(note->midi, master.key);

                            xml.writeStartElement(QStringLiteral("note"));
                            // Every note of a chord after the first says so,
                            // which is how MusicXML stacks them: without it
                            // they would be read as one after another.
                            if (!firstOfChord) {
                                xml.writeEmptyElement(QStringLiteral("chord"));
                            }
                            firstOfChord = false;

                            if (part.isPercussion()) {
                                // Empty, and deliberately: where a drum sits on
                                // the staff is a decision about how to print it
                                // and nothing in a `.gp` says. The instrument it
                                // points at is what identifies it.
                                xml.writeEmptyElement(QStringLiteral("unpitched"));
                            } else {
                                xml.writeStartElement(QStringLiteral("pitch"));
                                xml.writeTextElement(
                                    QStringLiteral("step"),
                                    QString::fromLatin1(Steps[spelling.step]));
                                if (spelling.alteration != 0) {
                                    xml.writeTextElement(
                                        QStringLiteral("alter"),
                                        QString::number(spelling.alteration));
                                }
                                xml.writeTextElement(QStringLiteral("octave"),
                                                     QString::number(spelling.octave));
                                xml.writeEndElement();
                            }

                            xml.writeTextElement(QStringLiteral("duration"),
                                                 QString::number(duration));

                            if (note->tieDestination) {
                                xml.writeStartElement(QStringLiteral("tie"));
                                xml.writeAttribute(QStringLiteral("type"),
                                                   QStringLiteral("stop"));
                                xml.writeEndElement();
                            }
                            if (note->tieOrigin) {
                                xml.writeStartElement(QStringLiteral("tie"));
                                xml.writeAttribute(QStringLiteral("type"),
                                                   QStringLiteral("start"));
                                xml.writeEndElement();
                            }

                            if (part.isPercussion()) {
                                xml.writeStartElement(QStringLiteral("instrument"));
                                xml.writeAttribute(
                                    QStringLiteral("id"),
                                    QStringLiteral("P%1-I%2").arg(track + 1).arg(note->midi));
                                xml.writeEndElement();
                            }
                            xml.writeTextElement(QStringLiteral("voice"),
                                                 QString::number(slot + 1));
                            if (!type.isEmpty()) {
                                xml.writeTextElement(QStringLiteral("type"), type);
                            }
                            for (int dot = 0; dot < written.dots; ++dot) {
                                xml.writeEmptyElement(QStringLiteral("dot"));
                            }
                            if (!part.isPercussion()) {
                                writeNotations(xml, *note, part, true);
                            }
                            xml.writeEndElement();
                        }
                    }
                }
            }

            if (master.repeatEnd) {
                xml.writeStartElement(QStringLiteral("barline"));
                xml.writeAttribute(QStringLiteral("location"), QStringLiteral("right"));
                xml.writeStartElement(QStringLiteral("repeat"));
                xml.writeAttribute(QStringLiteral("direction"), QStringLiteral("backward"));
                if (master.repeatCount > 2) {
                    xml.writeAttribute(QStringLiteral("times"),
                                       QString::number(master.repeatCount));
                }
                xml.writeEndElement();
                xml.writeEndElement();
            }

            xml.writeEndElement();
        }
        xml.writeEndElement();
    }

    xml.writeEndElement();
    xml.writeEndDocument();
    return output;
}
