// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "notename.h"
#include "tuner.h"

#include <QTest>

#include <cmath>

/**
 * What a frequency means when there is a score in front of you.
 *
 * The arithmetic is the easy half and is checked here against numbers anybody
 * can look up. The half worth arguing about is when the tuner refuses to name
 * a string: a guitar tuned from nothing can be a long way out, and a tuner
 * that decided a very flat B was a slightly sharp G would have somebody
 * winding the wrong peg.
 */
class TunerTest : public QObject
{
    Q_OBJECT

private:
    static Track guitar(const QList<int> &tuning, int capo = 0)
    {
        Track track;
        track.name = QStringLiteral("Guitar");
        track.instrumentType = QStringLiteral("electricGuitar");
        track.tuning = tuning;
        track.capo = capo;
        return track;
    }

    static const QList<int> &standard()
    {
        static const QList<int> tuning = {40, 45, 50, 55, 59, 64};
        return tuning;
    }

private Q_SLOTS:
    void agreesWithTheNumbersEverybodyKnows()
    {
        QCOMPARE(std::lround(Tuner::hertzForMidi(69)), 440L);      // A4
        QCOMPARE(std::lround(Tuner::hertzForMidi(40)), 82L);       // E2, the low string
        QCOMPARE(std::lround(Tuner::hertzForMidi(64)), 330L);      // E4, the high one
        QCOMPARE(std::lround(Tuner::hertzForMidi(60)), 262L);      // middle C

        // An octave is 1200 cents and a semitone is 100, whichever way round.
        QCOMPARE(std::lround(Tuner::cents(880, 440)), 1200L);
        QCOMPARE(std::lround(Tuner::cents(440, 880)), -1200L);
        QCOMPARE(std::lround(Tuner::cents(Tuner::hertzForMidi(41), Tuner::hertzForMidi(40))),
                 100L);

        // And the two conversions are inverses, which is what everything else
        // in here quietly relies on.
        for (int midi = 28; midi <= 88; ++midi) {
            QVERIFY(std::abs(Tuner::midiForHertz(Tuner::hertzForMidi(midi)) - midi) < 1e-9);
        }
    }

    void namesPitchesTheWayAGuitaristWould()
    {
        QCOMPARE(NoteName::of(40), QStringLiteral("E2"));
        QCOMPARE(NoteName::of(60), QStringLiteral("C4"));
        QCOMPARE(NoteName::of(69), QStringLiteral("A4"));
        QCOMPARE(NoteName::of(36), QStringLiteral("C2"));
        QCOMPARE(NoteName::of(63), QStringLiteral("D#4"));
        QCOMPARE(NoteName::pitchClass(63), QStringLiteral("D#"));
        // Below the bottom of the piano the octave still counts downwards
        // rather than wrapping, which integer division would have done.
        QCOMPARE(NoteName::of(0), QStringLiteral("C-1"));
    }

    void takesItsTargetsFromTheScore()
    {
        const QList<Tuner::StringTarget> targets = Tuner::targetsFor(guitar(standard()));
        QCOMPARE(targets.size(), 6);
        QCOMPARE(targets.first().index, 0);
        QCOMPARE(targets.first().name, QStringLiteral("E2"));
        QCOMPARE(targets.last().name, QStringLiteral("E4"));

        // Drop C, which is the point of reading the tuning out of the file at
        // all: three of these strings are not where a chromatic tuner's owner
        // would expect them.
        const QList<Tuner::StringTarget> dropped =
            Tuner::targetsFor(guitar({36, 43, 48, 53, 57, 62}));
        QCOMPARE(dropped.first().name, QStringLiteral("C2"));
        QCOMPARE(std::lround(dropped.first().hertz), 65L);
    }

    void putsTheCapoInTheTarget()
    {
        // What the string will sound when it is plucked, which is what gets
        // measured. A capo at the second fret is two semitones of it.
        const QList<Tuner::StringTarget> targets = Tuner::targetsFor(guitar(standard(), 2));
        QCOMPARE(targets.first().midi, 42);
        QCOMPARE(targets.first().name, QStringLiteral("F#2"));

        const Tuner::Reading reading =
            Tuner::read(Tuner::hertzForMidi(42), 0.95, targets);
        QCOMPARE(reading.string, 0);
        QVERIFY(std::abs(reading.cents) < 0.001);
    }

    void hasNothingToSayAboutADrumKit()
    {
        Track kit;
        kit.instrumentType = QStringLiteral("drumKit");
        kit.tuning = {0, 0, 0, 0, 0, 0};
        QVERIFY(Tuner::targetsFor(kit).isEmpty());
    }

    void findsTheStringAndHowFarOutItIs()
    {
        const QList<Tuner::StringTarget> targets = Tuner::targetsFor(guitar(standard()));

        // The A string, ten cents flat.
        const double flat = Tuner::hertzForMidi(45) * std::pow(2.0, -10.0 / 1200.0);
        const Tuner::Reading reading = Tuner::read(flat, 0.98, targets);
        QVERIFY(reading.heard);
        QCOMPARE(reading.string, 1);
        QVERIFY2(std::abs(reading.cents + 10.0) < 0.01,
                 qPrintable(QString::number(reading.cents)));
        QCOMPARE(Tuner::describe(reading), QStringLiteral("flat"));

        // Dead on is dead on, in both directions.
        for (const Tuner::StringTarget &target : targets) {
            const Tuner::Reading exact = Tuner::read(target.hertz, 1.0, targets);
            QCOMPARE(exact.string, target.index);
            QCOMPARE(Tuner::describe(exact), QStringLiteral("in tune"));
        }
    }

    void namesTheNoteEvenWhenItNamesNoString()
    {
        const QList<Tuner::StringTarget> targets = Tuner::targetsFor(guitar(standard()));

        // Midway between the E and the A -- a quarter of a tone above a G2,
        // 250 cents from either string, and therefore neither of them. The
        // note is still named, because "I have no idea" is not what was heard.
        const Tuner::Reading reading = Tuner::read(Tuner::hertzForMidi(42.5), 0.99, targets);
        QVERIFY(reading.heard);
        QCOMPARE(reading.string, -1);
        QCOMPARE(reading.noteName, QStringLiteral("G2"));
        QCOMPARE(reading.nearestMidi, 43);
        QCOMPARE(std::lround(reading.nearestCents), -50L);
        QCOMPARE(Tuner::describe(reading), QStringLiteral("no string near"));
    }

    void willNotGuessBetweenTwoStringsThatAreFarApart()
    {
        const QList<Tuner::StringTarget> targets = Tuner::targetsFor(guitar(standard()));

        // The G and the B are a major third apart -- 400 cents, the smallest
        // gap in standard tuning -- so no string answers past 200 cents.
        QCOMPARE(std::lround(Tuner::acceptanceCents(targets)), 200L);

        // A tone and a half above the G: still the G, and told how badly.
        const double sharp = Tuner::hertzForMidi(55) * std::pow(2.0, 150.0 / 1200.0);
        QCOMPARE(Tuner::read(sharp, 0.9, targets).string, 3);
        QCOMPARE(Tuner::describe(Tuner::read(sharp, 0.9, targets)), QStringLiteral("very sharp"));

        // The E and the A are a fourth apart, so between them there is room to
        // be nowhere: 250 cents from each is neither, rather than the nearer
        // one by a hair.
        const double between = Tuner::hertzForMidi(40) * std::pow(2.0, 250.0 / 1200.0);
        QCOMPARE(Tuner::read(between, 0.9, targets).string, -1);

        // Twenty cents inside the window is inside it.
        const double inside = Tuner::hertzForMidi(40) * std::pow(2.0, 180.0 / 1200.0);
        QCOMPARE(Tuner::read(inside, 0.9, targets).string, 0);
    }

    void narrowsTheWindowWhenTheTuningIsCloseGrained()
    {
        // Two strings a semitone apart is an odd thing for a score to ask for
        // and the importer will hand it over without comment. Whatever the
        // tuning, two targets must never both answer to the same note, so the
        // window closes to half the smallest gap rather than staying at 200.
        const QList<Tuner::StringTarget> targets = Tuner::targetsFor(guitar({40, 41, 50, 55}));
        QCOMPARE(std::lround(Tuner::acceptanceCents(targets)), 50L);

        // Forty cents above the second string is still the second string.
        const double near = Tuner::hertzForMidi(41) * std::pow(2.0, 40.0 / 1200.0);
        QCOMPARE(Tuner::read(near, 0.9, targets).string, 1);

        // Three semitones above it is a long way from anything, and in a
        // tuning this close-grained that is far enough to say so.
        QCOMPARE(Tuner::read(Tuner::hertzForMidi(44), 0.9, targets).string, -1);
    }

    void saysNothingAboutNothing()
    {
        const Tuner::Reading reading = Tuner::read(0, 0, Tuner::standardGuitar());
        QVERIFY(!reading.heard);
        QCOMPARE(reading.string, -1);
        QCOMPARE(Tuner::describe(reading), QStringLiteral("listening"));
    }
};

QTEST_GUILESS_MAIN(TunerTest)
#include "tunertest.moc"
