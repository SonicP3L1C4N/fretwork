// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#pragma once

#include <QFile>
#include <QString>

#include <vector>

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
/**
 * Reads a WAV file into memory, whole.
 *
 * Whole rather than streamed, because the thing that reads these is a sampler
 * and a sampler needs any part of a sample at any moment: a note struck at the
 * top of a bar wants frame zero, and the note under it wants frame forty
 * thousand of a different file. Streaming that from disk is a buffering
 * problem nobody has to have while a guitar library is a few hundred megabytes
 * and a machine has several gigabytes.
 *
 * Reads what sample libraries are actually written in -- 16- and 24-bit PCM
 * and 32-bit float, mono or stereo -- and refuses anything else by name rather
 * than by returning silence. A sample that loaded as nothing would be a
 * missing note somebody would look for in the wrong place.
 */
class WavReader
{
public:
    explicit WavReader(const QString &path);

    bool isValid() const;
    QString error() const;

    int channels() const;
    int sampleRate() const;

    /** How many frames, which is samples divided by channels. */
    qint64 frames() const;

    /** Interleaved, -1 to 1, `frames() * channels()` of them. */
    const std::vector<float> &samples() const;

private:
    QString m_error;
    int m_channels = 0;
    int m_sampleRate = 0;
    std::vector<float> m_samples;
};

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
