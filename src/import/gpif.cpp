// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "gpif.h"

#include "swing.h"
#include "zipreader.h"

#include <QDomDocument>
#include <QFile>
#include <QFileInfo>
#include <QXmlStreamReader>

namespace
{
/**
 * Deeper than any tablature nests, and shallower than the point where building
 * a tree of it stops being worth the wait.
 *
 * A gpif document is about ten deep at its worst -- GPIF, Tracks, Track,
 * Staves, Staff, Properties, Property, and a couple of wrappers. The number
 * here is that with two orders of magnitude of room, because the cost of being
 * generous is nothing and the cost of being wrong is refusing a real file.
 */
constexpr int MaximumDepth = 100;

/**
 * Because QDomDocument builds the whole tree before anybody asks it anything,
 * and it does so at a cost that grows faster than the document does. Nesting
 * empty elements 100,000 deep is 875 bytes on disk and twelve seconds in
 * setContent(); 200,000 is a minute and a half. Neither is a crash, and that
 * is what makes it worth catching -- the program simply stops answering, with
 * nothing on screen to say why, and the file that did it arrived by mail
 * looking like a song.
 *
 * The scan is a pull parser, which is linear and keeps no tree, and it stops
 * at the first element past the limit rather than reading to the end.
 */
bool nestsTooDeeply(const QByteArray &xml)
{
    QXmlStreamReader reader(xml);
    int depth = 0;
    while (!reader.atEnd()) {
        switch (reader.readNext()) {
        case QXmlStreamReader::StartElement:
            if (++depth > MaximumDepth) {
                return true;
            }
            break;
        case QXmlStreamReader::EndElement:
            --depth;
            break;
        default:
            break;
        }
    }
    return false;
}

/** gpif indents its values onto lines of their own, so everything is trimmed. */
QString childText(const QDomElement &parent, const QString &tag,
                  const QString &fallback = QString())
{
    if (parent.isNull()) {
        return fallback;
    }
    const QDomElement child = parent.firstChildElement(tag);
    return child.isNull() ? fallback : child.text().trimmed();
}

/** "0 176 352 528" -- the format's way of writing every list it has. */
QList<int> ints(const QString &value)
{
    QList<int> out;
    const QStringList parts = value.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    out.reserve(parts.size());
    for (const QString &part : parts) {
        out.append(part.trimmed().toInt());
    }
    return out;
}

/**
 * gpif hangs most of its data off `<Property name="...">` children rather than
 * off elements named for what they hold, so nearly every read goes through
 * here.
 */
QDomElement property(const QDomElement &owner, const QString &name)
{
    const QDomElement holder = owner.firstChildElement(QStringLiteral("Properties"));
    if (holder.isNull()) {
        return {};
    }
    for (QDomElement candidate = holder.firstChildElement(QStringLiteral("Property"));
         !candidate.isNull();
         candidate = candidate.nextSiblingElement(QStringLiteral("Property"))) {
        if (candidate.attribute(QStringLiteral("name")) == name) {
            return candidate;
        }
    }
    return {};
}

/** A technique that is either there or not: `<Property name="Muted"><Enable/>`. */
bool hasProperty(const QDomElement &owner, const QString &name)
{
    return !property(owner, name).isNull();
}

int propertyInt(const QDomElement &owner, const QString &name,
                const QString &tag, int fallback = 0)
{
    const QDomElement found = property(owner, name);
    if (found.isNull()) {
        return fallback;
    }
    const QString value = childText(found, tag);
    return value.isEmpty() ? fallback : int(value.toDouble());
}

Dynamic dynamicFrom(const QString &name)
{
    static const QHash<QString, Dynamic> known = {
        {QStringLiteral("PPP"), Dynamic::PPP}, {QStringLiteral("PP"), Dynamic::PP},
        {QStringLiteral("P"), Dynamic::P},     {QStringLiteral("MP"), Dynamic::MP},
        {QStringLiteral("MF"), Dynamic::MF},   {QStringLiteral("F"), Dynamic::F},
        {QStringLiteral("FF"), Dynamic::FF},   {QStringLiteral("FFF"), Dynamic::FFF},
    };
    return known.value(name, Dynamic::MF);
}

/**
 * A slide is a bit field rather than a name, and the bits are not documented
 * anywhere except in the implementations that worked them out.
 */
SlideType slideFrom(int flags)
{
    if (flags & 0x02) {
        return SlideType::Legato;
    }
    if (flags & 0x01) {
        return SlideType::Shift;
    }
    if (flags & 0x04) {
        return SlideType::OutDown;
    }
    if (flags & 0x08) {
        return SlideType::OutUp;
    }
    if (flags & 0x10) {
        return SlideType::InFromBelow;
    }
    if (flags & 0x20) {
        return SlideType::InFromAbove;
    }
    return SlideType::None;
}

/** The whole note, halved as many times as the name says. */
Rational noteValue(const QString &name)
{
    static const QHash<QString, Rational> known = {
        {QStringLiteral("Whole"), Rational(4)},    {QStringLiteral("Half"), Rational(2)},
        {QStringLiteral("Quarter"), Rational(1)},  {QStringLiteral("Eighth"), Rational(1, 2)},
        {QStringLiteral("16th"), Rational(1, 4)},  {QStringLiteral("32nd"), Rational(1, 8)},
        {QStringLiteral("64th"), Rational(1, 16)}, {QStringLiteral("128th"), Rational(1, 32)},
        {QStringLiteral("256th"), Rational(1, 64)},
    };
    return known.value(name, Rational(1));
}

Track readTrack(const QDomElement &element)
{
    Track track;
    track.name = childText(element, QStringLiteral("Name"));
    if (track.name.isEmpty()) {
        track.name = QStringLiteral("Track");
    }
    const QDomElement set = element.firstChildElement(QStringLiteral("InstrumentSet"));
    track.instrumentType = childText(set, QStringLiteral("Type"));

    const QDomElement midi = element.firstChildElement(QStringLiteral("Sounds"))
                                 .firstChildElement(QStringLiteral("Sound"))
                                 .firstChildElement(QStringLiteral("MIDI"));
    track.program = childText(midi, QStringLiteral("Program"), QStringLiteral("0")).toInt();

    const QDomElement staff = element.firstChildElement(QStringLiteral("Staves"))
                                  .firstChildElement(QStringLiteral("Staff"));
    if (!staff.isNull()) {
        const QDomElement tuning = property(staff, QStringLiteral("Tuning"));
        track.tuning = ints(childText(tuning, QStringLiteral("Pitches")));
        track.capo = propertyInt(staff, QStringLiteral("CapoFret"), QStringLiteral("Fret"));
    }
    return track;
}

MasterBar readMasterBar(const QDomElement &element)
{
    MasterBar bar;
    bar.bars = ints(childText(element, QStringLiteral("Bars")));

    const QStringList signature =
        childText(element, QStringLiteral("Time"), QStringLiteral("4/4")).split(QLatin1Char('/'));
    if (signature.size() == 2) {
        bar.numerator = signature.at(0).toInt();
        bar.denominator = signature.at(1).toInt();
    }
    if (bar.numerator <= 0 || bar.denominator <= 0) {
        bar.numerator = 4;
        bar.denominator = 4;
    }

    bar.section = childText(element.firstChildElement(QStringLiteral("Section")),
                            QStringLiteral("Text"));

    // Absent means straight. Guitar Pro writes the element only where there is
    // a feel and never writes one saying there is none, which is why this is
    // not inherited from the bar before it.
    bar.tripletFeel = Swing::fromToken(childText(element, QStringLiteral("TripletFeel")));

    // The signature is a signed count of accidentals, which is the shape the
    // circle of fifths has and the shape the model keeps. Anything outside
    // seven either way is not a key signature, so it is read as none rather
    // than refused: a file that is wrong about its key is not a file nobody
    // can open, and none is what a score that never says already means.
    const QDomElement key = element.firstChildElement(QStringLiteral("Key"));
    if (!key.isNull()) {
        const Key::Signature read{childText(key, QStringLiteral("AccidentalCount")).toInt(),
                                  childText(key, QStringLiteral("Mode")) == QLatin1String("Minor")};
        if (Key::isValid(read)) {
            bar.key = read;
        }
    }

    const QDomElement repeat = element.firstChildElement(QStringLiteral("Repeat"));
    if (!repeat.isNull()) {
        bar.repeatStart = repeat.attribute(QStringLiteral("start")) == QLatin1String("true");
        bar.repeatEnd = repeat.attribute(QStringLiteral("end")) == QLatin1String("true");
        bar.repeatCount = repeat.attribute(QStringLiteral("count"), QStringLiteral("0")).toInt();
    }
    bar.alternateEndings =
        !element.firstChildElement(QStringLiteral("AlternateEndings")).isNull();
    return bar;
}

Note readNote(const QDomElement &element)
{
    Note note;
    note.midi = propertyInt(element, QStringLiteral("Midi"), QStringLiteral("Number"), -1);
    note.string = propertyInt(element, QStringLiteral("String"), QStringLiteral("String"), -1);
    note.fret = propertyInt(element, QStringLiteral("Fret"), QStringLiteral("Fret"));

    const QDomElement tie = element.firstChildElement(QStringLiteral("Tie"));
    if (!tie.isNull()) {
        note.tieOrigin = tie.attribute(QStringLiteral("origin")) == QLatin1String("true");
        note.tieDestination =
            tie.attribute(QStringLiteral("destination")) == QLatin1String("true");
    }

    note.letRing = !element.firstChildElement(QStringLiteral("LetRing")).isNull();
    note.vibrato = !element.firstChildElement(QStringLiteral("Vibrato")).isNull();
    note.accent = !element.firstChildElement(QStringLiteral("Accent")).isNull();
    note.ghost = !element.firstChildElement(QStringLiteral("AntiAccent")).isNull();

    note.muted = hasProperty(element, QStringLiteral("Muted"));
    note.palmMuted = hasProperty(element, QStringLiteral("PalmMuted"));
    note.hammerOrigin = hasProperty(element, QStringLiteral("HopoOrigin"));
    note.hammerDestination = hasProperty(element, QStringLiteral("HopoDestination"));
    note.tapped = hasProperty(element, QStringLiteral("Tapped"));
    note.harmonic = hasProperty(element, QStringLiteral("Harmonic"));
    note.slide = slideFrom(
        propertyInt(element, QStringLiteral("Slide"), QStringLiteral("Flags")));

    note.bended = hasProperty(element, QStringLiteral("Bended"));
    if (note.bended) {
        const auto value = [&element](const char *name) {
            return propertyInt(element, QString::fromLatin1(name), QStringLiteral("Float"));
        };
        note.bendOriginValue = value("BendOriginValue");
        note.bendMiddleValue = value("BendMiddleValue");
        note.bendDestinationValue = value("BendDestinationValue");
        note.bendOriginOffset = value("BendOriginOffset");
        note.bendDestinationOffset =
            propertyInt(element, QStringLiteral("BendDestinationOffset"),
                        QStringLiteral("Float"), 100);
        note.bendMiddleOffset1 =
            propertyInt(element, QStringLiteral("BendMiddleOffset1"),
                        QStringLiteral("Float"), -1);
        note.bendMiddleOffset2 =
            propertyInt(element, QStringLiteral("BendMiddleOffset2"),
                        QStringLiteral("Float"), -1);
    }
    return note;
}

Rational readRhythm(const QDomElement &element)
{
    Rational value = noteValue(childText(element, QStringLiteral("NoteValue"),
                                         QStringLiteral("Quarter")));

    const QDomElement dot = element.firstChildElement(QStringLiteral("AugmentationDot"));
    if (!dot.isNull()) {
        // One dot is a half again, two is three quarters again: 2 - 1/2^count.
        const int count = dot.attribute(QStringLiteral("count"), QStringLiteral("1")).toInt();
        value = value * Rational((qint64(1) << count) * 2 - 1, qint64(1) << count);
    }

    const QDomElement tuplet = element.firstChildElement(QStringLiteral("PrimaryTuplet"));
    if (!tuplet.isNull()) {
        const int num = tuplet.attribute(QStringLiteral("num")).toInt();
        const int den = tuplet.attribute(QStringLiteral("den")).toInt();
        if (num > 0 && den > 0) {
            value = value * Rational(den, num);
        }
    }
    return value;
}

/**
 * Tempo is not on the bar. It is an automation on the master track, carrying a
 * value of two numbers: the rate, and which note the rate counts.
 */
QList<TempoChange> readTempos(const QDomElement &master)
{
    static const QHash<int, Rational> beat = {
        {1, Rational(1, 2)}, {2, Rational(1)}, {3, Rational(3, 2)},
        {4, Rational(2)},    {5, Rational(3)},
    };

    QList<TempoChange> tempos;
    const QDomElement automations = master.firstChildElement(QStringLiteral("Automations"));
    for (QDomElement automation = automations.firstChildElement(QStringLiteral("Automation"));
         !automation.isNull();
         automation = automation.nextSiblingElement(QStringLiteral("Automation"))) {
        if (childText(automation, QStringLiteral("Type")) != QLatin1String("Tempo")) {
            continue;
        }
        const QStringList value = childText(automation, QStringLiteral("Value"))
                                      .split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (value.isEmpty()) {
            continue;
        }
        TempoChange change;
        change.bar = childText(automation, QStringLiteral("Bar"), QStringLiteral("0")).toInt();
        change.position =
            childText(automation, QStringLiteral("Position"), QStringLiteral("0")).toDouble();
        const int counts = value.size() > 1 ? value.at(1).toInt() : 2;
        change.quarterBpm = value.at(0).toDouble() * beat.value(counts, Rational(1)).toDouble();
        tempos.append(change);
    }

    std::sort(tempos.begin(), tempos.end(), [](const TempoChange &a, const TempoChange &b) {
        return a.bar == b.bar ? a.position < b.position : a.bar < b.bar;
    });
    return tempos;
}

/** Every child of `container`, keyed by its id attribute. */
template<typename T, typename Reader>
QHash<int, T> readTable(const QDomElement &root, const QString &container,
                        const QString &item, Reader reader)
{
    QHash<int, T> table;
    const QDomElement holder = root.firstChildElement(container);
    for (QDomElement element = holder.firstChildElement(item); !element.isNull();
         element = element.nextSiblingElement(item)) {
        table.insert(element.attribute(QStringLiteral("id")).toInt(), reader(element));
    }
    return table;
}
}

Score Gpif::parse(const QByteArray &xml, QString *error)
{
    if (nestsTooDeeply(xml)) {
        if (error) {
            *error = QStringLiteral("its elements nest more than %1 deep, which no tablature does")
                         .arg(MaximumDepth);
        }
        return {};
    }

    QDomDocument document;
    const QDomDocument::ParseResult parsed = document.setContent(xml);
    if (!parsed) {
        if (error) {
            *error = QStringLiteral("line %1: %2").arg(parsed.errorLine).arg(parsed.errorMessage);
        }
        return {};
    }

    const QDomElement root = document.documentElement();
    if (root.tagName() != QLatin1String("GPIF")) {
        if (error) {
            *error = QStringLiteral("not a gpif document: the root element is <%1>")
                         .arg(root.tagName());
        }
        return {};
    }

    Score score;
    score.version = childText(root, QStringLiteral("GPVersion"));

    const QDomElement heading = root.firstChildElement(QStringLiteral("Score"));
    score.title = childText(heading, QStringLiteral("Title"));
    score.artist = childText(heading, QStringLiteral("Artist"));
    score.album = childText(heading, QStringLiteral("Album"));

    const QDomElement tracks = root.firstChildElement(QStringLiteral("Tracks"));
    for (QDomElement track = tracks.firstChildElement(QStringLiteral("Track"));
         !track.isNull(); track = track.nextSiblingElement(QStringLiteral("Track"))) {
        score.tracks.append(readTrack(track));
    }

    const QDomElement masterBars = root.firstChildElement(QStringLiteral("MasterBars"));
    for (QDomElement bar = masterBars.firstChildElement(QStringLiteral("MasterBar"));
         !bar.isNull(); bar = bar.nextSiblingElement(QStringLiteral("MasterBar"))) {
        score.masterBars.append(readMasterBar(bar));
    }

    score.bars = readTable<Bar>(root, QStringLiteral("Bars"), QStringLiteral("Bar"),
                                [](const QDomElement &element) {
                                    return Bar{ints(childText(element, QStringLiteral("Voices")))};
                                });
    score.voices = readTable<Voice>(root, QStringLiteral("Voices"), QStringLiteral("Voice"),
                                    [](const QDomElement &element) {
                                        return Voice{ints(childText(element, QStringLiteral("Beats")))};
                                    });
    score.beats = readTable<Beat>(root, QStringLiteral("Beats"), QStringLiteral("Beat"),
                                  [](const QDomElement &element) {
                                      Beat beat;
                                      beat.rhythm = element.firstChildElement(QStringLiteral("Rhythm"))
                                                        .attribute(QStringLiteral("ref"), QStringLiteral("-1"))
                                                        .toInt();
                                      beat.notes = ints(childText(element, QStringLiteral("Notes")));
                                      beat.dynamic = dynamicFrom(childText(element, QStringLiteral("Dynamic")));
                                      beat.tremolo = !element.firstChildElement(QStringLiteral("Tremolo")).isNull();
                                      // "1/8", "1/16", "1/32" -- how fast it is
                                      // picked, which the file says and which is
                                      // the difference between two effects.
                                      if (beat.tremolo) {
                                          const QStringList halves =
                                              childText(element, QStringLiteral("Tremolo"))
                                                  .split(QLatin1Char('/'));
                                          if (halves.size() == 2 && halves.at(1).toInt() > 0) {
                                              // Times four: a file counts note
                                              // values against a semibreve and
                                              // this model counts them in
                                              // quarters, so its 1/8 is a
                                              // quaver and a quaver is a half.
                                              beat.tremoloValue =
                                                  Rational(4 * halves.at(0).toInt(),
                                                           halves.at(1).toInt());
                                          }
                                      }
                                      beat.brush = hasProperty(element, QStringLiteral("Brush"));
                                      return beat;
                                  });
    score.notes = readTable<Note>(root, QStringLiteral("Notes"), QStringLiteral("Note"), readNote);
    score.rhythms = readTable<Rational>(root, QStringLiteral("Rhythms"),
                                        QStringLiteral("Rhythm"), readRhythm);

    score.tempos = readTempos(root.firstChildElement(QStringLiteral("MasterTrack")));

    if (score.isEmpty() && error) {
        *error = QStringLiteral("the document holds no tracks or no bars");
    }
    return score;
}

Score Gpif::read(const QString &path, QString *error)
{
    QString why;
    const QByteArray document =
        Zip::readEntry(path, QStringLiteral("Content/score.gpif"), &why);

    if (document.isNull()) {
        if (error) {
            // GP6 and earlier are ZIPs too, and their contents are not this.
            // Saying which format it is not turns a shrug into a bug report.
            *error = why.startsWith(QLatin1String("no Content/score.gpif"))
                ? QStringLiteral("no Content/score.gpif inside: this is not a "
                                 "Guitar Pro 7 or 8 file")
                : why;
        }
        return {};
    }
    return parse(document, error);
}
