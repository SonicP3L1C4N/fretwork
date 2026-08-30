// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "fretboard.h"

#include <QTest>

/**
 * Choosing where on the neck a pitch is played.
 *
 * The arithmetic half of this is one addition and is tested here mostly to
 * pin the capo down, which is the part of it people get backwards. The half
 * worth testing is the judgement: given the same pitch in six places, does it
 * pick the one a player would, and does it refuse where a player could not.
 *
 * The chord tests are the sharpest of them, because open E and barre F are
 * shapes that a guitarist can check by eye -- if the solver does not produce
 * 022100 for the notes of an open E, no explanation of the cost function is
 * worth having.
 */
class FretboardTest : public QObject
{
    Q_OBJECT

private:
    /** A guitar in standard tuning, which is what every figure below is on. */
    static Fretboard::Instrument guitar(int capo = 0, int frets = 24)
    {
        return Fretboard::Instrument{{40, 45, 50, 55, 59, 64}, capo, frets};
    }

    /** "3/5 4/0" -- a shape written out, so a failure says what it got. */
    static QString written(const QList<Fretboard::Position> &shape)
    {
        QStringList parts;
        for (const Fretboard::Position &at : shape) {
            parts.append(QStringLiteral("%1/%2").arg(at.string).arg(at.fret));
        }
        return parts.join(QLatin1Char(' '));
    }

    static void compare(const QList<Fretboard::Position> &shape, const QString &expected)
    {
        QCOMPARE(written(shape), expected);
    }

private Q_SLOTS:
    void theIdentityReadsBothWays()
    {
        const Fretboard::Instrument open = guitar();
        QCOMPARE(Fretboard::pitchAt(open, 0, 0), 40);
        QCOMPARE(Fretboard::pitchAt(open, 5, 12), 76);
        QCOMPARE(Fretboard::fretFor(open, 5, 76), 12);

        // A capo raises what the string sounds and leaves the numbers alone,
        // which is the whole of what a capo does: fret 3 is still fret 3 and
        // sounds two semitones higher than it did.
        const Fretboard::Instrument capoed = guitar(2);
        QCOMPARE(Fretboard::pitchAt(capoed, 0, 3), 45);
        QCOMPARE(Fretboard::fretFor(capoed, 0, 45), 3);

        // A string the instrument has not got has no fret on it.
        QVERIFY(Fretboard::fretFor(open, 6, 60) < 0);
        QCOMPARE(Fretboard::pitchAt(open, 6, 0), Fretboard::Unknown);
    }

    void everyPlaceAPitchSits()
    {
        // E4 on a guitar is in six places, which is the reason this component
        // exists at all.
        compare(Fretboard::candidates(guitar(), 64), QStringLiteral("0/24 1/19 2/14 3/9 4/5 5/0"));

        // And a pitch under the lowest string is in none of them.
        QVERIFY(Fretboard::candidates(guitar(), 39).isEmpty());
    }

    void aCapoShortensTheNeckRatherThanTheNumbers()
    {
        // The bottom of the range moves up with the capo: the open low string
        // is no longer available, because there is a capo on it.
        QVERIFY(!Fretboard::candidates(guitar(0), 40).isEmpty());
        QVERIFY(Fretboard::candidates(guitar(5), 40).isEmpty());

        // The top of it does not move at all, and that is not a coincidence:
        // the capo raises every open string by exactly the number of frets it
        // takes away from the far end of the neck.
        QVERIFY(!Fretboard::candidates(guitar(0), 88).isEmpty());
        QVERIFY(!Fretboard::candidates(guitar(5), 88).isEmpty());
        QVERIFY(Fretboard::candidates(guitar(0), 89).isEmpty());
        QVERIFY(Fretboard::candidates(guitar(5), 89).isEmpty());

        // Written from the capo: the highest fret anybody can play with a capo
        // at the fifth is the nineteenth, and it sounds what the twenty-fourth
        // did.
        compare(Fretboard::candidates(guitar(5), 88), QStringLiteral("5/19"));
    }

    void aShorterNeckIsAShorterNeck()
    {
        // A classical guitar stops at nineteen and the answer changes with it.
        compare(Fretboard::candidates(guitar(0, 19), 64), QStringLiteral("1/19 2/14 3/9 4/5 5/0"));
    }

    void aDrumKitIsNotAskedThisQuestion()
    {
        const Fretboard::Instrument kit{{}, 0, 24};
        QVERIFY(Fretboard::candidates(kit, 60).isEmpty());
        QVERIFY(!Fretboard::choose(kit, 60).isValid());
        QVERIFY(Fretboard::chord(kit, {60, 64}).isEmpty());
        QVERIFY(Fretboard::phrase(kit, {60, 64}).isEmpty());
    }

    void withNoHandItPicksTheFirstPosition()
    {
        // Nothing has been played, so nothing is near: what is left is the
        // preference for the bottom of the neck, which is where a tablature
        // program writes a note it has been told nothing else about.
        compare({Fretboard::choose(guitar(), 64)}, QStringLiteral("5/0"));
        compare({Fretboard::choose(guitar(), 60)}, QStringLiteral("4/1"));
    }

    void aHandKeepsItInPosition()
    {
        // The same C4, with a hand already at the fifth fret: on the G string
        // under the fingers rather than four frets down the neck.
        const Fretboard::Hand fifth{5, 4, true};
        compare({Fretboard::choose(guitar(), 60, fifth)}, QStringLiteral("3/5"));
    }

    void aStringAlreadySoundingIsNotOffered()
    {
        // Two notes on one string at one moment is not a chord, it is a
        // mistake -- so the next best place is used instead, which is two
        // frets up rather than four down.
        const Fretboard::Hand fifth{5, 4, true};
        compare({Fretboard::choose(guitar(), 60, fifth, {3})}, QStringLiteral("2/10"));
    }

    void aPitchThatIsNotThereIsRefused()
    {
        QVERIFY(!Fretboard::choose(guitar(), 20).isValid());
        // And so is one whose every string is spoken for.
        QVERIFY(!Fretboard::choose(guitar(), 64, {}, {0, 1, 2, 3, 4, 5}).isValid());
    }

    void anOpenStringIsAvoidedInAPhraseThatHasNone()
    {
        // A run at the fifth fret that drops to an open string is a break in
        // the sound rather than a convenience.
        const Fretboard::Hand fifth{5, 4, false};
        compare({Fretboard::choose(guitar(), 64, fifth)}, QStringLiteral("4/5"));

        // Where the music is using them, the open string is the cheapest thing
        // on the instrument and it is taken.
        const Fretboard::Hand welcoming{5, 4, true};
        compare({Fretboard::choose(guitar(), 64, welcoming)}, QStringLiteral("5/0"));
    }

    void theHandMovesTheLeastItCan()
    {
        const Fretboard::Hand fifth{5, 4, true};
        // Within reach, it has not moved.
        QCOMPARE(fifth.after({3, 7}).fret, 5);
        // Above it, far enough that the note is under the last finger.
        QCOMPARE(fifth.after({3, 10}).fret, 7);
        // Below it, the hand arrives on the note itself.
        QCOMPARE(fifth.after({3, 2}).fret, 2);
        // An open string is played with no hand in it and says nothing about
        // where the hand is.
        QCOMPARE(fifth.after({5, 0}).fret, 5);

        // From nowhere, the hand simply arrives.
        const Fretboard::Hand nowhere;
        QCOMPARE(nowhere.fret, Fretboard::Unknown);
        QCOMPARE(nowhere.after({2, 9}).fret, 9);
        QCOMPARE(nowhere.after({5, 0}).fret, Fretboard::Unknown);
    }

    void anOpenEComesOutAsAnOpenE()
    {
        // E2 B2 E3 G#3 B3 E4 -- the first chord anybody learns, and the
        // fingering everybody uses for it: 0 2 2 1 0 0.
        compare(Fretboard::chord(guitar(), {40, 47, 52, 56, 59, 64}),
                QStringLiteral("0/0 1/2 2/2 3/1 4/0 5/0"));
    }

    void aBarreFComesOutAsABarreF()
    {
        // F2 C3 F3 A3 C4 F4 -- 1 3 3 2 1 1, the barre at the first fret.
        compare(Fretboard::chord(guitar(), {41, 48, 53, 57, 60, 65}),
                QStringLiteral("0/1 1/3 2/3 3/2 4/1 5/1"));
    }

    void aChordFollowsTheHandUpTheNeck()
    {
        // The notes of an A minor, which is an open shape at the bottom of the
        // neck: A2 E3 A3 C4 E4.
        const QList<int> aMinor = {45, 52, 57, 60, 64};
        compare(Fretboard::chord(guitar(), aMinor), QStringLiteral("1/0 2/2 3/2 4/1 5/0"));

        // The same five notes asked for with the hand at the fifth fret and a
        // phrase that is not using open strings: the barre shape instead.
        const Fretboard::Hand fifth{5, 4, false};
        compare(Fretboard::chord(guitar(), aMinor, fifth), QStringLiteral("0/5 1/7 2/7 3/5 4/5"));
    }

    void aChordIsRefusedWhole()
    {
        // More notes than the instrument has strings.
        QVERIFY(Fretboard::chord(guitar(), {40, 45, 50, 55, 59, 64, 69}).isEmpty());
        // A note that is not on the instrument at all.
        QVERIFY(Fretboard::chord(guitar(), {40, 20}).isEmpty());
        // And a pair no one hand covers: the first fret and the twenty-third.
        QVERIFY(Fretboard::chord(guitar(), {41, 87}).isEmpty());
    }

    void aCrossedVoicingIsAvoidedWhereThereIsAChoice()
    {
        // Two strings tuned the same, so that the two shapes are identical in
        // every other term and only the crossing separates them. Artificial as
        // an instrument; it is the only way to ask about one term on its own.
        const Fretboard::Instrument pair{{40, 40}, 0, 24};
        compare(Fretboard::chord(pair, {40, 45}), QStringLiteral("0/0 1/5"));
        compare(Fretboard::chord(pair, {45, 40}), QStringLiteral("1/5 0/0"));

        // A unison is on two strings by definition and is not crossed
        // whichever way round it is written.
        const QList<Fretboard::Position> unison = Fretboard::chord(pair, {45, 45});
        QCOMPARE(unison.size(), 2);
        QVERIFY(unison.at(0).string != unison.at(1).string);
    }

    void aPhraseIsChosenOverTheWholeLine()
    {
        // B3 then E5, with no hand yet and a phrase not using open strings.
        // Taken one at a time, the B goes to the fourth fret because that is
        // the cheapest place for a note with nothing around it -- and leaves
        // the hand nowhere near the E, which is then a five-fret jump.
        const Fretboard::Hand closed{Fretboard::Unknown, 4, false};
        compare({Fretboard::choose(guitar(), 59, closed)}, QStringLiteral("3/4"));

        // Read as a phrase, the B is played at the ninth fret instead, which
        // is worse for the B alone and puts the hand exactly where the E is.
        compare(Fretboard::phrase(guitar(), {59, 76}, closed), QStringLiteral("2/9 5/12"));
    }

    void aPhraseKeepsItsPosition()
    {
        // C4 D4 E4 with the hand at the fifth fret: three notes under it
        // rather than a walk down to the first position.
        const Fretboard::Hand closed{5, 4, false};
        compare(Fretboard::phrase(guitar(), {60, 62, 64}, closed), QStringLiteral("3/5 3/7 4/5"));

        // The same run in a phrase that does use open strings ends on the open
        // top string, which costs nothing at all.
        const Fretboard::Hand welcoming{5, 4, true};
        compare(Fretboard::phrase(guitar(), {60, 62, 64}, welcoming), QStringLiteral("3/5 3/7 5/0"));
    }

    void aPhraseIsRefusedWhole()
    {
        // A phrase with one note left behind is not the phrase that was asked
        // for, so one unreachable pitch refuses all of it rather than most.
        QVERIFY(Fretboard::phrase(guitar(), {60, 20, 64}).isEmpty());
        QVERIFY(Fretboard::phrase(guitar(), {}).isEmpty());
    }

    void aPhraseOfOpenStringsLeavesTheHandWhereItWas()
    {
        // Nothing here moves the hand, so every state in the search is the one
        // it started in -- the case where "where the hand is" is still Unknown
        // at the end, which must not be mistaken for having found nothing.
        compare(Fretboard::phrase(guitar(), {40, 45, 50}), QStringLiteral("0/0 1/0 2/0"));
    }
};

QTEST_GUILESS_MAIN(FretboardTest)
#include "fretboardtest.moc"
