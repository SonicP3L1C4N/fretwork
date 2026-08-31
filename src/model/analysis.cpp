// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "analysis.h"

#include <algorithm>
#include <cmath>

namespace
{
/**
 * What each key sounds like: how much of each degree a piece in it uses.
 *
 * Krumhansl and Kessler, 1982 -- measured by playing people a key and then a
 * note and asking how well the second belonged to the first, which is a better
 * foundation than anybody's opinion about which degrees matter. The shape is
 * the part that does the work: the tonic well clear of everything, the fifth
 * and the third behind it, and the five notes outside the scale at the bottom.
 *
 * Index 0 is the tonic, so the profile is rotated onto a candidate rather than
 * being written out twelve times.
 */
/*
 * Written a degree to a line rather than as twelve numbers in a row. Twelve
 * numbers in a row is how the tritone went missing out of the major profile
 * the first time this was written, taking the fifth's weight with it and
 * quietly handing every major key to its relative minor -- a wrong answer that
 * looked entirely plausible, since the relative minor is where a bad key guess
 * always lands. Labelled, that is a line that does not read right.
 */
constexpr std::array<double, 12> MajorProfile = {
    6.35, // the tonic
    2.23, // flat second
    3.48, // second
    2.33, // flat third
    4.38, // third
    4.09, // fourth
    2.52, // tritone
    5.19, // fifth
    2.39, // flat sixth
    3.66, // sixth
    2.29, // flat seventh
    2.88, // seventh
};
constexpr std::array<double, 12> MinorProfile = {
    6.33, // the tonic
    2.68, // flat second
    3.52, // second
    5.38, // third, which is where a minor key lives
    2.60, // major third
    3.53, // fourth
    2.54, // tritone
    4.75, // fifth
    3.98, // sixth
    2.69, // major sixth
    3.34, // seventh
    3.17, // major seventh
};

/**
 * The signature each tonic is written with, by pitch class.
 *
 * A table rather than arithmetic, for the same reason the tonics themselves
 * are one: at six accidentals the circle meets itself and two spellings are
 * equally correct, so what is wanted is the one a musician would write. D flat
 * major over C sharp major, F sharp major over G flat major, E flat minor over
 * D sharp minor. No rule produces that set; a music book does.
 */
constexpr std::array<int, 12> MajorSignatures = {
    0,  // C
    -5, // Db
    2,  // D
    -3, // Eb
    4,  // E
    -1, // F
    6,  // F#
    1,  // G
    -4, // Ab
    3,  // A
    -2, // Bb
    5,  // B
};
constexpr std::array<int, 12> MinorSignatures = {
    -3, // C
    4,  // C#
    -1, // D
    -6, // Eb
    1,  // E
    -4, // F
    3,  // F#
    -2, // G
    5,  // G#
    0,  // A
    -5, // Bb
    2,  // B
};

int pitchClassOf(int midi)
{
    return ((midi % 12) + 12) % 12;
}

/**
 * Whether a note contributes a pitch at all.
 *
 * A dead note is a click at no particular pitch and counting it as one would
 * be counting the sound of a hand. A note the importer could not give a pitch
 * to is not a pitch either.
 */
bool isPitched(const Note &note)
{
    return !note.muted && note.midi >= 0;
}

/** How long a beat lasts, in quarters, or nothing where it has no rhythm. */
double lengthOf(const Score &score, const Beat &beat)
{
    return score.rhythms.contains(beat.rhythm) ? score.rhythms.value(beat.rhythm).toDouble() : 0.0;
}

/**
 * Every beat of a track, in order, with the bar it is in.
 *
 * A tie destination is counted like any other note. It is not a new note being
 * struck, but it is the same pitch still sounding, and this is a measure of how
 * long things sound.
 */
template<typename Visit>
void walkTrack(const Score &score, int track, int firstVoice, int lastVoice, int firstBar,
               int lastBar, Visit visit)
{
    if (track < 0 || track >= score.tracks.size() || score.tracks.at(track).isPercussion()) {
        return;
    }
    const int last = std::min(lastBar, int(score.masterBars.size()) - 1);
    for (int bar = std::max(0, firstBar); bar <= last; ++bar) {
        const MasterBar &master = score.masterBars.at(bar);
        if (track >= master.bars.size()) {
            continue;
        }
        const QList<int> voices = score.bars.value(master.bars.at(track)).voices;
        for (int voice = firstVoice; voice <= lastVoice && voice < voices.size(); ++voice) {
            if (voices.at(voice) < 0) {
                continue;
            }
            const QList<int> beats = score.voices.value(voices.at(voice)).beats;
            for (int index = 0; index < beats.size(); ++index) {
                visit(bar, index, score.beats.value(beats.at(index)));
            }
        }
    }
}

/** Whether a beat of a bar is inside the passage, both ends included. */
bool holds(const Analysis::Passage &passage, int bar, int beat)
{
    const bool started = bar > passage.firstBar
        || (bar == passage.firstBar && beat >= passage.firstBeat);
    const bool ended = bar > passage.lastBar || (bar == passage.lastBar && beat > passage.lastBeat);
    return started && !ended;
}

/**
 * How alike two shapes are, between -1 and 1.
 *
 * Pearson, so what is being compared is the shape of the two and not their
 * size: a passage of four bars and a passage of four hundred in the same key
 * should read the same, and they do because both are measured against their
 * own average.
 */
double correlation(const std::array<double, 12> &left, const std::array<double, 12> &right)
{
    double leftMean = 0;
    double rightMean = 0;
    for (int index = 0; index < 12; ++index) {
        leftMean += left[index];
        rightMean += right[index];
    }
    leftMean /= 12;
    rightMean /= 12;

    double product = 0;
    double leftSquares = 0;
    double rightSquares = 0;
    for (int index = 0; index < 12; ++index) {
        const double leftOffset = left[index] - leftMean;
        const double rightOffset = right[index] - rightMean;
        product += leftOffset * rightOffset;
        leftSquares += leftOffset * leftOffset;
        rightSquares += rightOffset * rightOffset;
    }
    // A passage using every pitch class equally -- or none at all -- has no
    // shape to compare, and the honest answer is that it is no more like one
    // key than another.
    if (leftSquares <= 0 || rightSquares <= 0) {
        return 0;
    }
    return product / std::sqrt(leftSquares * rightSquares);
}
}

Analysis::Weights Analysis::weigh(const Score &score)
{
    Weights weights = {};
    for (int track = 0; track < score.tracks.size(); ++track) {
        walkTrack(score, track, 0, 3, 0, int(score.masterBars.size()) - 1,
                  [&](int, int, const Beat &beat) {
                      const double length = lengthOf(score, beat);
                      for (const int noteId : beat.notes) {
                          const Note &note = score.notes.value(noteId);
                          if (isPitched(note)) {
                              weights[pitchClassOf(note.midi)] += length;
                          }
                      }
                  });
    }
    return weights;
}

Analysis::Weights Analysis::weigh(const Score &score, const Passage &passage)
{
    Weights weights = {};
    walkTrack(score, passage.track, passage.voice, passage.voice, passage.firstBar,
              passage.lastBar, [&](int bar, int index, const Beat &beat) {
                  if (!holds(passage, bar, index)) {
                      return;
                  }
                  const double length = lengthOf(score, beat);
                  for (const int noteId : beat.notes) {
                      const Note &note = score.notes.value(noteId);
                      if (isPitched(note)) {
                          weights[pitchClassOf(note.midi)] += length;
                      }
                  }
              });
    return weights;
}

bool Analysis::isSilent(const Weights &weights)
{
    for (const double weight : weights) {
        if (weight > 0) {
            return false;
        }
    }
    return true;
}

Key::Signature Analysis::signatureFor(int tonicPitchClass, bool minor)
{
    const int pitchClass = ((tonicPitchClass % 12) + 12) % 12;
    return Key::Signature{minor ? MinorSignatures[pitchClass] : MajorSignatures[pitchClass], minor};
}

QList<Analysis::Fit> Analysis::ranked(const Weights &weights)
{
    QList<Fit> fits;
    fits.reserve(24);
    // Built in a fixed order and sorted stably, so that two keys the music
    // likes exactly equally come back in the same order every time.
    for (const bool minor : {false, true}) {
        for (int tonic = 0; tonic < 12; ++tonic) {
            std::array<double, 12> rotated = {};
            for (int degree = 0; degree < 12; ++degree) {
                rotated[degree] = weights[(tonic + degree) % 12];
            }
            fits.append(Fit{signatureFor(tonic, minor),
                            correlation(rotated, minor ? MinorProfile : MajorProfile)});
        }
    }
    std::stable_sort(fits.begin(), fits.end(),
                     [](const Fit &left, const Fit &right) { return left.fit > right.fit; });
    return fits;
}

Analysis::Fit Analysis::best(const Weights &weights)
{
    if (isSilent(weights)) {
        return Fit{};
    }
    return ranked(weights).constFirst();
}

QList<int> Analysis::pitched(const Score &score)
{
    QList<int> found;
    for (int track = 0; track < score.tracks.size(); ++track) {
        walkTrack(score, track, 0, 3, 0, int(score.masterBars.size()) - 1,
                  [&](int, int, const Beat &beat) {
                      for (const int noteId : beat.notes) {
                          if (isPitched(score.notes.value(noteId))) {
                              found.append(noteId);
                          }
                      }
                  });
    }
    return found;
}

QList<int> Analysis::pitched(const Score &score, const Passage &passage)
{
    QList<int> found;
    walkTrack(score, passage.track, passage.voice, passage.voice, passage.firstBar,
              passage.lastBar, [&](int bar, int index, const Beat &beat) {
                  if (!holds(passage, bar, index)) {
                      return;
                  }
                  for (const int noteId : beat.notes) {
                      if (isPitched(score.notes.value(noteId))) {
                          found.append(noteId);
                      }
                  }
              });
    return found;
}

namespace
{
/** The ones of those that the key does not hold. */
QList<int> strangers(const Score &score, const QList<int> &notes, const Key::Signature &key)
{
    QList<int> found;
    for (const int noteId : notes) {
        if (!Key::isDiatonic(score.notes.value(noteId).midi, key)) {
            found.append(noteId);
        }
    }
    return found;
}
}

QList<int> Analysis::outside(const Score &score, const Key::Signature &key)
{
    return strangers(score, pitched(score), key);
}

QList<int> Analysis::outside(const Score &score, const Passage &passage, const Key::Signature &key)
{
    return strangers(score, pitched(score, passage), key);
}
