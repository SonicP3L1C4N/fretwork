// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "wav.h"

#include <QtEndian>

#include <cstring>

#include <algorithm>
#include <cmath>

namespace
{
constexpr int Channels = 2;
constexpr int BitsPerSample = 16;

void appendU16(QByteArray &out, quint16 value)
{
    char bytes[2];
    qToLittleEndian(value, bytes);
    out.append(bytes, 2);
}

void appendU32(QByteArray &out, quint32 value)
{
    char bytes[4];
    qToLittleEndian(value, bytes);
    out.append(bytes, 4);
}
}

WavWriter::WavWriter(const QString &path, int sampleRate)
    : m_file(path)
    , m_sampleRate(sampleRate)
{
    if (!m_file.open(QIODevice::WriteOnly)) {
        m_error = m_file.errorString();
        return;
    }

    const int bytesPerFrame = Channels * BitsPerSample / 8;
    QByteArray header;
    header.append("RIFF", 4);
    appendU32(header, 0);                   // patched at close
    header.append("WAVE", 4);
    header.append("fmt ", 4);
    appendU32(header, 16);                  // the size of what follows in this chunk
    appendU16(header, 1);                   // PCM
    appendU16(header, Channels);
    appendU32(header, quint32(sampleRate));
    appendU32(header, quint32(sampleRate * bytesPerFrame));
    appendU16(header, quint16(bytesPerFrame));
    appendU16(header, BitsPerSample);
    header.append("data", 4);
    appendU32(header, 0);                   // patched at close

    if (m_file.write(header) != header.size()) {
        m_error = m_file.errorString();
        m_file.close();
    }
}

WavWriter::~WavWriter()
{
    close();
}

bool WavWriter::isOpen() const
{
    return m_file.isOpen() && m_error.isEmpty();
}

QString WavWriter::error() const
{
    return m_error;
}

float WavWriter::peak() const
{
    return m_peak;
}

bool WavWriter::write(const float *left, const float *right, int frames)
{
    if (!isOpen() || frames <= 0) {
        return isOpen();
    }

    QByteArray block;
    block.resize(frames * Channels * BitsPerSample / 8);
    char *out = block.data();

    for (int frame = 0; frame < frames; ++frame) {
        for (const float sample : {left[frame], right[frame]}) {
            m_peak = std::max(m_peak, std::abs(sample));
            // Clipped rather than wrapped: a loud mix should sound loud and
            // wrong, not quiet and inside out.
            const float clamped = std::clamp(sample, -1.0f, 1.0f);
            qToLittleEndian(qint16(std::lround(clamped * 32767.0f)), out);
            out += 2;
        }
    }

    if (m_file.write(block) != block.size()) {
        m_error = m_file.errorString();
        return false;
    }
    m_frames += frames;
    return true;
}

bool WavWriter::close()
{
    if (m_closed || !m_file.isOpen()) {
        return m_error.isEmpty();
    }
    m_closed = true;

    const quint32 data = quint32(m_frames * Channels * BitsPerSample / 8);
    const auto patch = [this](qint64 at, quint32 value) {
        char bytes[4];
        qToLittleEndian(value, bytes);
        return m_file.seek(at) && m_file.write(bytes, 4) == 4;
    };

    if (!patch(4, 36 + data) || !patch(40, data)) {
        m_error = m_file.errorString();
    }
    m_file.close();
    return m_error.isEmpty();
}

namespace
{
/** A four-character chunk tag, compared without pretending it is a string. */
bool tagIs(const char *at, const char *what)
{
    return std::memcmp(at, what, 4) == 0;
}

quint32 readU32(const char *at)
{
    return quint32(quint8(at[0])) | (quint32(quint8(at[1])) << 8)
        | (quint32(quint8(at[2])) << 16) | (quint32(quint8(at[3])) << 24);
}

quint16 readU16(const char *at)
{
    return quint16(quint8(at[0])) | quint16(quint16(quint8(at[1])) << 8);
}
}

WavReader::WavReader(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        m_error = QStringLiteral("%1: %2").arg(path, file.errorString());
        return;
    }
    const QByteArray all = file.readAll();
    if (all.size() < 44) {
        m_error = QStringLiteral("%1: too short to be a WAV").arg(path);
        return;
    }
    const char *bytes = all.constData();
    if (!tagIs(bytes, "RIFF") || !tagIs(bytes + 8, "WAVE")) {
        m_error = QStringLiteral("%1: not a WAV").arg(path);
        return;
    }

    // Walk the chunks rather than assuming fmt is first and data is second.
    // Libraries written by trackers and by editors disagree about that, and a
    // reader that assumed would fail on half of them for no good reason.
    int format = 0;
    int bits = 0;
    qint64 dataAt = -1;
    qint64 dataSize = 0;
    qint64 at = 12;
    while (at + 8 <= all.size()) {
        const quint32 size = readU32(bytes + at + 4);
        const qint64 body = at + 8;
        if (tagIs(bytes + at, "fmt ") && size >= 16 && body + 16 <= all.size()) {
            format = readU16(bytes + body);
            m_channels = readU16(bytes + body + 2);
            m_sampleRate = int(readU32(bytes + body + 4));
            bits = readU16(bytes + body + 14);
            // "Extensible" names no format itself: the real one is the first
            // two bytes of the SubFormat GUID, twenty-four bytes into the
            // chunk. It has to be read rather than guessed from the bit
            // depth, because 32-bit integer and 32-bit float are both 32 bits
            // and reading one as the other is silence or noise -- not an
            // error, which is the worst way for a sample to be wrong.
            if (format == 0xFFFE && size >= 40 && body + 26 <= all.size()) {
                format = readU16(bytes + body + 24);
            }
        } else if (tagIs(bytes + at, "data")) {
            dataAt = body;
            dataSize = std::min<qint64>(size, all.size() - body);
        }
        // Chunks are padded to even lengths, and a reader that forgot walks
        // into the middle of the next one.
        at = body + size + (size & 1);
    }

    if (m_channels < 1 || m_channels > 2 || m_sampleRate <= 0 || dataAt < 0) {
        m_error = QStringLiteral("%1: no usable fmt or data chunk").arg(path);
        return;
    }

    // 1 is PCM and 3 is float, whether they were named directly or through an
    // extensible header. Anything still calling itself 0xFFFE here had a
    // SubFormat nobody could read, and is refused by name like any other.
    const bool isFloat = format == 3;
    if (format != 1 && !isFloat) {
        m_error = QStringLiteral("%1: WAV format %2 is not one this reads")
                      .arg(path)
                      .arg(format);
        return;
    }

    const int bytesPer = bits / 8;
    if (bytesPer < 2 || bytesPer > 4) {
        m_error = QStringLiteral("%1: %2-bit samples are not read here").arg(path).arg(bits);
        return;
    }

    const qint64 count = dataSize / bytesPer;
    m_samples.resize(size_t(count));
    for (qint64 index = 0; index < count; ++index) {
        const char *sample = bytes + dataAt + index * bytesPer;
        if (isFloat) {
            float value = 0;
            std::memcpy(&value, sample, sizeof(float));
            m_samples[size_t(index)] = value;
        } else if (bytesPer == 2) {
            m_samples[size_t(index)] = float(qint16(readU16(sample))) / 32768.0f;
        } else if (bytesPer == 3) {
            // Sign-extended by hand: there is no 24-bit integer to read into.
            const qint32 raw = (qint32(quint8(sample[2])) << 24)
                | (qint32(quint8(sample[1])) << 16) | (qint32(quint8(sample[0])) << 8);
            m_samples[size_t(index)] = float(raw >> 8) / 8388608.0f;
        } else {
            m_samples[size_t(index)] = float(qint32(readU32(sample))) / 2147483648.0f;
        }
    }
}

bool WavReader::isValid() const
{
    return m_error.isEmpty();
}

QString WavReader::error() const
{
    return m_error;
}

int WavReader::channels() const
{
    return m_channels;
}

int WavReader::sampleRate() const
{
    return m_sampleRate;
}

qint64 WavReader::frames() const
{
    return m_channels > 0 ? qint64(m_samples.size()) / m_channels : 0;
}

const std::vector<float> &WavReader::samples() const
{
    return m_samples;
}
