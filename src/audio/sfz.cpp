// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "sfz.h"

#include "notename.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>

namespace
{
/**
 * A key, which SFZ writes either as a number or as a note.
 *
 * `lokey=60` and `lokey=c4` are the same note, and libraries use both --
 * sometimes in the same file. The note spelling is the one this program
 * already reads for tunings, so there is one answer to what "c#3" means
 * rather than two that can drift apart.
 */
int keyOf(const QString &value, int fallback)
{
    const int parsed = NoteName::parse(value);
    return parsed >= 0 ? parsed : fallback;
}

/**
 * Splits a line into opcodes, keeping `sample=` whole.
 *
 * The one genuinely awkward thing about this format. Opcodes are separated by
 * spaces and a value may contain them -- `sample=Clean Guitar/E2 v3.wav` is
 * one opcode and four words -- so a value runs to the next thing that looks
 * like `name=`, not to the next space. Every SFZ parser has this and the ones
 * that do not are the ones that cannot open half the libraries in the world.
 */
QList<QPair<QString, QString>> opcodesIn(const QString &line)
{
    static const QRegularExpression next(QStringLiteral("([A-Za-z0-9_]+)\\s*="));
    QList<QPair<QString, QString>> found;

    QRegularExpressionMatchIterator matches = next.globalMatch(line);
    QString name;
    int valueFrom = -1;
    while (matches.hasNext()) {
        const QRegularExpressionMatch match = matches.next();
        if (!name.isEmpty()) {
            found.append({name, line.mid(valueFrom, match.capturedStart() - valueFrom).trimmed()});
        }
        name = match.captured(1);
        valueFrom = match.capturedEnd();
    }
    if (!name.isEmpty() && valueFrom >= 0) {
        found.append({name, line.mid(valueFrom).trimmed()});
    }
    return found;
}

/** The opcodes in force, innermost last: global, then group, then region. */
using Level = QHash<QString, QString>;

Sfz::Region regionFrom(const Level &settings, const QString &directory,
                       const QString &defaultPath)
{
    Sfz::Region region;
    const auto value = [&settings](const char *name, const QString &fallback = QString()) {
        return settings.value(QString::fromLatin1(name), fallback);
    };
    const auto number = [&](const char *name, double fallback) {
        bool ok = false;
        const double got = value(name).toDouble(&ok);
        return ok ? got : fallback;
    };

    if (!value("key").isEmpty()) {
        // One opcode saying all three, which is how a one-shot percussion
        // region is usually written.
        const int key = keyOf(value("key"), 60);
        region.lowKey = region.highKey = region.keyCentre = key;
    }
    region.lowKey = keyOf(value("lokey"), region.lowKey);
    region.highKey = keyOf(value("hikey"), region.highKey);
    region.keyCentre = keyOf(value("pitch_keycenter"), region.keyCentre);
    region.lowVelocity = int(number("lovel", region.lowVelocity));
    region.highVelocity = int(number("hivel", region.highVelocity));
    region.tuneCents = int(number("tune", number("pitch", 0)));
    region.volumeDb = number("volume", 0);
    region.pan = number("pan", 0);
    region.sequenceLength = std::max(1, int(number("seq_length", 1)));
    region.sequencePosition = std::max(1, int(number("seq_position", 1)));
    region.offset = qint64(number("offset", 0));
    region.end = qint64(number("end", -1));
    region.loops = value("loop_mode") == QLatin1String("loop_continuous")
        || value("loop_mode") == QLatin1String("loop_sustain");
    region.loopStart = qint64(number("loop_start", 0));
    region.loopEnd = qint64(number("loop_end", -1));
    region.release = number("ampeg_release", 0.05);
    region.releaseDecayDb = number("rt_decay", 0);
    region.group = int(number("group", 0));
    region.lowRandom = number("lorand", 0);
    region.highRandom = number("hirand", 1);
    region.delay = number("delay", 0);
    region.switchLow = keyOf(value("sw_lokey"), -1);
    region.switchHigh = keyOf(value("sw_hikey"), -1);
    region.switchLast = keyOf(value("sw_last"), -1);
    region.switchDefault = keyOf(value("sw_default"), -1);

    const QString trigger = value("trigger").toLower();
    if (trigger == QLatin1String("release")) {
        region.trigger = Sfz::Region::Trigger::Release;
    } else if (trigger == QLatin1String("first")) {
        region.trigger = Sfz::Region::Trigger::First;
    } else if (trigger == QLatin1String("legato")) {
        region.trigger = Sfz::Region::Trigger::Legato;
    }
    region.offBy = int(number("off_by", number("offby", 0)));

    QString sample = value("sample");
    // Libraries are written on Windows as often as not, and a backslash in a
    // path is a separator there and a perfectly legal filename character here.
    sample.replace(QLatin1Char('\\'), QLatin1Char('/'));
    if (!sample.isEmpty()) {
        const QString base = defaultPath.isEmpty()
            ? directory
            : QDir(directory).filePath(defaultPath);
        region.sample = QFileInfo(sample).isAbsolute() ? sample : QDir(base).filePath(sample);
        // Inside the library, or not at all. A library is a folder somebody
        // downloaded, and `default_path=../../` or an absolute `sample=` is
        // that folder reaching for files that are not its own. A region
        // whose sample is refused is a region with no sample, which the
        // sampler already knows how to ignore.
        const QString root = QDir::cleanPath(QDir(directory).absolutePath()) + QLatin1Char('/');
        const QString resolved = QDir::cleanPath(QFileInfo(region.sample).absoluteFilePath());
        region.sample = resolved.startsWith(root) ? resolved : QString();
    }
    return region;
}
}

Sfz::Instrument Sfz::parse(const QString &text, const QString &directory, QString *error)
{
    Instrument instrument;

    Level global;
    Level master;
    Level group;
    Level region;
    Level loose;                //< anything in a header this does not model
    QString defaultPath;
    bool inRegion = false;

    // Which level the opcodes being read belong to. Tracked rather than
    // guessed from whether a region is open: `<global> volume=-3` puts the
    // volume in the global level, and a parser that dropped it into the group
    // would lose it at the next `<group>`.
    Level *current = &group;

    const auto flush = [&] {
        if (!inRegion) {
            return;
        }
        Level settings = global;
        for (const Level *level : {&master, &group, &region}) {
            for (auto entry = level->constBegin(); entry != level->constEnd(); ++entry) {
                settings.insert(entry.key(), entry.value());
            }
        }
        instrument.regions.append(regionFrom(settings, directory, defaultPath));
        region.clear();
        inRegion = false;
    };

    const QStringList lines = text.split(QLatin1Char('\n'));
    for (const QString &raw : lines) {
        QString line = raw;
        const int comment = line.indexOf(QLatin1String("//"));
        if (comment >= 0) {
            line.truncate(comment);
        }
        line = line.trimmed();
        if (line.isEmpty()) {
            continue;
        }

        // A line may hold a header and opcodes, or several headers. Walking
        // the headers in order is what makes `<group> volume=-3 <region>
        // sample=a.wav` mean what it looks like it means.
        int at = 0;
        while (at < line.size()) {
            const int open = line.indexOf(QLatin1Char('<'), at);
            const QString run = open < 0 ? line.mid(at) : line.mid(at, open - at);
            if (!run.trimmed().isEmpty()) {
                for (const auto &opcode : opcodesIn(run)) {
                    if (opcode.first == QLatin1String("default_path")) {
                        defaultPath = QString(opcode.second)
                                          .replace(QLatin1Char('\\'), QLatin1Char('/'));
                    }
                    current->insert(opcode.first, opcode.second);
                }
            }
            if (open < 0) {
                break;
            }
            const int close = line.indexOf(QLatin1Char('>'), open);
            if (close < 0) {
                break;
            }
            const QString header = line.mid(open + 1, close - open - 1).toLower();
            flush();
            if (header == QLatin1String("global")) {
                global.clear();
                master.clear();
                group.clear();
                current = &global;
            } else if (header == QLatin1String("master")) {
                master.clear();
                group.clear();
                current = &master;
            } else if (header == QLatin1String("group")) {
                group.clear();
                current = &group;
            } else if (header == QLatin1String("region")) {
                inRegion = true;
                current = &region;
            } else {
                // <control> and anything else this does not model. Its opcodes
                // go somewhere they cannot affect a region, except
                // default_path, which is read on the way past.
                loose.clear();
                current = &loose;
            }
            at = close + 1;
        }
    }
    flush();

    if (instrument.isEmpty() && error) {
        *error = QStringLiteral("no regions in it: an instrument needs at least one "
                                "<region> with a sample");
    }
    return instrument;
}

namespace
{
/** Which noise a sample path names, or None where it names none. */
enum class Named {
    None,
    Fingering,
    Muted,
    PickRest,
    Scrape,
};

/**
 * What a library calls the recording, reduced to which noise it is.
 *
 * The folder and the file name together, because libraries put the answer in
 * one or the other and not reliably in both: Emily files everything under
 * `noises/` and distinguishes them by file name, Growlybass names the folder
 * `scrape/` and numbers the files.
 *
 * Read in order of how specific the word is. "pickrest" would otherwise be
 * caught by nothing and "muted3" by anything looking for "mute" first is
 * fine, but a file called `slide_mute` should be the mute -- so the words
 * that name a thing come before the words that name a manner.
 */
Named namedIn(const QString &sample)
{
    const QString tail = QFileInfo(sample).dir().dirName().toLower()
        + QLatin1Char('/') + QFileInfo(sample).fileName().toLower();

    const auto has = [&tail](const char *word) {
        return tail.contains(QLatin1String(word));
    };
    if (has("scrape") || has("scratch")) {
        return Named::Scrape;
    }
    if (has("pickrest") || has("pick_rest") || has("pickstop") || has("pick_stop")) {
        return Named::PickRest;
    }
    if (has("mute") || has("dead")) {
        return Named::Muted;
    }
    if (has("finger") || has("squeak") || has("slide")) {
        return Named::Fingering;
    }
    return Named::None;
}

void addSorted(QList<int> *keys, int from, int to)
{
    for (int key = from; key <= to; ++key) {
        if (!keys->contains(key)) {
            keys->append(key);
        }
    }
    std::sort(keys->begin(), keys->end());
}
}

Noises::Map Sfz::noises(const Instrument &instrument)
{
    // The top of what the instrument can play, which is every attack region
    // whose sample is not filed under a noise. An instrument that is nothing
    // but noises leaves this at -1 and every one of them qualifies, which is
    // the right answer for a mapping that holds only scrapes.
    int highestNote = -1;
    for (const Region &region : instrument.regions) {
        if (region.trigger != Region::Trigger::Attack || region.sample.isEmpty()) {
            continue;
        }
        if (namedIn(region.sample) == Named::None) {
            highestNote = std::max(highestNote, region.highKey);
        }
    }

    Noises::Map map;
    for (const Region &region : instrument.regions) {
        if (region.trigger != Region::Trigger::Attack || region.sample.isEmpty()) {
            continue;
        }
        // Above the range, all of it. A region straddling the top note is a
        // region that answers to notes as well, and playing it as a noise
        // would put a squeak where somebody wrote a note.
        if (region.lowKey <= highestNote) {
            continue;
        }
        switch (namedIn(region.sample)) {
        case Named::Fingering:
            addSorted(&map.fingering, region.lowKey, region.highKey);
            break;
        case Named::Muted:
            addSorted(&map.muted, region.lowKey, region.highKey);
            break;
        case Named::PickRest:
            addSorted(&map.pickRest, region.lowKey, region.highKey);
            break;
        case Named::Scrape:
            addSorted(&map.scrape, region.lowKey, region.highKey);
            break;
        case Named::None:
            break;
        }
    }
    return map;
}

Sfz::Instrument Sfz::read(const QString &path, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error) {
            *error = QStringLiteral("%1: %2").arg(path, file.errorString());
        }
        return {};
    }
    return parse(QString::fromUtf8(file.readAll()), QFileInfo(path).absolutePath(), error);
}

QList<Sfz::Library> Sfz::found(const QStringList &roots, int maximumDepth)
{
    QList<Library> libraries;
    QSet<QString> seen;

    // Breadth first with a depth cap, so a directory that turns out to hold a
    // whole disk of samples still answers rather than descending for ever.
    for (const QString &root : roots) {
        const QDir base(root);
        QList<QPair<QString, int>> pending;
        pending.append({root, 0});
        while (!pending.isEmpty()) {
            const auto [where, depth] = pending.takeFirst();
            QDir directory(where);
            if (!directory.exists()) {
                continue;
            }
            const QFileInfoList entries = directory.entryInfoList(
                QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
            for (const QFileInfo &entry : entries) {
                if (entry.isDir()) {
                    if (depth < maximumDepth) {
                        pending.append({entry.absoluteFilePath(), depth + 1});
                    }
                    continue;
                }
                if (entry.suffix().compare(QLatin1String("sfz"), Qt::CaseInsensitive) != 0) {
                    continue;
                }
                const QString path = entry.absoluteFilePath();
                if (seen.contains(path)) {
                    continue;
                }
                seen.insert(path);

                // The collection is the first folder under the root, which is
                // one downloaded library: a menu of five hundred programmes is
                // a menu nobody can use, and they come in about a dozen boxes.
                const QString relative = base.relativeFilePath(path);
                const int slash = relative.indexOf(QLatin1Char('/'));
                const QString collection =
                    slash > 0 ? relative.left(slash) : base.dirName();

                // What is left of the path is the programme, minus the
                // extension: "Programs/03-kit-complete" says more than
                // "03-kit-complete" and much more than "kit".
                QString name = slash > 0 ? relative.mid(slash + 1) : relative;
                if (name.endsWith(QLatin1String(".sfz"), Qt::CaseInsensitive)) {
                    name.chop(4);
                }
                libraries.append({collection, name, path});
            }
        }
    }

    std::sort(libraries.begin(), libraries.end(),
              [](const Library &a, const Library &b) {
                  const int box = a.collection.localeAwareCompare(b.collection);
                  return box != 0 ? box < 0 : a.name.localeAwareCompare(b.name) < 0;
              });
    return libraries;
}
