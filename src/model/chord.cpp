// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "chord.h"

#include <QStringList>

#include <algorithm>
#include <array>

namespace
{
int pitchClassOf(int value)
{
    return ((value % 12) + 12) % 12;
}

/** The pitch classes of a key, in scale order from its tonic. */
QList<int> degreesOf(const Key::Signature &key)
{
    QList<int> degrees;
    degrees.reserve(7);
    for (const Key::Spelling &degree : Key::scaleOf(key)) {
        degrees.append(pitchClassOf(Key::midiOf(degree)));
    }
    return degrees;
}

/** What a third and a fifth of these sizes add up to. */
Chord::Quality qualityOf(int third, int fifth)
{
    if (third == 4 && fifth == 7) {
        return Chord::Quality::Major;
    }
    if (third == 3 && fifth == 7) {
        return Chord::Quality::Minor;
    }
    if (third == 3 && fifth == 6) {
        return Chord::Quality::Diminished;
    }
    return Chord::Quality::Augmented;
}
}

bool Chord::operator==(const Named &left, const Named &right)
{
    return left.root == right.root && left.quality == right.quality;
}

bool Chord::operator!=(const Named &left, const Named &right)
{
    return !(left == right);
}

QList<int> Chord::intervalsOf(Quality quality)
{
    switch (quality) {
    case Quality::Major:
        return {0, 4, 7};
    case Quality::Minor:
        return {0, 3, 7};
    case Quality::Diminished:
        return {0, 3, 6};
    case Quality::Augmented:
        return {0, 4, 8};
    case Quality::Dominant7:
        return {0, 4, 7, 10};
    case Quality::Major7:
        return {0, 4, 7, 11};
    case Quality::Minor7:
        return {0, 3, 7, 10};
    case Quality::HalfDiminished7:
        return {0, 3, 6, 10};
    }
    return {0, 4, 7};
}

QList<int> Chord::pitchClassesOf(const Named &chord)
{
    QList<int> classes;
    for (const int interval : intervalsOf(chord.quality)) {
        classes.append(pitchClassOf(chord.root + interval));
    }
    return classes;
}

QString Chord::nameOf(const Named &chord, const Key::Signature &key)
{
    const QString root = Key::nameOf(Key::spell(60 + chord.root, key));
    switch (chord.quality) {
    case Quality::Major:
        return root;
    case Quality::Minor:
        return root + QStringLiteral("m");
    case Quality::Diminished:
        return root + QStringLiteral("°");
    case Quality::Augmented:
        return root + QStringLiteral("+");
    case Quality::Dominant7:
        return root + QStringLiteral("7");
    case Quality::Major7:
        return root + QStringLiteral("maj7");
    case Quality::Minor7:
        return root + QStringLiteral("m7");
    case Quality::HalfDiminished7:
        return root + QStringLiteral("ø7");
    }
    return root;
}

QString Chord::degreeOf(const Named &chord, const Key::Signature &key)
{
    static const QStringList upper = {
        QStringLiteral("I"),  QStringLiteral("II"), QStringLiteral("III"), QStringLiteral("IV"),
        QStringLiteral("V"),  QStringLiteral("VI"), QStringLiteral("VII"),
    };
    const QList<int> scale = degreesOf(key);
    const int degree = int(scale.indexOf(pitchClassOf(chord.root)));
    if (degree < 0) {
        // A chord whose root is not in the key has no degree in it. That is a
        // statement about the chord and not a complaint about it.
        return QString();
    }
    const bool small = chord.quality == Quality::Minor || chord.quality == Quality::Diminished
        || chord.quality == Quality::Minor7 || chord.quality == Quality::HalfDiminished7;
    QString name = small ? upper.at(degree).toLower() : upper.at(degree);
    if (chord.quality == Quality::Diminished || chord.quality == Quality::HalfDiminished7) {
        name += QStringLiteral("°");
    }
    if (chord.quality == Quality::Augmented) {
        name += QStringLiteral("+");
    }
    return name;
}

QList<Chord::Named> Chord::diatonic(const Key::Signature &key)
{
    const QList<int> scale = degreesOf(key);
    QList<Named> chords;
    chords.reserve(7);
    for (int degree = 0; degree < 7; ++degree) {
        const int root = scale.at(degree);
        const int third = pitchClassOf(scale.at((degree + 2) % 7) - root);
        const int fifth = pitchClassOf(scale.at((degree + 4) % 7) - root);
        chords.append(Named{root, qualityOf(third, fifth)});
    }
    return chords;
}

QList<Chord::Named> Chord::borrowed(const Key::Signature &key)
{
    // The parallel key: the same tonic, the other mode. Spelled in its own
    // signature rather than in this one, so that a flat sixth in C major is
    // written as the A flat it is and not as a G sharp.
    const int tonic = pitchClassOf(Key::midiOf(Key::tonicOf(key)));
    const QList<Named> here = diatonic(key);
    QList<Named> chords;
    for (const Named &chord : diatonic(Key::signatureFor(tonic, !key.minor))) {
        if (!here.contains(chord)) {
            chords.append(chord);
        }
    }
    return chords;
}

QList<Fretboard::Position> Chord::shapeOn(const Fretboard::Instrument &instrument,
                                          const Named &chord, const Fretboard::Hand &hand)
{
    if (instrument.tuning.isEmpty() || hand.fret < 0) {
        return {};
    }
    const QList<int> classes = pitchClassesOf(chord);
    const int highest = instrument.frets - instrument.capo;

    // Which frets the hand covers -- and here, unlike everywhere else, an open
    // string is not free. The solver treats fret nought as always available
    // because a single note on an open string needs no hand at all. A chord
    // does: the hand that is holding a shape at the first fret is lying across
    // the strings, and it cannot also be leaving them open. Allowing it turned
    // an F barre into 103211, which is not a chord anybody can play.
    const auto inReach = [&](int fret) {
        return fret >= hand.fret && fret <= hand.fret + hand.span - 1;
    };
    // The lowest chord tone this string can sound under that hand, or -1.
    const auto lowestOf = [&](int string, const QList<int> &wanted) {
        for (int fret = 0; fret <= highest; ++fret) {
            if (!inReach(fret)) {
                continue;
            }
            if (wanted.contains(pitchClassOf(Fretboard::pitchAt(instrument, string, fret)))) {
                return fret;
            }
        }
        return -1;
    };

    // The bass is the root, so the search starts at the lowest string that can
    // sound one. Strings below it are left out rather than filled with a third:
    // a chord standing on its third is a different chord.
    QList<Fretboard::Position> shape;
    for (int bass = 0; bass < instrument.tuning.size(); ++bass) {
        const int root = lowestOf(bass, {chord.root});
        if (root < 0) {
            continue;
        }
        shape.append(Fretboard::Position{bass, root});
        for (int string = bass + 1; string < instrument.tuning.size(); ++string) {
            const int fret = lowestOf(string, classes);
            if (fret >= 0) {
                shape.append(Fretboard::Position{string, fret});
            }
        }
        break;
    }

    // Every note of it has to be there, or it is not the chord: two thirds of
    // a diminished triad is an interval.
    QList<int> sounded;
    for (const Fretboard::Position &at : shape) {
        const int pitchClass = pitchClassOf(Fretboard::pitchAt(instrument, at.string, at.fret));
        if (!sounded.contains(pitchClass)) {
            sounded.append(pitchClass);
        }
    }
    if (sounded.size() != classes.size()) {
        return {};
    }

    // And a hand has to be able to hold it. Three strings stopped at the same
    // fret is a barre -- one finger laid across the neck -- and a finger lying
    // across the neck is also lying across every string between them, so no
    // string in the shape can be open. Without this the first fret produces an
    // F as 103211, which has every note of an F in it and cannot be played by
    // anybody: it wants a barre at the first fret and the A string open
    // underneath it at the same time.
    int lowest = instrument.frets;
    for (const Fretboard::Position &at : shape) {
        if (at.fret > 0) {
            lowest = std::min(lowest, at.fret);
        }
    }
    int stopped = 0;
    bool anyOpen = false;
    for (const Fretboard::Position &at : shape) {
        stopped += at.fret == lowest ? 1 : 0;
        anyOpen = anyOpen || at.fret == 0;
    }
    if (stopped >= 3 && anyOpen) {
        return {};
    }
    return shape;
}

QList<Fretboard::Position> Chord::shapeOf(const Fretboard::Instrument &instrument,
                                          const Named &chord)
{
    for (int fret = 0; fret <= 12; ++fret) {
        const QList<Fretboard::Position> shape =
            shapeOn(instrument, chord, Fretboard::Hand{fret, 4, true});
        if (!shape.isEmpty()) {
            return shape;
        }
    }
    return {};
}

QList<Fretboard::Position> Chord::shapeNear(const Fretboard::Instrument &instrument,
                                            const Named &chord, int handFret)
{
    if (handFret > 0) {
        const QList<Fretboard::Position> here =
            shapeOn(instrument, chord, Fretboard::Hand{handFret, 4, true});
        if (!here.isEmpty()) {
            return here;
        }
    }
    return shapeOf(instrument, chord);
}
