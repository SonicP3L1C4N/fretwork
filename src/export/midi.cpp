// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "midi.h"
#include "timeline.h"

#include <QFile>
#include <QtEndian>

#include <algorithm>

namespace
{
constexpr int TicksPerQuarter = 960;
constexpr int DrumChannel = 9;

/**
 * How far a pitch bend message reaches, in semitones.
 *
 * The General MIDI default is two, which cannot express a whole-tone bend on a
 * held note and certainly not a whammy dive, so every channel is told
 * otherwise by RPN before anything else happens. Twelve is what Guitar Pro
 * uses and what a synth is most likely to honour.
 */
constexpr int BendRangeSemitones = 12;
constexpr int BendCentre = 8192;

/** A bend is redrawn at least this often, so that a slow one is not a staircase. */
constexpr int BendStepTicks = TicksPerQuarter / 16;
constexpr int MaximumBendSteps = 128;

struct Event {
    qint64 tick = 0;
    int order = 0;          //< ties broken by insertion, so output is deterministic
    QByteArray message;
};

void appendVariable(QByteArray &out, quint32 value)
{
    QByteArray digits;
    digits.append(char(value & 0x7F));
    value >>= 7;
    while (value) {
        digits.prepend(char((value & 0x7F) | 0x80));
        value >>= 7;
    }
    out.append(digits);
}

QByteArray chunk(const char *name, const QByteArray &body)
{
    QByteArray out(name, 4);
    char length[4];
    qToBigEndian(quint32(body.size()), length);
    out.append(length, 4);
    out.append(body);
    return out;
}

QByteArray meta(quint8 type, const QByteArray &payload)
{
    QByteArray out;
    out.append(char(0xFF));
    out.append(char(type));
    appendVariable(out, quint32(payload.size()));
    out.append(payload);
    return out;
}

QByteArray track(QList<Event> events)
{
    std::stable_sort(events.begin(), events.end(), [](const Event &a, const Event &b) {
        return a.tick == b.tick ? a.order < b.order : a.tick < b.tick;
    });

    QByteArray body;
    qint64 previous = 0;
    for (const Event &event : std::as_const(events)) {
        appendVariable(body, quint32(std::max<qint64>(0, event.tick - previous)));
        body.append(event.message);
        previous = event.tick;
    }
    appendVariable(body, 0);
    body.append(meta(0x2F, {}));    // end of track
    return chunk("MTrk", body);
}

QByteArray note(bool on, int channel, int pitch, int velocity)
{
    QByteArray out;
    out.append(char((on ? 0x90 : 0x80) | (channel & 0x0F)));
    out.append(char(std::clamp(pitch, 0, 127)));
    out.append(char(std::clamp(velocity, 0, 127)));
    return out;
}

QByteArray controller(int channel, int number, int value)
{
    QByteArray out;
    out.append(char(0xB0 | (channel & 0x0F)));
    out.append(char(number & 0x7F));
    out.append(char(value & 0x7F));
    return out;
}

QByteArray pitchBend(int channel, int value)
{
    const int clamped = std::clamp(value, 0, 16383);
    QByteArray out;
    out.append(char(0xE0 | (channel & 0x0F)));
    out.append(char(clamped & 0x7F));
    out.append(char((clamped >> 7) & 0x7F));
    return out;
}

int bendValueFor(int cents)
{
    const double range = BendRangeSemitones * 100.0;
    const double scaled = std::clamp(cents / range, -1.0, 1.0);
    return BendCentre + int(scaled * (BendCentre - 1));
}

/**
 * Which MIDI channels each track may use.
 *
 * Percussion tracks all share channel 10, which is the one thing the format
 * insists on. Every other track is guaranteed one channel, and the channels
 * still spare afterwards widen tracks to one per string -- the widest
 * instruments first, since a six-string bend collides more often than a
 * four-string one.
 */
QList<QList<int>> allocate(const Score &score, int only, Midi::Compromises *compromises)
{
    QList<QList<int>> channels;
    channels.resize(int(score.tracks.size()));

    QList<int> wanted;      // indices of the tracks that need a channel each
    for (int index = 0; index < score.tracks.size(); ++index) {
        if (only >= 0 && index != only) {
            continue;
        }
        if (score.tracks.at(index).isPercussion()) {
            channels[index] = {DrumChannel};
        } else {
            wanted.append(index);
        }
    }

    QList<int> free;
    for (int channel = 0; channel < 16; ++channel) {
        if (channel != DrumChannel) {
            free.append(channel);
        }
    }

    for (const int index : std::as_const(wanted)) {
        if (free.isEmpty()) {
            // Sixteen channels and more than fifteen melodic tracks. Sharing
            // would play one instrument with another's sound, so this is the
            // one case where a track is left out rather than played wrongly.
            if (compromises) {
                compromises->append(QStringLiteral(
                    "%1 could not be given a MIDI channel and is missing from the file")
                        .arg(score.tracks.at(index).name));
            }
            continue;
        }
        channels[index] = {free.takeFirst()};
    }

    // Widest first, so the instruments most likely to need it get the room.
    QList<int> widening = wanted;
    std::stable_sort(widening.begin(), widening.end(), [&score](int a, int b) {
        return score.tracks.at(a).stringCount() > score.tracks.at(b).stringCount();
    });

    for (const int index : std::as_const(widening)) {
        const Track &instrument = score.tracks.at(index);
        const int strings = instrument.stringCount();
        if (strings <= 1 || channels.at(index).isEmpty()) {
            continue;
        }
        const int extra = strings - 1;
        if (free.size() < extra) {
            if (compromises && instrument.stringCount() > 1) {
                compromises->append(QStringLiteral(
                    "%1 shares one MIDI channel across %2 strings, so a bend moves "
                    "every note it is holding")
                        .arg(instrument.name)
                        .arg(strings));
            }
            continue;
        }
        for (int step = 0; step < extra; ++step) {
            channels[index].append(free.takeFirst());
        }
    }
    return channels;
}

/** The channel a note lands on, given what its track was allocated. */
int channelFor(const QList<int> &allocated, const Timeline::NoteEvent &event)
{
    if (allocated.isEmpty()) {
        return -1;
    }
    return allocated.at(std::clamp(event.channel, 0, int(allocated.size()) - 1));
}

/** Tempo and time signature: the score's clock, in a track of its own. */
QByteArray conductor(const Score &score, const QList<int> &order)
{
    QList<Event> events;
    int counter = 0;

    for (const Timeline::TempoEvent &tempo : Timeline::tempoMap(score, order)) {
        const quint32 micros = quint32(60'000'000.0 / std::max(tempo.quarterBpm, 1.0));
        QByteArray payload;
        payload.append(char((micros >> 16) & 0xFF));
        payload.append(char((micros >> 8) & 0xFF));
        payload.append(char(micros & 0xFF));
        events.append({tempo.at.toTicks(TicksPerQuarter), counter++, meta(0x51, payload)});
    }

    Rational position;
    int numerator = 0;
    int denominator = 0;
    for (const int barIndex : order) {
        const MasterBar &bar = score.masterBars.at(barIndex);
        if (bar.numerator != numerator || bar.denominator != denominator) {
            numerator = bar.numerator;
            denominator = bar.denominator;
            QByteArray payload;
            payload.append(char(numerator));
            // MIDI writes the denominator as the power of two it is.
            int power = 0;
            for (int value = denominator; value > 1; value >>= 1) {
                ++power;
            }
            payload.append(char(power));
            payload.append(char(24));   // clocks per metronome click
            payload.append(char(8));    // thirty-seconds per quarter
            events.append({position.toTicks(TicksPerQuarter), counter++, meta(0x58, payload)});
        }
        position += bar.length();
    }
    return track(events);
}

QByteArray instrument(const Score &score, const QList<int> &order, int index,
                      const QList<int> &allocated)
{
    const Track &instrumentTrack = score.tracks.at(index);
    QList<Event> events;
    int counter = 0;

    events.append({0, counter++, meta(0x03, instrumentTrack.name.toUtf8())});

    for (const int channel : allocated) {
        if (channel != DrumChannel) {
            QByteArray program;
            program.append(char(0xC0 | (channel & 0x0F)));
            program.append(char(instrumentTrack.program & 0x7F));
            events.append({0, counter++, program});
        }
        // Widen the bend range before anything is played on this channel: the
        // default of two semitones cannot express what a guitar does.
        events.append({0, counter++, controller(channel, 101, 0)});
        events.append({0, counter++, controller(channel, 100, 0)});
        events.append({0, counter++, controller(channel, 6, BendRangeSemitones)});
        events.append({0, counter++, controller(channel, 38, 0)});
    }

    for (const Timeline::NoteEvent &event : Timeline::notesFor(score, index, order)) {
        const int channel = channelFor(allocated, event);
        if (channel < 0) {
            continue;
        }
        const qint64 start = event.start.toTicks(TicksPerQuarter);
        const qint64 end = std::max(start + 1, event.end.toTicks(TicksPerQuarter));

        if (!event.bend.isEmpty()) {
            // Straight lines between the points, redrawn often enough that a
            // slow bend sounds like one rather than like a staircase.
            events.append({start, counter++,
                           pitchBend(channel, bendValueFor(event.bend.first().cents))});
            for (int point = 1; point < event.bend.size(); ++point) {
                const Timeline::BendPoint &from = event.bend.at(point - 1);
                const Timeline::BendPoint &to = event.bend.at(point);
                const qint64 fromTick = start + from.at.toTicks(TicksPerQuarter);
                const qint64 toTick = start + to.at.toTicks(TicksPerQuarter);
                const qint64 span = toTick - fromTick;
                if (span <= 0) {
                    events.append({toTick, counter++,
                                   pitchBend(channel, bendValueFor(to.cents))});
                    continue;
                }
                const int steps = int(std::clamp<qint64>(span / BendStepTicks, 1,
                                                         MaximumBendSteps));
                for (int step = 1; step <= steps; ++step) {
                    const qint64 tick = fromTick + span * step / steps;
                    const int cents = from.cents + (to.cents - from.cents) * step / steps;
                    events.append({tick, counter++, pitchBend(channel, bendValueFor(cents))});
                }
            }
        }

        events.append({start, counter++, note(true, channel, event.pitch, event.velocity)});
        events.append({end, counter++, note(false, channel, event.pitch, 0)});

        if (!event.bend.isEmpty()) {
            // Back to centre once the note is done, or the next note on this
            // string inherits the bend -- which is the bug this arrangement of
            // channels exists to prevent, reintroduced at the last moment.
            events.append({end, counter++, pitchBend(channel, BendCentre)});
        }
    }
    return track(events);
}
}

bool Midi::write(const Score &score, const QList<int> &order, const QString &path,
                 int trackIndex, QString *error, Compromises *compromises)
{
    if (score.isEmpty()) {
        if (error) {
            *error = QStringLiteral("there is nothing to write");
        }
        return false;
    }
    if (trackIndex >= int(score.tracks.size())) {
        if (error) {
            *error = QStringLiteral("no track %1 in this score").arg(trackIndex);
        }
        return false;
    }

    const QList<QList<int>> channels = allocate(score, trackIndex, compromises);

    QList<QByteArray> chunks;
    chunks.append(conductor(score, order));
    for (int index = 0; index < score.tracks.size(); ++index) {
        if (trackIndex >= 0 && index != trackIndex) {
            continue;
        }
        if (channels.at(index).isEmpty()) {
            continue;
        }
        chunks.append(instrument(score, order, index, channels.at(index)));
    }

    QByteArray header;
    char division[6];
    qToBigEndian(quint16(1), division);                     // format 1
    qToBigEndian(quint16(chunks.size()), division + 2);
    qToBigEndian(quint16(TicksPerQuarter), division + 4);
    header.append(division, 6);

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) {
            *error = file.errorString();
        }
        return false;
    }
    file.write(chunk("MThd", header));
    for (const QByteArray &part : std::as_const(chunks)) {
        file.write(part);
    }
    file.close();
    return true;
}
