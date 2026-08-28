// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "rigfile.h"

#include <KLocalizedString>

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

#include <algorithm>

namespace
{
QString versionKey()
{
    return QStringLiteral("rig");
}
}

bool Rig::Document::isEmpty() const
{
    // A track carrying neither a sampler nor a chain is a track played the way
    // every track is played by default, and says nothing worth keeping.
    for (const Track &track : tracks) {
        if (!track.sampler.isEmpty() || !track.chain.isEmpty()) {
            return false;
        }
    }
    return true;
}

QString Rig::extension()
{
    return QStringLiteral("rig");
}

QString Rig::pathFor(const QString &scorePath)
{
    if (scorePath.isEmpty()) {
        return {};
    }
    return scorePath + QLatin1Char('.') + extension();
}

bool Rig::write(const Document &rig, const QString &path, QString *error)
{
    if (path.isEmpty()) {
        if (error) {
            *error = i18n("there is nowhere to write the rig");
        }
        return false;
    }

    if (rig.isEmpty()) {
        // Nothing to say, so say nothing -- and take away anything said
        // earlier, or a rig somebody has just cleared would come back.
        QFile stale(path);
        if (stale.exists() && !stale.remove()) {
            if (error) {
                *error = i18nc("a file, and what is wrong with it", "%1: %2", path,
                               stale.errorString());
            }
            return false;
        }
        return true;
    }

    QJsonArray tracks;
    for (const Track &track : rig.tracks) {
        if (track.sampler.isEmpty() && track.chain.isEmpty()) {
            continue;
        }
        QJsonObject object;
        object.insert(QStringLiteral("track"), track.track);
        if (!track.sampler.isEmpty()) {
            object.insert(QStringLiteral("sampler"), track.sampler);
        }
        if (!track.chain.isEmpty()) {
            object.insert(QStringLiteral("chain"), QJsonArray::fromStringList(track.chain));
        }
        if (!track.knobs.isEmpty()) {
            QJsonArray knobs;
            for (const Knob &knob : track.knobs) {
                QJsonObject one;
                one.insert(QStringLiteral("stage"), knob.stage);
                one.insert(QStringLiteral("symbol"), knob.symbol);
                one.insert(QStringLiteral("value"), double(knob.value));
                knobs.append(one);
            }
            object.insert(QStringLiteral("knobs"), knobs);
        }
        if (!track.voicings.isEmpty()) {
            QJsonArray voicings;
            // Sorted, so that two rigs describing the same thing are the same
            // file: a QHash iterates in whatever order it likes, and a diff
            // that changes every time it is written is a diff nobody reads.
            QList<int> stages = track.voicings.keys();
            std::sort(stages.begin(), stages.end());
            for (const int stage : stages) {
                QJsonObject one;
                one.insert(QStringLiteral("stage"), stage);
                one.insert(QStringLiteral("name"), track.voicings.value(stage));
                voicings.append(one);
            }
            object.insert(QStringLiteral("voicings"), voicings);
        }
        tracks.append(object);
    }

    QJsonObject root;
    root.insert(versionKey(), FormatVersion);
    root.insert(QStringLiteral("tracks"), tracks);

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error) {
            *error = i18nc("a file, and what is wrong with it", "%1: %2", path,
                           file.errorString());
        }
        return false;
    }
    // Indented, because the point of a readable format is that somebody reads
    // it.
    const QByteArray text = QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (file.write(text) != text.size()) {
        if (error) {
            *error = i18nc("a file, and what is wrong with it", "%1: %2", path,
                           file.errorString());
        }
        return false;
    }
    // Closed here rather than left to the destructor, so a full disk is an
    // error somebody is told about rather than one discovered on the next open.
    file.close();
    return file.error() == QFile::NoError;
}

Rig::Document Rig::read(const QString &path, QString *error)
{
    Document rig;
    QFile file(path);
    if (!file.exists()) {
        // Not an error: a score nobody has built a rig for yet.
        return rig;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) {
            *error = i18nc("a file, and what is wrong with it", "%1: %2", path,
                           file.errorString());
        }
        return rig;
    }

    QJsonParseError trouble;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &trouble);
    if (document.isNull() || !document.isObject()) {
        if (error) {
            *error = i18nc("a file, and what is wrong with it", "%1: %2", path,
                           trouble.errorString());
        }
        return rig;
    }

    const QJsonObject root = document.object();
    const int version = root.value(versionKey()).toInt();
    if (version > FormatVersion) {
        // Refused rather than half-understood, for the reason set out in
        // Fw::FormatVersion: a rig that loaded with half its knobs would be a
        // sound nobody chose.
        if (error) {
            *error = i18n("%1 was written by a later version of Fretwork", path);
        }
        return rig;
    }

    const QJsonArray tracks = root.value(QStringLiteral("tracks")).toArray();
    for (const QJsonValue &value : tracks) {
        const QJsonObject object = value.toObject();
        Track track;
        track.track = object.value(QStringLiteral("track")).toInt(-1);
        if (track.track < 0) {
            continue;
        }
        track.sampler = object.value(QStringLiteral("sampler")).toString();
        const QJsonArray chain = object.value(QStringLiteral("chain")).toArray();
        for (const QJsonValue &uri : chain) {
            const QString text = uri.toString();
            if (!text.isEmpty()) {
                track.chain.append(text);
            }
        }
        const QJsonArray knobs = object.value(QStringLiteral("knobs")).toArray();
        for (const QJsonValue &value : knobs) {
            const QJsonObject one = value.toObject();
            Knob knob;
            knob.stage = one.value(QStringLiteral("stage")).toInt();
            knob.symbol = one.value(QStringLiteral("symbol")).toString();
            knob.value = float(one.value(QStringLiteral("value")).toDouble());
            // A knob with no symbol names no control, and a negative stage is
            // no plugin: both are a file saying nothing rather than a file
            // saying something wrong, so both are read past.
            if (knob.symbol.isEmpty() || knob.stage < 0) {
                continue;
            }
            track.knobs.append(knob);
        }
        const QJsonArray voicings = object.value(QStringLiteral("voicings")).toArray();
        for (const QJsonValue &value : voicings) {
            const QJsonObject one = value.toObject();
            const int stage = one.value(QStringLiteral("stage")).toInt(-1);
            const QString name = one.value(QStringLiteral("name")).toString();
            if (stage < 0 || name.isEmpty()) {
                continue;
            }
            track.voicings.insert(stage, name);
        }
        rig.tracks.append(track);
    }
    return rig;
}
