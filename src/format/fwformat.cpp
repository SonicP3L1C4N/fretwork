// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "fwformat.h"

#include "swing.h"

#include "zipreader.h"

#include <KZip>

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace
{
const QString ScoreEntry = QStringLiteral("score.json");
const QString ManifestEntry = QStringLiteral("manifest.json");

/** Written only when true, and read as false when absent: files stay small. */
void putFlag(QJsonObject &object, const QString &key, bool value)
{
    if (value) {
        object.insert(key, true);
    }
}

QString nameOf(Dynamic dynamic)
{
    switch (dynamic) {
    case Dynamic::PPP: return QStringLiteral("ppp");
    case Dynamic::PP:  return QStringLiteral("pp");
    case Dynamic::P:   return QStringLiteral("p");
    case Dynamic::MP:  return QStringLiteral("mp");
    case Dynamic::MF:  return QStringLiteral("mf");
    case Dynamic::F:   return QStringLiteral("f");
    case Dynamic::FF:  return QStringLiteral("ff");
    case Dynamic::FFF: return QStringLiteral("fff");
    }
    return QStringLiteral("mf");
}

Dynamic dynamicFrom(const QString &name)
{
    static const QHash<QString, Dynamic> known = {
        {QStringLiteral("ppp"), Dynamic::PPP}, {QStringLiteral("pp"), Dynamic::PP},
        {QStringLiteral("p"), Dynamic::P},     {QStringLiteral("mp"), Dynamic::MP},
        {QStringLiteral("mf"), Dynamic::MF},   {QStringLiteral("f"), Dynamic::F},
        {QStringLiteral("ff"), Dynamic::FF},   {QStringLiteral("fff"), Dynamic::FFF},
    };
    return known.value(name, Dynamic::MF);
}

QString nameOf(SlideType slide)
{
    switch (slide) {
    case SlideType::None:        return QString();
    case SlideType::Legato:      return QStringLiteral("legato");
    case SlideType::Shift:       return QStringLiteral("shift");
    case SlideType::OutDown:     return QStringLiteral("outDown");
    case SlideType::OutUp:       return QStringLiteral("outUp");
    case SlideType::InFromBelow: return QStringLiteral("inFromBelow");
    case SlideType::InFromAbove: return QStringLiteral("inFromAbove");
    }
    return QString();
}

SlideType slideFrom(const QString &name)
{
    static const QHash<QString, SlideType> known = {
        {QStringLiteral("legato"), SlideType::Legato},
        {QStringLiteral("shift"), SlideType::Shift},
        {QStringLiteral("outDown"), SlideType::OutDown},
        {QStringLiteral("outUp"), SlideType::OutUp},
        {QStringLiteral("inFromBelow"), SlideType::InFromBelow},
        {QStringLiteral("inFromAbove"), SlideType::InFromAbove},
    };
    return known.value(name, SlideType::None);
}

QJsonArray fromInts(const QList<int> &values)
{
    QJsonArray array;
    for (const int value : values) {
        array.append(value);
    }
    return array;
}

QList<int> toInts(const QJsonArray &array)
{
    QList<int> values;
    values.reserve(array.size());
    for (const QJsonValue &value : array) {
        values.append(value.toInt());
    }
    return values;
}

QJsonObject fromNote(const Note &note)
{
    QJsonObject object;
    object.insert(QStringLiteral("midi"), note.midi);
    object.insert(QStringLiteral("string"), note.string);
    object.insert(QStringLiteral("fret"), note.fret);

    putFlag(object, QStringLiteral("tieOrigin"), note.tieOrigin);
    putFlag(object, QStringLiteral("tieDestination"), note.tieDestination);
    putFlag(object, QStringLiteral("muted"), note.muted);
    putFlag(object, QStringLiteral("palmMuted"), note.palmMuted);
    putFlag(object, QStringLiteral("letRing"), note.letRing);
    putFlag(object, QStringLiteral("accent"), note.accent);
    putFlag(object, QStringLiteral("ghost"), note.ghost);
    putFlag(object, QStringLiteral("vibrato"), note.vibrato);
    putFlag(object, QStringLiteral("hammerOrigin"), note.hammerOrigin);
    putFlag(object, QStringLiteral("hammerDestination"), note.hammerDestination);
    putFlag(object, QStringLiteral("tapped"), note.tapped);
    putFlag(object, QStringLiteral("harmonic"), note.harmonic);

    if (note.slide != SlideType::None) {
        object.insert(QStringLiteral("slide"), nameOf(note.slide));
    }

    if (note.bended) {
        QJsonObject bend;
        bend.insert(QStringLiteral("originValue"), note.bendOriginValue);
        bend.insert(QStringLiteral("middleValue"), note.bendMiddleValue);
        bend.insert(QStringLiteral("destinationValue"), note.bendDestinationValue);
        bend.insert(QStringLiteral("originOffset"), note.bendOriginOffset);
        bend.insert(QStringLiteral("middleOffset1"), note.bendMiddleOffset1);
        bend.insert(QStringLiteral("middleOffset2"), note.bendMiddleOffset2);
        bend.insert(QStringLiteral("destinationOffset"), note.bendDestinationOffset);
        object.insert(QStringLiteral("bend"), bend);
    }
    return object;
}

Note toNote(const QJsonObject &object)
{
    Note note;
    note.midi = object.value(QStringLiteral("midi")).toInt(-1);
    note.string = object.value(QStringLiteral("string")).toInt(-1);
    note.fret = object.value(QStringLiteral("fret")).toInt();

    const auto flag = [&object](const char *key) {
        return object.value(QLatin1String(key)).toBool(false);
    };
    note.tieOrigin = flag("tieOrigin");
    note.tieDestination = flag("tieDestination");
    note.muted = flag("muted");
    note.palmMuted = flag("palmMuted");
    note.letRing = flag("letRing");
    note.accent = flag("accent");
    note.ghost = flag("ghost");
    note.vibrato = flag("vibrato");
    note.hammerOrigin = flag("hammerOrigin");
    note.hammerDestination = flag("hammerDestination");
    note.tapped = flag("tapped");
    note.harmonic = flag("harmonic");
    note.slide = slideFrom(object.value(QStringLiteral("slide")).toString());

    const QJsonObject bend = object.value(QStringLiteral("bend")).toObject();
    if (!bend.isEmpty()) {
        note.bended = true;
        note.bendOriginValue = bend.value(QStringLiteral("originValue")).toInt();
        note.bendMiddleValue = bend.value(QStringLiteral("middleValue")).toInt();
        note.bendDestinationValue = bend.value(QStringLiteral("destinationValue")).toInt();
        note.bendOriginOffset = bend.value(QStringLiteral("originOffset")).toInt();
        note.bendMiddleOffset1 = bend.value(QStringLiteral("middleOffset1")).toInt(-1);
        note.bendMiddleOffset2 = bend.value(QStringLiteral("middleOffset2")).toInt(-1);
        note.bendDestinationOffset =
            bend.value(QStringLiteral("destinationOffset")).toInt(100);
    }
    return note;
}

/** Tables are written keyed by id, because that is what they are. */
template<typename T, typename Convert>
QJsonObject fromTable(const QHash<int, T> &table, Convert convert)
{
    QJsonObject object;
    // Sorted, so that saving the same score twice produces the same bytes and
    // a version control system has something useful to show.
    QList<int> ids = table.keys();
    std::sort(ids.begin(), ids.end());
    for (const int id : std::as_const(ids)) {
        object.insert(QString::number(id), convert(table.value(id)));
    }
    return object;
}

template<typename T, typename Convert>
QHash<int, T> toTable(const QJsonObject &object, Convert convert)
{
    QHash<int, T> table;
    for (auto entry = object.constBegin(); entry != object.constEnd(); ++entry) {
        bool ok = false;
        const int id = entry.key().toInt(&ok);
        if (ok) {
            table.insert(id, convert(entry.value()));
        }
    }
    return table;
}

QByteArray manifest()
{
    QJsonObject object;
    object.insert(QStringLiteral("format"), Fw::FormatVersion);
    object.insert(QStringLiteral("application"), QStringLiteral("Fretwork"));
    // Which build wrote it, which is the first thing worth knowing about a
    // file that has arrived attached to a bug report.
    object.insert(QStringLiteral("wroteWith"), QStringLiteral(FRETWORK_VERSION));
    return QJsonDocument(object).toJson(QJsonDocument::Indented);
}

/**
 * Brings a document forward from the version that wrote it.
 *
 * One step at a time and in order, so that a file two versions behind is
 * carried through the same code a file one version behind is -- a migration
 * that jumped straight to the present would be a second description of every
 * change, and the one nobody ran would be the wrong one.
 *
 * There is one version, so there is nothing to do yet. The loop is here rather
 * than waiting to be written because the moment it is needed is the moment
 * somebody already has files, and that is the wrong moment to be designing
 * where the code goes.
 */
void migrate(QJsonObject &document, int from)
{
    for (int version = from; version < Fw::FormatVersion; ++version) {
        switch (version) {
        // case 1: turn a version-one document into a version-two one, here.
        default:
            break;
        }
    }
    Q_UNUSED(document);
}

QByteArray encode(const Score &score)
{
    QJsonObject root;

    QJsonObject about;
    about.insert(QStringLiteral("title"), score.title);
    about.insert(QStringLiteral("artist"), score.artist);
    about.insert(QStringLiteral("album"), score.album);
    if (!score.version.isEmpty()) {
        // Provenance: which Guitar Pro wrote the file this was imported from.
        // Not ours, and worth keeping -- it is the first thing to ask about
        // when an imported score turns out wrong.
        about.insert(QStringLiteral("importedFrom"), score.version);
    }
    root.insert(QStringLiteral("score"), about);

    QJsonArray tracks;
    for (const Track &track : score.tracks) {
        QJsonObject object;
        object.insert(QStringLiteral("name"), track.name);
        object.insert(QStringLiteral("instrument"), track.instrumentType);
        object.insert(QStringLiteral("program"), track.program);
        object.insert(QStringLiteral("tuning"), fromInts(track.tuning));
        if (track.capo != 0) {
            object.insert(QStringLiteral("capo"), track.capo);
        }
        tracks.append(object);
    }
    root.insert(QStringLiteral("tracks"), tracks);

    QJsonArray masterBars;
    for (const MasterBar &bar : score.masterBars) {
        QJsonObject object;
        object.insert(QStringLiteral("bars"), fromInts(bar.bars));
        object.insert(QStringLiteral("time"),
                      QJsonArray({bar.numerator, bar.denominator}));
        if (!bar.section.isEmpty()) {
            object.insert(QStringLiteral("section"), bar.section);
        }
        if (bar.tripletFeel != TripletFeel::None) {
            object.insert(QStringLiteral("tripletFeel"), Swing::tokenOf(bar.tripletFeel));
        }
        if (bar.repeatStart || bar.repeatEnd) {
            QJsonObject repeat;
            putFlag(repeat, QStringLiteral("start"), bar.repeatStart);
            putFlag(repeat, QStringLiteral("end"), bar.repeatEnd);
            if (bar.repeatCount) {
                repeat.insert(QStringLiteral("count"), bar.repeatCount);
            }
            object.insert(QStringLiteral("repeat"), repeat);
        }
        putFlag(object, QStringLiteral("alternateEndings"), bar.alternateEndings);
        masterBars.append(object);
    }
    root.insert(QStringLiteral("masterBars"), masterBars);

    root.insert(QStringLiteral("bars"), fromTable(score.bars, [](const Bar &bar) {
                    return QJsonValue(fromInts(bar.voices));
                }));
    root.insert(QStringLiteral("voices"), fromTable(score.voices, [](const Voice &voice) {
                    return QJsonValue(fromInts(voice.beats));
                }));
    root.insert(QStringLiteral("beats"), fromTable(score.beats, [](const Beat &beat) {
                    QJsonObject object;
                    object.insert(QStringLiteral("rhythm"), beat.rhythm);
                    object.insert(QStringLiteral("notes"), fromInts(beat.notes));
                    object.insert(QStringLiteral("dynamic"), nameOf(beat.dynamic));
                    putFlag(object, QStringLiteral("tremolo"), beat.tremolo);
                    putFlag(object, QStringLiteral("brush"), beat.brush);
                    return QJsonValue(object);
                }));
    root.insert(QStringLiteral("notes"), fromTable(score.notes, [](const Note &note) {
                    return QJsonValue(fromNote(note));
                }));
    root.insert(QStringLiteral("rhythms"), fromTable(score.rhythms, [](const Rational &value) {
                    // A duration in quarters, exactly as it is held in memory.
                    return QJsonValue(QJsonArray({double(value.numerator),
                                                  double(value.denominator)}));
                }));

    QJsonArray tempos;
    for (const TempoChange &tempo : score.tempos) {
        QJsonObject object;
        object.insert(QStringLiteral("bar"), tempo.bar);
        object.insert(QStringLiteral("position"), tempo.position);
        object.insert(QStringLiteral("bpm"), tempo.quarterBpm);
        tempos.append(object);
    }
    root.insert(QStringLiteral("tempos"), tempos);

    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

Score decode(const QByteArray &json, QString *error)
{
    QJsonParseError parsed{};
    const QJsonDocument document = QJsonDocument::fromJson(json, &parsed);
    if (parsed.error != QJsonParseError::NoError) {
        if (error) {
            *error = QStringLiteral("score.json is not valid JSON: %1")
                         .arg(parsed.errorString());
        }
        return {};
    }
    const QJsonObject root = document.object();

    Score score;
    const QJsonObject about = root.value(QStringLiteral("score")).toObject();
    score.title = about.value(QStringLiteral("title")).toString();
    score.artist = about.value(QStringLiteral("artist")).toString();
    score.album = about.value(QStringLiteral("album")).toString();
    score.version = about.value(QStringLiteral("importedFrom")).toString();

    const QJsonArray tracks = root.value(QStringLiteral("tracks")).toArray();
    for (const QJsonValue &value : tracks) {
        const QJsonObject object = value.toObject();
        Track track;
        track.name = object.value(QStringLiteral("name")).toString();
        track.instrumentType = object.value(QStringLiteral("instrument")).toString();
        track.program = object.value(QStringLiteral("program")).toInt();
        track.tuning = toInts(object.value(QStringLiteral("tuning")).toArray());
        track.capo = object.value(QStringLiteral("capo")).toInt();
        score.tracks.append(track);
    }

    const QJsonArray masterBars = root.value(QStringLiteral("masterBars")).toArray();
    for (const QJsonValue &value : masterBars) {
        const QJsonObject object = value.toObject();
        MasterBar bar;
        bar.bars = toInts(object.value(QStringLiteral("bars")).toArray());
        const QJsonArray time = object.value(QStringLiteral("time")).toArray();
        if (time.size() == 2) {
            bar.numerator = time.at(0).toInt(4);
            bar.denominator = time.at(1).toInt(4);
        }
        bar.section = object.value(QStringLiteral("section")).toString();
        bar.tripletFeel =
            Swing::fromToken(object.value(QStringLiteral("tripletFeel")).toString());
        const QJsonObject repeat = object.value(QStringLiteral("repeat")).toObject();
        bar.repeatStart = repeat.value(QStringLiteral("start")).toBool();
        bar.repeatEnd = repeat.value(QStringLiteral("end")).toBool();
        bar.repeatCount = repeat.value(QStringLiteral("count")).toInt();
        bar.alternateEndings = object.value(QStringLiteral("alternateEndings")).toBool();
        score.masterBars.append(bar);
    }

    score.bars = toTable<Bar>(root.value(QStringLiteral("bars")).toObject(),
                              [](const QJsonValue &value) {
                                  return Bar{toInts(value.toArray())};
                              });
    score.voices = toTable<Voice>(root.value(QStringLiteral("voices")).toObject(),
                                  [](const QJsonValue &value) {
                                      return Voice{toInts(value.toArray())};
                                  });
    score.beats = toTable<Beat>(root.value(QStringLiteral("beats")).toObject(),
                                [](const QJsonValue &value) {
                                    const QJsonObject object = value.toObject();
                                    Beat beat;
                                    beat.rhythm = object.value(QStringLiteral("rhythm")).toInt(-1);
                                    beat.notes = toInts(object.value(QStringLiteral("notes")).toArray());
                                    beat.dynamic = dynamicFrom(object.value(QStringLiteral("dynamic")).toString());
                                    beat.tremolo = object.value(QStringLiteral("tremolo")).toBool();
                                    beat.brush = object.value(QStringLiteral("brush")).toBool();
                                    return beat;
                                });
    score.notes = toTable<Note>(root.value(QStringLiteral("notes")).toObject(),
                                [](const QJsonValue &value) {
                                    return toNote(value.toObject());
                                });
    score.rhythms = toTable<Rational>(root.value(QStringLiteral("rhythms")).toObject(),
                                      [](const QJsonValue &value) {
                                          const QJsonArray pair = value.toArray();
                                          if (pair.size() != 2 || pair.at(1).toInteger() == 0) {
                                              return Rational(1);
                                          }
                                          return Rational(pair.at(0).toInteger(),
                                                          pair.at(1).toInteger());
                                      });

    const QJsonArray tempos = root.value(QStringLiteral("tempos")).toArray();
    for (const QJsonValue &value : tempos) {
        const QJsonObject object = value.toObject();
        TempoChange tempo;
        tempo.bar = object.value(QStringLiteral("bar")).toInt();
        tempo.position = object.value(QStringLiteral("position")).toDouble();
        tempo.quarterBpm = object.value(QStringLiteral("bpm")).toDouble(120);
        score.tempos.append(tempo);
    }

    if (score.isEmpty() && error) {
        *error = QStringLiteral("the file holds no tracks or no bars");
    }
    return score;
}
}

QString Fw::extension()
{
    return QStringLiteral("fw");
}

bool Fw::looksLikeOurs(const QString &path)
{
    return QFileInfo(path).suffix().compare(extension(), Qt::CaseInsensitive) == 0;
}

bool Fw::write(const Score &score, const QString &path, QString *error)
{
    if (score.isEmpty()) {
        if (error) {
            *error = QStringLiteral("there is nothing to save");
        }
        return false;
    }

    // Written by KArchive and read by our own reader, which the tests already
    // check against archives KArchive wrote. Writing a ZIP is the part nobody
    // disagrees about; it was reading one that needed doing ourselves.
    KZip archive(path);
    if (!archive.open(QIODevice::WriteOnly)) {
        if (error) {
            *error = QStringLiteral("cannot write to %1").arg(path);
        }
        return false;
    }
    const bool ok = archive.writeFile(ManifestEntry, manifest())
        && archive.writeFile(ScoreEntry, encode(score));
    if (!archive.close() || !ok) {
        if (error) {
            *error = QStringLiteral("could not finish writing %1").arg(path);
        }
        return false;
    }
    return true;
}

int Fw::versionOf(const QString &path)
{
    const QByteArray json = Zip::readEntry(path, ManifestEntry);
    if (json.isNull()) {
        return 1;
    }
    const QJsonObject object = QJsonDocument::fromJson(json).object();
    const QJsonValue format = object.value(QStringLiteral("format"));
    return format.isDouble() ? format.toInt() : 1;
}

Score Fw::read(const QString &path, QString *error)
{
    // Before the document, because a document from the future is one this
    // reader would misunderstand rather than one it would fail to parse.
    const int version = versionOf(path);
    if (version > FormatVersion) {
        if (error) {
            *error = QStringLiteral("this file is Fretwork format %1 and this program "
                                    "understands up to %2: it needs a newer Fretwork")
                         .arg(version)
                         .arg(FormatVersion);
        }
        return {};
    }

    QString why;
    const QByteArray json = Zip::readEntry(path, ScoreEntry, &why);
    if (json.isNull()) {
        if (error) {
            *error = why.startsWith(QLatin1String("no score.json"))
                ? QStringLiteral("no score.json inside: this is not a Fretwork file")
                : why;
        }
        return {};
    }
    QJsonParseError parsed;
    QJsonDocument document = QJsonDocument::fromJson(json, &parsed);
    if (parsed.error != QJsonParseError::NoError || !document.isObject()) {
        if (error) {
            *error = QStringLiteral("score.json is not readable JSON: %1")
                         .arg(parsed.errorString());
        }
        return {};
    }

    QJsonObject root = document.object();
    migrate(root, version);
    return decode(QJsonDocument(root).toJson(QJsonDocument::Compact), error);
}
