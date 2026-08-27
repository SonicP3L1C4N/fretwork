// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#pragma once

#include "score.h"
#include "portedoutput.h"
#include "synth.h"
#include "tracksynth.h"

#include <QHash>
#include <QList>
#include <QString>

#include <atomic>
#include <memory>
#include <vector>

typedef struct _fluid_audio_driver_t fluid_audio_driver_t;

/**
 * Live playback: the same engine the renderer drives, driven by an audio
 * device instead of a loop.
 *
 * That sentence is the reason `fill(frames, at)` was written the way it was.
 * Nothing below re-implements how a score is played; it decides which block of
 * frames comes next and mixes what the synths hand back.
 *
 * **What runs in the audio callback, and what does not.** The callback fills
 * buffers and reads a handful of atomics. It does not allocate, take a lock,
 * log, touch a file, or emit a signal -- all of which are ordinary and correct
 * in any other thread, and any of which will produce an audible dropout here.
 * Everything a user does -- pressing play, moving a fader, soloing a track --
 * is a single atomic store from their thread and a single atomic load from
 * this one, which is the whole of the concurrency design and deliberately so.
 *
 * Positions are therefore polled rather than pushed: a window asks where the
 * playhead is on a timer, because the alternative is the audio thread emitting
 * signals, which is how audio threads come to allocate.
 */
class Player
{
public:
    struct Options {
        QString soundFont;          //< empty means "find a General MIDI one"

        /**
         * An `.sfz` per track, by track number, for the parts that have one.
         *
         * A part with an entry here is played by recordings; a part without is
         * played by a General MIDI programme, as everything was before. Not
         * all or nothing, because a library exists for the guitar and does not
         * for the organ.
         */
        QHash<int, QString> samplers;
        QString audioDriver;        //< empty means "pipewire if there is one"
        int sampleRate = 48000;
        int periodFrames = 512;
        double gain = 0.4;

        /**
         * Give every track a pair of ports in the audio graph.
         *
         * The point of a synth per track, made true outside this window: a DAW
         * links to these and records the stems as they play, instead of
         * importing WAVs written after the fact. It replaces the ordinary
         * output rather than joining it -- two ways out would be two clocks,
         * and the whole reason the ports are one node is that there is one.
         */
        bool perTrackPorts = false;

        /**
         * Take the transport from the graph rather than from this program.
         *
         * With the ports open, the thing recording them is usually the thing
         * that should decide when to roll: press play in the DAW and the
         * stems arrive from the same place in the piece, every take. Only
         * meaningful with `perTrackPorts`, since it is the graph's transport
         * that is being followed and only the ported output can see it.
         */
        bool followTransport = false;
    };

    Player(const Score &score, const QList<int> &order, const Options &options);
    ~Player();

    Player(const Player &) = delete;
    Player &operator=(const Player &) = delete;

    bool isValid() const;
    QString error() const;

    /** Which audio driver actually opened, once one has. */
    QString driverName() const;

    /** How many port pairs are in the graph; zero where there are none. */
    int portCount() const;

    /** Whether the transport is the graph's rather than this program's. */
    bool isFollowing() const;

    /** Whether the graph has actually given a position to follow yet. */
    bool hasGraphTransport() const;

    // ---- transport, all callable from any thread ----

    void play();
    void pause();
    /** Stops and rewinds to the beginning. */
    void stop();

    bool isPlaying() const;
    bool hasFinished() const;

    void seekSeconds(double seconds);
    double positionSeconds() const;
    double lengthSeconds() const;

    // ---- the mixer ----

    int trackCount() const;
    void setMuted(int track, bool muted);
    void setSolo(int track, bool solo);
    void setGain(int track, float gain);

    bool isMuted(int track) const;
    bool isSolo(int track) const;
    float gain(int track) const;

    // ---- the click ----

    /**
     * A metronome on every beat, which no file contains.
     *
     * Not one of the tracks and deliberately outside the solo rule: soloing
     * the guitar to hear what it is doing is not a reason to lose the beat you
     * are hearing it against. It has a level of its own and nothing else.
     *
     * The synth behind it is built with the others whether it is switched on
     * or not, because building one costs a soundfont load and the only thread
     * that could ask for it later is the one that must never allocate.
     */
    void setClickEnabled(bool enabled);
    bool isClickEnabled() const;

    void setClickGain(float gain);
    float clickGain() const;

    /**
     * Whether a track is being heard right now, soloing taken into account.
     *
     * A window wants this to grey out a muted track: it is not the same as
     * "not muted", because one track soloed silences all the others.
     */
    bool isAudible(int track) const;

private:
    static int fillCallback(void *data, int frames, int effectCount, float *effects[],
                            int outputCount, float *outputs[]);
    void mix(int frames, float *left, float *right);

    static void portCallback(void *data, int frames, const PortedOutput::Transport &transport,
                             float *const *left, float *const *right);

    /** Fills one buffer pair per track rather than one pair for all of them. */
    void spread(int frames, const PortedOutput::Transport &transport, float *const *left,
                float *const *right);

    /** Moves the transport to where the graph says it is, seeking if it jumped. */
    qint64 followed(const PortedOutput::Transport &transport, int frames);

    /** Where the transport is now, and where it will be after `frames`. */
    qint64 advance(int frames);

    struct Channel {
        std::unique_ptr<Synth> synth;
        std::atomic<bool> muted{false};
        std::atomic<bool> solo{false};
        std::atomic<float> gain{1.0f};
    };

    Options m_options;
    QString m_error;
    QString m_driverName;

    std::vector<std::unique_ptr<Channel>> m_channels;
    std::unique_ptr<Channel> m_click;
    std::atomic<bool> m_clickEnabled{false};
    std::vector<float> m_scratchLeft;
    std::vector<float> m_scratchRight;

    fluid_settings_t *m_driverSettings = nullptr;
    fluid_audio_driver_t *m_driver = nullptr;
    std::unique_ptr<PortedOutput> m_ports;

    std::atomic<bool> m_playing{false};
    std::atomic<bool> m_finished{false};
    std::atomic<qint64> m_position{0};
    std::atomic<qint64> m_seekTo{-1};
    std::atomic<int> m_soloCount{0};
    std::atomic<bool> m_graphTransport{false};

    qint64 m_length = 0;
};
