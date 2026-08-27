// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "renderer.h"

#include "sampler.h"
#include "sfz.h"
#include "timeline.h"
#include "tracksynth.h"
#include "wav.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

namespace
{
QString safeName(const QString &name)
{
    QString out = name;
    out.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9._-]+")), QStringLiteral("_"));
    return out.isEmpty() ? QStringLiteral("track") : out;
}
}

QString Render::findSoundFont()
{
    // The usual places a distribution puts a General MIDI bank. Ordered by how
    // good they sound rather than alphabetically.
    static const QStringList candidates = {
        QStringLiteral("/usr/share/sounds/sf2/FluidR3_GM.sf2"),
        QStringLiteral("/usr/share/sounds/sf2/default-GM.sf2"),
        QStringLiteral("/usr/share/soundfonts/FluidR3_GM.sf2"),
        QStringLiteral("/usr/share/soundfonts/default.sf2"),
    };
    for (const QString &candidate : candidates) {
        if (QFileInfo::exists(candidate)) {
            return candidate;
        }
    }
    return {};
}

bool Render::stems(const Score &score, const QList<int> &order, const QString &directory,
                   const Options &given, QString *error, QList<Written> *written)
{
    Options options = given;
    if (options.soundFont.isEmpty()) {
        options.soundFont = findSoundFont();
    }
    if (options.soundFont.isEmpty()) {
        if (error) {
            *error = QStringLiteral("no SoundFont found: install fluid-soundfont-gm, "
                                    "or name one with --soundfont");
        }
        return false;
    }
    if (score.isEmpty()) {
        if (error) {
            *error = QStringLiteral("there is nothing to render");
        }
        return false;
    }

    QDir folder(directory);
    if (!folder.mkpath(QStringLiteral("."))) {
        if (error) {
            *error = QStringLiteral("cannot make %1").arg(directory);
        }
        return false;
    }

    const Timeline::Clock clock(score, order);

    std::vector<std::unique_ptr<Synth>> voices;
    std::vector<std::unique_ptr<WavWriter>> writers;
    QStringList paths;

    qint64 lastEvent = 0;
    for (int index = 0; index < score.tracks.size(); ++index) {
        const Track &track = score.tracks.at(index);
        TrackSynth::Options synthOptions;
        synthOptions.soundFont = options.soundFont;
        synthOptions.sampleRate = options.sampleRate;
        synthOptions.gain = options.gain;
        const QList<Timeline::Message> messages = Timeline::messagesFor(score, index, order);

        std::unique_ptr<Synth> voice;
        const QString sfz = options.samplers.value(index);
        if (!sfz.isEmpty()) {
            QString why;
            const Sfz::Instrument instrument = Sfz::read(sfz, &why);
            Sampler::Options samplerOptions;
            samplerOptions.sampleRate = options.sampleRate;
            samplerOptions.gain = options.gain;
            auto sampler =
                std::make_unique<Sampler>(instrument, messages, clock, samplerOptions);
            if (!sampler->isValid()) {
                if (error) {
                    *error = QStringLiteral("%1: %2").arg(
                        sfz, why.isEmpty() ? sampler->error() : why);
                }
                return false;
            }
            voice = std::move(sampler);
        } else {
            voice = std::make_unique<TrackSynth>(track, messages, clock, synthOptions);
        }
        if (!voice->isValid()) {
            if (error) {
                *error = QStringLiteral("could not load %1").arg(options.soundFont);
            }
            return false;
        }
        lastEvent = std::max(lastEvent, voice->lastEventSample());

        const QString path = folder.filePath(QStringLiteral("%1-%2.wav")
                                                 .arg(index, 2, 10, QLatin1Char('0'))
                                                 .arg(safeName(track.name)));
        auto writer = std::make_unique<WavWriter>(path, options.sampleRate);
        if (!writer->isOpen()) {
            if (error) {
                *error = QStringLiteral("%1: %2").arg(path, writer->error());
            }
            return false;
        }
        paths.append(path);
        voices.push_back(std::move(voice));
        writers.push_back(std::move(writer));
    }

    // The metronome, if it was asked for: a part like any other to everything
    // below, and the one whose index the mix has to skip.
    int clickVoice = -1;
    if (options.click) {
        TrackSynth::Options synthOptions;
        synthOptions.soundFont = options.soundFont;
        synthOptions.sampleRate = options.sampleRate;
        synthOptions.gain = options.gain;
        std::unique_ptr<Synth> voice =
            std::make_unique<TrackSynth>(Timeline::clickTrack(),
                                         Timeline::clickFor(score, order), clock,
                                         synthOptions);
        if (!voice->isValid()) {
            if (error) {
                *error = QStringLiteral("could not load %1").arg(options.soundFont);
            }
            return false;
        }
        // Deliberately not counted into `lastEvent`: a beat clicking over the
        // last bar of a decay is not a reason for every file to be longer.
        const QString path = folder.filePath(QStringLiteral("click.wav"));
        auto writer = std::make_unique<WavWriter>(path, options.sampleRate);
        if (!writer->isOpen()) {
            if (error) {
                *error = QStringLiteral("%1: %2").arg(path, writer->error());
            }
            return false;
        }
        clickVoice = int(voices.size());
        paths.append(path);
        voices.push_back(std::move(voice));
        writers.push_back(std::move(writer));
    }

    const QString mixPath = folder.filePath(QStringLiteral("mix.wav"));
    WavWriter mix(mixPath, options.sampleRate);
    if (!mix.isOpen()) {
        if (error) {
            *error = QStringLiteral("%1: %2").arg(mixPath, mix.error());
        }
        return false;
    }

    // The tail matters: a score does not stop when its last note-off is sent.
    const qint64 total =
        lastEvent + qint64(options.tailSeconds * options.sampleRate);
    const int block = std::max(32, options.blockFrames);

    // Two arguments rather than one: `std::vector<float> left(size_t(block))`
    // declares a function, which is the most vexing parse and compiles until
    // something is asked of it.
    std::vector<float> left(size_t(block), 0.0f);
    std::vector<float> right(size_t(block), 0.0f);
    std::vector<float> mixLeft(size_t(block), 0.0f);
    std::vector<float> mixRight(size_t(block), 0.0f);

    for (qint64 at = 0; at < total; at += block) {
        const int frames = int(std::min<qint64>(block, total - at));
        std::fill_n(mixLeft.begin(), frames, 0.0f);
        std::fill_n(mixRight.begin(), frames, 0.0f);

        for (size_t index = 0; index < voices.size(); ++index) {
            voices[index]->fill(left.data(), right.data(), frames, at);
            if (!writers[index]->write(left.data(), right.data(), frames)) {
                if (error) {
                    *error = writers[index]->error();
                }
                return false;
            }
            if (int(index) == clickVoice) {
                continue;
            }
            for (int frame = 0; frame < frames; ++frame) {
                mixLeft[size_t(frame)] += left[size_t(frame)];
                mixRight[size_t(frame)] += right[size_t(frame)];
            }
        }

        if (!mix.write(mixLeft.data(), mixRight.data(), frames)) {
            if (error) {
                *error = mix.error();
            }
            return false;
        }
    }

    const double seconds = double(total) / options.sampleRate;
    for (size_t index = 0; index < writers.size(); ++index) {
        const float peak = writers[index]->peak();
        if (!writers[index]->close()) {
            if (error) {
                *error = writers[index]->error();
            }
            return false;
        }
        if (written) {
            written->append({paths.at(int(index)), peak, seconds});
        }
    }
    const float mixPeak = mix.peak();
    if (!mix.close()) {
        if (error) {
            *error = mix.error();
        }
        return false;
    }
    if (written) {
        written->append({mixPath, mixPeak, seconds});
    }
    return true;
}
