// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#pragma once

#include "audioinput.h"
#include "pitchdetector.h"
#include "tuner.h"

#include <QElapsedTimer>
#include <QObject>
#include <QQmlEngine>
#include <QStringList>
#include <QTimer>
#include <QVariantList>

#include <memory>
#include <vector>

/**
 * The tuner, as the window sees it.
 *
 * The same three pieces the command line uses -- `AudioInput` listens,
 * `Pitch::Detector` hears, `Tuner` decides what the number means -- with a
 * timer on top turning them into properties QML can bind to. None of the three
 * knows this class exists, which is the same arrangement `Session` has with
 * the player and for the same reason.
 *
 * **The microphone is open only while the panel is.** Nothing about a
 * tablature program justifies holding an audio input open all day, so
 * `listening` is what creates and destroys the stream rather than a flag on
 * one that is always there.
 *
 * **What it heard stays on screen for a moment after it stops.** A plucked
 * string dies away in a second or two and a readout that blanked the instant
 * it did would be unreadable exactly when somebody is looking down at a peg
 * rather than at the screen. The reading is held, and `fresh` says whether it
 * is still arriving or is the last thing that did.
 */
class Tuning : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    /** Open the input and start listening. Closing it releases the device. */
    Q_PROPERTY(bool listening READ isListening WRITE setListening NOTIFY listeningChanged)

    /** True once audio is actually arriving, which is later than `listening`. */
    Q_PROPERTY(bool running READ isRunning NOTIFY stateChanged)
    Q_PROPERTY(QString error READ error NOTIFY stateChanged)

    /**
     * The strings to tune, as MIDI pitches with any capo already in them.
     *
     * Set from the score's current track. Empty means a guitar in standard
     * tuning, which is what somebody who opened the window with no score in it
     * is holding.
     */
    Q_PROPERTY(QVariantList strings READ strings WRITE setStrings NOTIFY stringsChanged)

    /** "C2", "F2", "A#2" -- one per string, lowest first. */
    Q_PROPERTY(QStringList stringNames READ stringNames NOTIFY stringsChanged)

    // ---- what is being heard, all of it changing together ----

    Q_PROPERTY(bool heard READ heard NOTIFY readingChanged)
    /** False while a held reading is fading rather than arriving. */
    Q_PROPERTY(bool fresh READ isFresh NOTIFY readingChanged)
    Q_PROPERTY(int string READ string NOTIFY readingChanged)
    Q_PROPERTY(double cents READ cents NOTIFY readingChanged)
    Q_PROPERTY(double hertz READ hertz NOTIFY readingChanged)
    Q_PROPERTY(bool inTune READ isInTune NOTIFY readingChanged)
    Q_PROPERTY(QString noteName READ noteName NOTIFY readingChanged)

    /** "in tune", "flat", "listening — play a string" -- already translated. */
    Q_PROPERTY(QString message READ message NOTIFY readingChanged)

    /** 0 to 1 for a level meter, so silence and a dead cable look different. */
    Q_PROPERTY(double level READ level NOTIFY readingChanged)

public:
    explicit Tuning(QObject *parent = nullptr);
    ~Tuning() override;

    bool isListening() const;
    void setListening(bool listening);

    bool isRunning() const;
    QString error() const;

    QVariantList strings() const;
    void setStrings(const QVariantList &strings);
    QStringList stringNames() const;

    bool heard() const;
    bool isFresh() const;
    int string() const;
    double cents() const;
    double hertz() const;
    bool isInTune() const;
    QString noteName() const;
    QString message() const;
    double level() const;

Q_SIGNALS:
    void listeningChanged();
    void stateChanged();
    void stringsChanged();
    void readingChanged();

private:
    void tick();
    void clearReading();

    QList<Tuner::StringTarget> m_targets;
    QVariantList m_strings;

    std::unique_ptr<AudioInput> m_input;
    std::unique_ptr<Pitch::Detector> m_detector;
    std::vector<float> m_window;
    double m_rate = 0;

    QTimer m_ticker;
    QElapsedTimer m_since;

    bool m_listening = false;
    bool m_wasRunning = false;
    QString m_error;

    Tuner::Reading m_reading;
    bool m_fresh = false;
    double m_level = 0;
    bool m_noteInIt = false;
};
