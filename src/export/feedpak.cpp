// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "feedpak.h"

#include "renderer.h"
#include "timeline.h"

#include <KLocalizedString>
#include <KZip>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>

#include <algorithm>
#include <QJsonDocument>
#include <QTemporaryDir>

namespace
{
/** Standard tuning, which is what a pack's zeros mean. */
const QList<int> StandardGuitar = {40, 45, 50, 55, 59, 64};
const QList<int> StandardBass = {28, 33, 38, 43};

/**
 * A part's tuning as the offsets a pack writes.
 *
 * Every pack on this machine is in standard tuning and writes six zeros, and
 * six zeros are not pitches -- so the field is semitones away from standard.
 * An instrument with a string count nobody has a standard for is written as
 * zeros, which is the only honest answer: the alternative is inventing a
 * reference nothing else shares.
 */
QList<int> offsetsOf(const Track &track)
{
    const QList<int> &standard = track.tuning.size() == 4 ? StandardBass : StandardGuitar;
    QList<int> offsets;
    for (int string = 0; string < track.tuning.size(); ++string) {
        offsets.append(string < standard.size() ? track.tuning.at(string) - standard.at(string)
                                                : 0);
    }
    return offsets;
}

/** An id a file name and a key can both be: lower case, no spaces. */
QString identifierFor(const QString &name, int fallback)
{
    QString id;
    for (const QChar letter : name.toLower()) {
        if (letter.isLetterOrNumber()) {
            id += letter;
        } else if (!id.isEmpty() && !id.endsWith(QLatin1Char('_'))) {
            id += QLatin1Char('_');
        }
    }
    while (id.endsWith(QLatin1Char('_'))) {
        id.chop(1);
    }
    return id.isEmpty() ? QStringLiteral("part_%1").arg(fallback + 1) : id;
}

/** YAML's own quoting, which is single quotes with the quote doubled. */
QString quoted(const QString &text)
{
    QString escaped = text;
    escaped.replace(QLatin1Char('\''), QStringLiteral("''"));
    return QLatin1Char('\'') + escaped + QLatin1Char('\'');
}
}

QList<int> Feedpak::playableParts(const Score &score)
{
    QList<int> parts;
    for (int track = 0; track < score.tracks.size(); ++track) {
        // Asked of the instrument and not of the tuning, which is the same
        // trap the importer already documents: a drum kit imports with a
        // tuning of six zeros rather than with none, so a kit looks fretted to
        // anything that only counts strings. An arrangement is a list of
        // strings and frets, and a pack's drums are a `drum_tab.json` of their
        // own, which is a different file and a later stage.
        const Track &part = score.tracks.at(track);
        if (!part.isPercussion() && !part.tuning.isEmpty()) {
            parts.append(track);
        }
    }
    return parts;
}

QString Feedpak::stemIdFor(const Score &score, int track)
{
    if (track < 0 || track >= score.tracks.size()) {
        return QString();
    }
    return identifierFor(score.tracks.at(track).name, track);
}

QByteArray Feedpak::manifestFor(const Score &score, double duration)
{
    QStringList lines;
    lines << QStringLiteral("title: %1").arg(quoted(score.title));
    lines << QStringLiteral("artist: %1").arg(quoted(score.artist));
    lines << QStringLiteral("album: %1").arg(quoted(score.album));
    lines << QStringLiteral("duration: %1").arg(duration, 0, 'f', 3);
    lines << QStringLiteral("feedpak_version: '1.2.0'");
    lines << QStringLiteral("authors:");
    lines << QStringLiteral("- name: 'Fretwork'");

    // A stem for every part, including the kit: the audio and the arrangements
    // are different lists, and a learner muting the drums needs the drums as a
    // file whether or not anything is written down for them.
    lines << QStringLiteral("stems:");
    lines << QStringLiteral("- id: full");
    lines << QStringLiteral("  file: 'stems/full.wav'");
    for (int track = 0; track < score.tracks.size(); ++track) {
        const QString id = stemIdFor(score, track);
        lines << QStringLiteral("- id: %1").arg(id);
        lines << QStringLiteral("  file: 'stems/%1.wav'").arg(id);
    }

    lines << QStringLiteral("arrangements:");
    for (const int track : playableParts(score)) {
        const Track &part = score.tracks.at(track);
        const QString id = identifierFor(part.name, track);
        lines << QStringLiteral("- id: %1").arg(id);
        lines << QStringLiteral("  name: %1").arg(quoted(part.name));
        lines << QStringLiteral("  file: 'arrangements/%1.json'").arg(id);
        lines << QStringLiteral("  tuning:");
        for (const int offset : offsetsOf(part)) {
            lines << QStringLiteral("  - %1").arg(offset);
        }
        lines << QStringLiteral("  capo: %1").arg(part.capo);
    }

    return (lines.join(QLatin1Char('\n')) + QLatin1Char('\n')).toUtf8();
}

QJsonObject Feedpak::arrangementFor(const Score &score, int track, const QList<int> &order)
{
    QJsonObject arrangement;
    if (track < 0 || track >= score.tracks.size()) {
        return arrangement;
    }
    const Track &part = score.tracks.at(track);
    const Timeline::Clock clock(score, order);

    QJsonArray tuning;
    for (const int offset : offsetsOf(part)) {
        tuning.append(offset);
    }
    arrangement.insert(QStringLiteral("name"), part.name);
    arrangement.insert(QStringLiteral("tuning"), tuning);
    arrangement.insert(QStringLiteral("capo"), part.capo);

    QJsonArray notes;
    const QList<Timeline::NoteEvent> events = Timeline::notesFor(score, track, order);
    for (int index = 0; index < events.size(); ++index) {
        const Timeline::NoteEvent &event = events.at(index);
        if (event.string < 0) {
            continue;
        }
        const double at = clock.secondsAt(event.start);

        QJsonObject note;
        note.insert(QStringLiteral("t"), at);
        note.insert(QStringLiteral("s"), event.string);
        note.insert(QStringLiteral("f"), event.fret);
        note.insert(QStringLiteral("sus"), clock.secondsAt(event.end) - at);

        // A hammer-on and a pull-off are one flag in this program, because
        // what they are is the same act. A pack wants them apart, and the
        // difference is which way the pitch went -- so it is worked out here
        // rather than guessed, and a legato note with nothing before it is
        // neither.
        const bool up = index > 0 && event.pitch > events.at(index - 1).pitch;
        const bool down = index > 0 && event.pitch < events.at(index - 1).pitch;
        note.insert(QStringLiteral("ho"), event.legato && up);
        note.insert(QStringLiteral("po"), event.legato && down);

        note.insert(QStringLiteral("pm"), event.palmMuted);
        note.insert(QStringLiteral("ln"), event.letRing);
        note.insert(QStringLiteral("mt"), event.muted);
        note.insert(QStringLiteral("ac"), event.accent);

        // How far the note bends, in semitones: the furthest the curve gets
        // from where it started. Nought where it does not bend, which is the
        // great majority of notes.
        int cents = 0;
        for (const Timeline::BendPoint &point : event.bend) {
            cents = std::max(cents, point.cents);
        }
        note.insert(QStringLiteral("bn"), cents / 100.0);

        // Everything this program cannot honestly say. Written as absent
        // rather than left out, because a reader filling in a missing field
        // with its own default is a reader deciding something about somebody's
        // playing that nobody told it.
        for (const QString &absent : {QStringLiteral("sl"), QStringLiteral("slu"),
                                      QStringLiteral("rh"), QStringLiteral("pkd")}) {
            note.insert(absent, -1);
        }
        for (const QString &absent :
             {QStringLiteral("hm"), QStringLiteral("hp"), QStringLiteral("tr"),
              QStringLiteral("tp"), QStringLiteral("vb"), QStringLiteral("fhm"),
              QStringLiteral("plk"), QStringLiteral("slp"), QStringLiteral("ig")}) {
            note.insert(absent, false);
        }
        notes.append(note);
    }
    arrangement.insert(QStringLiteral("notes"), notes);
    return arrangement;
}

bool Feedpak::write(const Score &score, const QList<int> &order, const QString &path,
                    const Options &options, QString *error)
{
    const QList<int> parts = playableParts(score);
    if (parts.isEmpty()) {
        if (error) {
            *error = i18n("there is no fretted part to make an arrangement from");
        }
        return false;
    }

    KZip archive(path);
    if (!archive.open(QIODevice::WriteOnly)) {
        if (error) {
            *error = i18n("%1 could not be written", path);
        }
        return false;
    }

    const double duration = Timeline::seconds(score, order);
    bool ok = archive.writeFile(QStringLiteral("manifest.yaml"), manifestFor(score, duration));
    for (const int track : parts) {
        const QString id = identifierFor(score.tracks.at(track).name, track);
        const QJsonDocument document(arrangementFor(score, track, order));
        ok = ok
            && archive.writeFile(QStringLiteral("arrangements/%1.json").arg(id),
                                 document.toJson(QJsonDocument::Compact));
    }

    if (ok && options.stems) {
        // Rendered by the same code that writes stems for anything else, which
        // is the point: a pack's audio is not a special kind of audio.
        QTemporaryDir folder;
        Render::Options rendering;
        rendering.soundFont = options.soundFont;
        rendering.sampleRate = options.sampleRate;
        QList<Render::Written> written;
        if (!folder.isValid() || !Render::stems(score, order, folder.path(), rendering, error,
                                                &written)) {
            archive.close();
            return false;
        }
        for (const Render::Written &file : written) {
            QFile audio(file.path);
            if (!audio.open(QIODevice::ReadOnly)) {
                continue;
            }
            // The renderer names a stem after the track *and its number* --
            // `00-Guitar I.wav` -- so the number is what says which part it
            // is. Read back rather than re-derived from the name, because the
            // manifest has to point at these and a second way of spelling a
            // track's name is a second chance to disagree with it.
            const QString name = QFileInfo(file.path).completeBaseName();
            const QString inside =
                name.compare(QStringLiteral("mix"), Qt::CaseInsensitive) == 0
                ? QStringLiteral("stems/full.wav")
                : QStringLiteral("stems/%1.wav")
                      .arg(stemIdFor(score, name.left(name.indexOf(QLatin1Char('-'))).toInt()));
            ok = ok && archive.writeFile(inside, audio.readAll());
        }
    }

    archive.close();
    if (!ok && error && error->isEmpty()) {
        *error = i18n("the pack could not be written to %1", path);
    }
    return ok;
}
