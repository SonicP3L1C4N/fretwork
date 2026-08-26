// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "tuning.h"

#include <KLocalizedString>

#include <algorithm>
#include <cmath>

namespace
{
/**
 * How long a reading stays on screen after the string stops sounding.
 *
 * A plucked string is loud for a moment and quiet for a while, and the person
 * reading this is looking at a machine head rather than at the window. Long
 * enough to look up and still see what it said; short enough that it is not
 * still saying it when the next string is picked.
 */
constexpr qint64 HoldMilliseconds = 1600;

/** Fifteen times a second: faster than the eye asks for, slower than the window slides. */
constexpr int TickMilliseconds = 66;
}

Tuning::Tuning(QObject *parent)
    : QObject(parent)
{
    m_ticker.setInterval(TickMilliseconds);
    connect(&m_ticker, &QTimer::timeout, this, &Tuning::tick);
    m_targets = Tuner::standardGuitar();
}

Tuning::~Tuning() = default;

bool Tuning::isListening() const
{
    return m_listening;
}

void Tuning::setListening(bool listening)
{
    if (listening == m_listening) {
        return;
    }
    m_listening = listening;

    if (!listening) {
        // Closing the panel gives the device back. Nothing about reading a tab
        // justifies holding a microphone open behind it.
        m_ticker.stop();
        m_input.reset();
        m_detector.reset();
        m_rate = 0;
        m_wasRunning = false;
        m_error.clear();
        clearReading();
        Q_EMIT listeningChanged();
        Q_EMIT stateChanged();
        return;
    }

    AudioInput::Options options;
    m_input = std::make_unique<AudioInput>(options);
    m_error = m_input->isValid() ? QString() : m_input->error();
    if (!m_input->isValid()) {
        m_input.reset();
    } else {
        m_since.start();
        m_ticker.start();
    }

    clearReading();
    Q_EMIT listeningChanged();
    Q_EMIT stateChanged();
}

bool Tuning::isRunning() const
{
    return m_input && m_input->isRunning() && m_input->framesCaptured() > 0;
}

QString Tuning::error() const
{
    return m_error;
}

QVariantList Tuning::strings() const
{
    return m_strings;
}

void Tuning::setStrings(const QVariantList &strings)
{
    if (strings == m_strings) {
        return;
    }
    m_strings = strings;

    if (strings.isEmpty()) {
        // No score open, or a track with no strings in it. A guitar is in
        // standard tuning until something says otherwise.
        m_targets = Tuner::standardGuitar();
    } else {
        Track track;
        track.instrumentType = QStringLiteral("electricGuitar");
        for (const QVariant &pitch : strings) {
            track.tuning.append(pitch.toInt());
        }
        m_targets = Tuner::targetsFor(track);
    }

    // Whatever was being heard was measured against the old strings.
    clearReading();
    Q_EMIT stringsChanged();
}

QStringList Tuning::stringNames() const
{
    QStringList names;
    names.reserve(m_targets.size());
    for (const Tuner::StringTarget &target : m_targets) {
        names.append(target.name);
    }
    return names;
}

bool Tuning::heard() const
{
    return m_reading.heard;
}

bool Tuning::isFresh() const
{
    return m_fresh;
}

int Tuning::string() const
{
    return m_reading.string;
}

double Tuning::cents() const
{
    return m_reading.string >= 0 ? m_reading.cents : m_reading.nearestCents;
}

double Tuning::hertz() const
{
    return m_reading.hertz;
}

bool Tuning::isInTune() const
{
    return m_reading.heard && m_reading.string >= 0
        && std::abs(m_reading.cents) <= Tuner::InTuneCents;
}

QString Tuning::noteName() const
{
    if (!m_reading.heard) {
        return QString();
    }
    // The string's own name where there is one, so that what is printed on the
    // needle is the note being aimed at rather than the nearest note to
    // whatever is currently coming out of the instrument.
    return m_reading.string >= 0 ? m_targets.at(m_reading.string).name : m_reading.noteName;
}

QString Tuning::message() const
{
    if (!m_error.isEmpty()) {
        return m_error;
    }
    if (!m_listening) {
        return QString();
    }
    if (!isRunning()) {
        return i18n("connecting to an input…");
    }
    if (m_reading.heard) {
        return Tuner::describe(m_reading);
    }
    // Silence and noise are different answers, and giving the same one for
    // both would have somebody checking a cable that is fine.
    return m_noteInIt ? i18n("hearing something, but no note in it")
                      : i18n("play a string");
}

double Tuning::level() const
{
    return m_level;
}

void Tuning::clearReading()
{
    m_reading = Tuner::Reading();
    m_fresh = false;
    m_level = 0;
    m_noteInIt = false;
    Q_EMIT readingChanged();
}

void Tuning::tick()
{
    if (!m_input) {
        return;
    }

    const bool running = isRunning();
    if (running != m_wasRunning) {
        m_wasRunning = running;
        Q_EMIT stateChanged();
        Q_EMIT readingChanged();      // the message changes with it
    }
    if (!running) {
        return;
    }

    // The rate is not known until the graph has settled on one, and the
    // detector's window is measured in samples, so it cannot be built before
    // then -- nor kept if the device changes underneath it.
    if (!m_detector || !qFuzzyCompare(m_rate, m_input->sampleRate())) {
        m_rate = m_input->sampleRate();
        Pitch::Settings settings;
        settings.sampleRate = m_rate;
        m_detector = std::make_unique<Pitch::Detector>(settings);
        m_window.assign(size_t(Pitch::windowFor(settings)), 0.0f);
    }

    const int frames = int(m_window.size());
    if (m_input->latest(m_window.data(), frames) != frames) {
        return;
    }

    const Pitch::Detection detection = m_detector->detect(m_window.data(), frames);

    // A level meter reads better as a fraction of the loudest thing a guitar
    // does than as an RMS: full scale on a plucked string is around a quarter.
    m_level = std::clamp(detection.level * 4.0, 0.0, 1.0);
    m_noteInIt = detection.level >= m_detector->settings().minimumLevel;

    if (detection.voiced) {
        m_reading = Tuner::read(detection.hertz, detection.clarity, m_targets);
        m_fresh = true;
        m_since.restart();
    } else if (m_fresh || m_reading.heard) {
        // Held rather than cleared, until it has been quiet long enough that
        // holding it would be a lie about what is being played now.
        m_fresh = false;
        if (m_since.elapsed() > HoldMilliseconds) {
            m_reading = Tuner::Reading();
        }
    }

    Q_EMIT readingChanged();
}
