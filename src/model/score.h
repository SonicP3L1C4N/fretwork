// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#pragma once

#include "rational.h"

#include <QHash>
#include <QList>
#include <QString>

/**
 * The document, shaped the way the music is rather than the way any one file
 * format writes it down.
 *
 * Beats, notes and rhythms are held in tables and referred to by id, which
 * looks like an optimisation and is not: Guitar Pro's own format deduplicates
 * them, a 176-bar score with four tracks holding 704 bars but only 235 distinct
 * beats. A model that expanded them into a tree would be four times the size
 * and would still have to put them back together to answer "is this the same
 * riff again".
 *
 * Nothing here knows about MIDI, about sample rates, or about how a bend is
 * played. That is `Timeline`'s business, and keeping it out of the document is
 * what lets the same document be drawn on a screen and rendered to audio
 * without either one deciding for the other.
 */

/** How loud a beat is. Every gpif beat carries one; it is the only velocity there is. */
enum class Dynamic {
    PPP, PP, P, MP, MF, F, FF, FFF
};

/** How a note gets from its own pitch to the next one. */
enum class SlideType {
    None,
    Legato,      //< into the following note
    Shift,       //< into the following note, restruck
    OutDown, OutUp, InFromBelow, InFromAbove,
};

struct Note {
    int midi = -1;          //< the sounding pitch, which is what playback wants
    int string = -1;        //< 0 is the lowest string
    int fret = 0;

    // Ties: a note may be both, in the middle of a chain across several bars.
    bool tieOrigin = false;
    bool tieDestination = false;

    bool muted = false;         //< a dead note: a click at no particular pitch
    bool palmMuted = false;
    bool letRing = false;
    bool accent = false;
    bool ghost = false;         //< gpif calls this AntiAccent
    bool vibrato = false;
    bool hammerOrigin = false;  //< gpif calls hammer-ons and pull-offs "Hopo"
    bool hammerDestination = false;
    bool tapped = false;
    bool harmonic = false;
    SlideType slide = SlideType::None;

    /**
     * A bend, as gpif describes it: values in hundredths of a semitone (100 is
     * a semitone, 200 a whole tone), at offsets given as percentages along the
     * note. Middle points are optional and -1 where absent.
     */
    bool bended = false;
    int bendOriginValue = 0;
    int bendMiddleValue = 0;
    int bendDestinationValue = 0;
    int bendOriginOffset = 0;
    int bendMiddleOffset1 = -1;
    int bendMiddleOffset2 = -1;
    int bendDestinationOffset = 100;
};

struct Beat {
    int rhythm = -1;            //< into Score::rhythms
    QList<int> notes;           //< into Score::notes; empty is a rest
    Dynamic dynamic = Dynamic::MF;
    bool tremolo = false;
    bool brush = false;
};

struct Voice {
    QList<int> beats;           //< in time order, from the start of the bar
};

struct Bar {
    QList<int> voices;          //< four slots; -1 where the voice is silent
};

/**
 * One bar of the score, across every track at once -- which is the level at
 * which time signatures, keys and repeats are decided.
 */
struct MasterBar {
    QList<int> bars;            //< one Bar id per track, in track order
    int numerator = 4;
    int denominator = 4;
    QString section;            //< "Intro", "Chorus" -- where the score names it

    bool repeatStart = false;
    bool repeatEnd = false;
    int repeatCount = 0;        //< how many times the section is played in total
    bool alternateEndings = false;

    /** The length of the bar, in quarters. */
    Rational length() const
    {
        return Rational(numerator * 4, denominator);
    }
};

struct Track {
    QString name;
    QString instrumentType;     //< "electricGuitar", "drumKit", "saxophone"
    int program = 0;            //< General MIDI programme
    QList<int> tuning;          //< MIDI pitch per string, lowest string first
    int capo = 0;

    /**
     * A drum kit is identified by its instrument type and never by its
     * programme, which is 0 -- an acoustic piano. Every score in the corpus has
     * a kit, so getting this wrong is not an edge case.
     */
    bool isPercussion() const
    {
        return instrumentType == QLatin1String("drumKit");
    }

    /** Strings to allocate a channel each to; 0 where the idea does not apply. */
    int stringCount() const
    {
        return isPercussion() ? 0 : int(tuning.size());
    }
};

struct TempoChange {
    int bar = 0;                //< index into Score::masterBars, as notated
    double position = 0;        //< quarters into that bar
    double quarterBpm = 120;    //< already converted to beats-per-minute in quarters
};

struct Score {
    QString version;            //< the writing application's, e.g. "8.1.3"
    QString title;
    QString artist;
    QString album;

    QList<Track> tracks;
    QList<MasterBar> masterBars;

    QHash<int, Bar> bars;
    QHash<int, Voice> voices;
    QHash<int, Beat> beats;
    QHash<int, Note> notes;
    QHash<int, Rational> rhythms;   //< a duration in quarters, dots and tuplet applied

    QList<TempoChange> tempos;

    bool isEmpty() const
    {
        return tracks.isEmpty() || masterBars.isEmpty();
    }
};
