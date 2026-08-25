// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "wav.h"

#include <QtEndian>

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
