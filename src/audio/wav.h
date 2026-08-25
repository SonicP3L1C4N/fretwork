// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#pragma once

#include <QFile>
#include <QString>

/**
 * Writes a stereo WAV file, a block at a time.
 *
 * Streamed rather than assembled: five minutes of 48 kHz stereo is fifty-odd
 * megabytes, and a stem export writes several of them at once. The header is
 * written with its lengths blank and patched when the file is closed, which is
 * how every streaming WAV writer does it and the reason so many WAV files in
 * the world have wrong lengths -- so close() is not optional here, and the
 * destructor calls it.
 *
 * Only what is needed: 16-bit PCM, two channels. Anything more is libsndfile's
 * job, and this project does not need libsndfile to write one format.
 */
class WavWriter
{
public:
    WavWriter(const QString &path, int sampleRate);
    ~WavWriter();

    WavWriter(const WavWriter &) = delete;
    WavWriter &operator=(const WavWriter &) = delete;

    bool isOpen() const;
    QString error() const;

    /** Interleaves and writes `frames` samples, clipping rather than wrapping. */
    bool write(const float *left, const float *right, int frames);

    /** Patches the lengths into the header. Called by the destructor if not before. */
    bool close();

    /** The loudest sample seen so far, 1.0 being full scale. */
    float peak() const;

private:
    QFile m_file;
    int m_sampleRate;
    qint64 m_frames = 0;
    float m_peak = 0;
    QString m_error;
    bool m_closed = false;
};
