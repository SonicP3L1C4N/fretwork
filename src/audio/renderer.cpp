// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "renderer.h"

#include "lv2chain.h"
#include "sampler.h"
#include "sfz.h"
#include "timeline.h"
#include "tracksynth.h"
#include "wav.h"

#include <KLocalizedString>

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
            *error = i18n("No SoundFont is installed, so there is nothing to render "
                          "the score with. Install fluid-soundfont-gm, or name one "
                          "with --soundfont.");
        }
        return false;
    }
    if (score.isEmpty()) {
        if (error) {
            *error = i18n("there is nothing to render");
        }
        return false;
    }

    QDir folder(directory);
    if (!folder.mkpath(QStringLiteral("."))) {
        if (error) {
            *error = i18n("cannot make %1", directory);
        }
        return false;
    }

    const Timeline::Clock clock(score, order);

    std::vector<std::unique_ptr<Synth>> voices;
    std::vector<std::unique_ptr<Lv2::Chain>> chains;
    std::vector<std::unique_ptr<WavWriter>> writers;
    /** One per track where a dry copy was asked for, null everywhere else. */
    std::vector<std::unique_ptr<WavWriter>> dryWriters;
    // Kept apart from `paths`, which is walked in step with `writers` to say
    // what was written: a dry file slipped into that list would rename every
    // stem after it.
    QStringList dryPaths;
    QStringList paths;

    qint64 lastEvent = 0;
    for (int index = 0; index < score.tracks.size(); ++index) {
        const Track &track = score.tracks.at(index);
        TrackSynth::Options synthOptions;
        synthOptions.soundFont = options.soundFont;
        synthOptions.sampleRate = options.sampleRate;
        synthOptions.gain = options.gain;
        // The library first, because what it can make besides notes decides
        // what the timeline asks for: a squeak has to be a note number by the
        // time anything downstream sees it, and only the library knows which
        // one. A part with no library gets an empty map and the notes it
        // always had.
        const QString sfz = options.samplers.value(index);
        QString why;
        const Sfz::Instrument instrument =
            sfz.isEmpty() ? Sfz::Instrument{} : Sfz::read(sfz, &why);
        const QList<Timeline::Message> messages =
            Timeline::messagesFor(score, index, order, Sfz::noises(instrument));

        std::unique_ptr<Synth> voice;
        if (!sfz.isEmpty()) {
            Sampler::Options samplerOptions;
            samplerOptions.sampleRate = options.sampleRate;
            samplerOptions.gain = options.gain;
            auto sampler =
                std::make_unique<Sampler>(instrument, messages, clock, samplerOptions);
            if (!sampler->isValid()) {
                if (error) {
                    *error = i18nc("a file, and what is wrong with it", "%1: %2",
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
                *error = i18n("could not load %1", options.soundFont);
            }
            return false;
        }
        lastEvent = std::max(lastEvent, voice->lastEventSample());

        const QStringList wanted = options.effects.value(index);
        if (!wanted.isEmpty()) {
            Lv2::Chain::Options chainOptions;
            chainOptions.sampleRate = options.sampleRate;
            chainOptions.maximumFrames = std::max(64, options.blockFrames);
            auto chain = std::make_unique<Lv2::Chain>(wanted, chainOptions);
            if (!chain->isValid()) {
                if (error) {
                    *error = chain->error();
                }
                return false;
            }
            for (const Options::Knob &knob : options.knobs) {
                if (knob.track == index) {
                    chain->setControl(knob.stage, knob.symbol, knob.value);
                }
            }
            chains.push_back(std::move(chain));
        } else {
            chains.push_back(nullptr);
        }

        const QString path = folder.filePath(QStringLiteral("%1-%2.wav")
                                                 .arg(index, 2, 10, QLatin1Char('0'))
                                                 .arg(safeName(track.name)));
        auto writer = std::make_unique<WavWriter>(path, options.sampleRate);
        if (!writer->isOpen()) {
            if (error) {
                *error = i18nc("a file, and what is wrong with it", "%1: %2",
                               path, writer->error());
            }
            return false;
        }

        // The same part before its chain, where there is a chain to be before.
        // The wet stem keeps the name it always had, so anything already
        // reading a rendered folder goes on reading it.
        std::unique_ptr<WavWriter> dryWriter;
        if (options.dryStems && chains.back()) {
            const QString dryPath = folder.filePath(QStringLiteral("%1-%2-dry.wav")
                                                        .arg(index, 2, 10, QLatin1Char('0'))
                                                        .arg(safeName(track.name)));
            dryWriter = std::make_unique<WavWriter>(dryPath, options.sampleRate);
            if (!dryWriter->isOpen()) {
                if (error) {
                    *error = i18nc("a file, and what is wrong with it", "%1: %2",
                                   dryPath, dryWriter->error());
                }
                return false;
            }
            dryPaths.append(dryPath);
        }

        paths.append(path);
        voices.push_back(std::move(voice));
        writers.push_back(std::move(writer));
        dryWriters.push_back(std::move(dryWriter));
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
                *error = i18n("could not load %1", options.soundFont);
            }
            return false;
        }
        // Deliberately not counted into `lastEvent`: a beat clicking over the
        // last bar of a decay is not a reason for every file to be longer.
        const QString path = folder.filePath(QStringLiteral("click.wav"));
        auto writer = std::make_unique<WavWriter>(path, options.sampleRate);
        if (!writer->isOpen()) {
            if (error) {
                *error = i18nc("a file, and what is wrong with it", "%1: %2",
                               path, writer->error());
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
            *error = i18nc("a file, and what is wrong with it", "%1: %2",
                           mixPath, mix.error());
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
            // Written here, between the instrument and its amplifier, because
            // that is the only place the dry signal exists.
            if (index < dryWriters.size() && dryWriters[index]
                && !dryWriters[index]->write(left.data(), right.data(), frames)) {
                if (error) {
                    *error = dryWriters[index]->error();
                }
                return false;
            }
            if (index < chains.size() && chains[index]) {
                // Between the instrument and the mix, the same as live: a stem
                // that came out dry while the transport was playing it wet
                // would be a stem of a different performance.
                chains[index]->process(left.data(), right.data(), frames);
            }
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
    // Closed like any other file, because a WAV that was never closed is a
    // WAV whose header still says it holds nothing.
    int dryIndex = 0;
    for (size_t index = 0; index < dryWriters.size(); ++index) {
        if (!dryWriters[index]) {
            continue;
        }
        const float peak = dryWriters[index]->peak();
        if (!dryWriters[index]->close()) {
            if (error) {
                *error = dryWriters[index]->error();
            }
            return false;
        }
        if (written) {
            written->append({dryPaths.at(dryIndex), peak, seconds});
        }
        ++dryIndex;
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
