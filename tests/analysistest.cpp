// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "analysis.h"

#include <QTest>

/**
 * Reading a key off the notes.
 *
 * The pair of tests worth understanding first is the C major and A minor one.
 * Both passages use the same seven pitch classes -- they are the same key
 * signature -- and they differ only in which note is leaned on. That is the
 * whole of what this layer can and cannot do: the signature falls out of the
 * pitch content and is solid, and the mode is a judgement about emphasis and
 * is not. Any test here that made the mode look certain would be lying about
 * the method.
 */
class AnalysisTest : public QObject
{
    Q_OBJECT

private:
    static Score empty(int bars, bool percussion = false)
    {
        Score score;
        Track part;
        part.name = QStringLiteral("Part");
        part.instrumentType = percussion ? QStringLiteral("drumKit")
                                         : QStringLiteral("electricGuitar");
        part.tuning = {40, 45, 50, 55, 59, 64};
        score.tracks = {part};
        for (int bar = 0; bar < bars; ++bar) {
            MasterBar master;
            master.bars = {bar};
            score.masterBars.append(master);
            score.bars.insert(bar, Bar{{bar, -1, -1, -1}});
            score.voices.insert(bar, Voice{});
        }
        return score;
    }

    /** A beat on the end of a bar: some pitches, sounding for a while. */
    static int put(Score &score, int bar, const QList<int> &pitches, const Rational &length,
                   bool muted = false)
    {
        const int rhythm = int(score.rhythms.size());
        score.rhythms.insert(rhythm, length);
        Beat beat;
        beat.rhythm = rhythm;
        for (const int midi : pitches) {
            const int noteId = int(score.notes.size());
            Note note;
            note.midi = midi;
            note.muted = muted;
            score.notes.insert(noteId, note);
            beat.notes.append(noteId);
        }
        const int beatId = int(score.beats.size());
        score.beats.insert(beatId, beat);
        score.voices[bar].beats.append(beatId);
        return beatId;
    }

    /**
     * A chord to a bar, each held a semibreve -- a progression rather than a
     * scale.
     *
     * Real music, deliberately. A run of the scale with the tonic held down
     * for four times as long as anything else is not what a piece in a key
     * looks like: it is a spike, it correlates with whatever profile is
     * spikiest, and a test built on one measures the fixture rather than the
     * method.
     */
    static void progression(Score &score, const QList<QList<int>> &chords)
    {
        for (int bar = 0; bar < chords.size(); ++bar) {
            put(score, bar % int(score.masterBars.size()), chords.at(bar), Rational(4));
        }
    }

    static QString written(const Analysis::Fit &fit)
    {
        return Key::nameOf(fit.key);
    }

private Q_SLOTS:
    void howLongANoteSoundsIsHowMuchItCounts()
    {
        Score score = empty(1);
        put(score, 0, {60}, Rational(4));       // one semibreve of C
        put(score, 0, {61}, Rational(1, 4));    // one semiquaver of C sharp

        const Analysis::Weights weights = Analysis::weigh(score);
        QCOMPARE(weights[0], 4.0);
        QCOMPARE(weights[1], 0.25);
        // Counted as notes rather than as time, those two would be equal, and
        // a key would be decidable by ornament.
        QVERIFY(weights[0] > weights[1] * 10);
    }

    void whatHasNoPitchIsNotCounted()
    {
        Score score = empty(1);
        put(score, 0, {60}, Rational(1));
        put(score, 0, {61}, Rational(4), true);     // a dead note: a click

        const Analysis::Weights weights = Analysis::weigh(score);
        QCOMPARE(weights[0], 1.0);
        QCOMPARE(weights[1], 0.0);
    }

    void aDrumKitIsNotInAKey()
    {
        Score kit = empty(1, true);
        put(kit, 0, {38, 42, 36}, Rational(1));
        QVERIFY(Analysis::isSilent(Analysis::weigh(kit)));
    }

    void silenceHasNoOpinion()
    {
        const Score score = empty(4);
        const Analysis::Weights weights = Analysis::weigh(score);
        QVERIFY(Analysis::isSilent(weights));
        // Not "C major", which is what sorting twenty-four equal answers would
        // have produced -- a fit of nothing is how a caller knows to say
        // nothing.
        QCOMPARE(Analysis::best(weights).fit, 0.0);
    }

    void theSameNotesLeanedOnDifferentlyAreDifferentKeys()
    {
        // I-V-vi-IV in C, which is most of the popular music ever written.
        Score major = empty(4);
        progression(major, {{60, 64, 67}, {67, 71, 62}, {69, 60, 64}, {65, 69, 60}});
        const QList<Analysis::Fit> majorReadings = Analysis::ranked(Analysis::weigh(major));
        QCOMPARE(written(majorReadings.constFirst()), QStringLiteral("C major"));

        // i-iv-v-i in A, which is the same seven pitch classes and nothing
        // else -- only the weight has moved.
        Score minor = empty(4);
        progression(minor, {{69, 60, 64}, {62, 65, 69}, {64, 67, 71}, {69, 60, 64}});
        const QList<Analysis::Fit> minorReadings = Analysis::ranked(Analysis::weigh(minor));
        QCOMPARE(written(minorReadings.constFirst()), QStringLiteral("A minor"));

        // The signature is the same in both and falls straight out of the
        // pitch content. The mode is the judgement, and the honest sign of
        // that is what comes second: each passage's runner-up is the other
        // one's answer, on the same accidentals.
        QCOMPARE(majorReadings.constFirst().key.accidentals, 0);
        QCOMPARE(minorReadings.constFirst().key.accidentals, 0);
        QCOMPARE(written(majorReadings.at(1)), QStringLiteral("A minor"));
        QCOMPARE(written(minorReadings.at(1)), QStringLiteral("C major"));
    }

    void aKeyWithAccidentalsInItIsFound()
    {
        // The same four chords in E: four sharps.
        Score score = empty(4);
        progression(score, {{64, 68, 71}, {71, 75, 78}, {73, 76, 80}, {69, 73, 76}});

        const Analysis::Fit fit = Analysis::best(Analysis::weigh(score));
        QCOMPARE(written(fit), QStringLiteral("E major"));
        QCOMPARE(fit.key.accidentals, 4);
    }

    /**
     * Move the music and the answer moves with it, by the same amount, twelve
     * times over. This is the one that exercises the rotation and both
     * signature tables together: an off-by-one anywhere in either shows up
     * here as a key a semitone out, and nowhere else as anything at all.
     */
    void aKeyMovesWithTheMusic()
    {
        const QList<QList<int>> fourChords = {{60, 64, 67}, {67, 71, 62}, {69, 60, 64}, {65, 69, 60}};
        for (int shift = 0; shift < 12; ++shift) {
            QList<QList<int>> moved;
            for (const QList<int> &chord : fourChords) {
                QList<int> here;
                for (const int midi : chord) {
                    here.append(midi + shift);
                }
                moved.append(here);
            }
            Score score = empty(4);
            progression(score, moved);

            const Key::Signature found = Analysis::best(Analysis::weigh(score)).key;
            const int tonic = Key::midiOf(Key::tonicOf(found)) % 12;
            if (tonic != shift % 12 || found.minor) {
                QFAIL(qPrintable(QStringLiteral("C major moved %1 semitones read as %2")
                                     .arg(shift)
                                     .arg(Key::nameOf(found))));
            }
        }
    }

    void everythingCountedIsANoteAndNotADrum()
    {
        Score score = empty(1);
        put(score, 0, {60, 64}, Rational(1));
        put(score, 0, {61}, Rational(1), true);     // dead
        QCOMPARE(Analysis::pitched(score).size(), 2);

        // What is outside the key is drawn from the same population, so it can
        // never name a note that was not counted in the first place.
        const QList<int> strangers = Analysis::outside(score, Key::Signature{});
        for (const int noteId : strangers) {
            QVERIFY(Analysis::pitched(score).contains(noteId));
        }
    }

    void sizeDoesNotChangeTheAnswer()
    {
        // A correlation compares shapes rather than sizes, so four bars and
        // four hundred of the same music read the same.
        const QList<QList<int>> fourChords = {{60, 64, 67}, {67, 71, 62}, {69, 60, 64}, {65, 69, 60}};
        Score small = empty(1);
        progression(small, fourChords);
        Score large = empty(1);
        for (int repeat = 0; repeat < 50; ++repeat) {
            progression(large, fourChords);
        }
        const Analysis::Fit one = Analysis::best(Analysis::weigh(small));
        const Analysis::Fit many = Analysis::best(Analysis::weigh(large));
        QCOMPARE(written(one), written(many));
        QVERIFY(qAbs(one.fit - many.fit) < 1e-9);
    }

    void aPassageIsOnlyWhatIsInsideIt()
    {
        Score score = empty(3);
        put(score, 0, {60}, Rational(1));
        put(score, 1, {66}, Rational(1));
        put(score, 2, {67}, Rational(1));

        Analysis::Passage middle;
        middle.firstBar = 1;
        middle.lastBar = 1;
        middle.firstBeat = 0;
        middle.lastBeat = 0;

        const Analysis::Weights weights = Analysis::weigh(score, middle);
        QCOMPARE(weights[6], 1.0);
        QCOMPARE(weights[0], 0.0);
        QCOMPARE(weights[7], 0.0);
    }

    void theNotesOutsideTheKeyAreCountedAndNothingElse()
    {
        Score score = empty(1);
        put(score, 0, {60}, Rational(1));       // C, in C major
        put(score, 0, {66}, Rational(1));       // F sharp, not
        put(score, 0, {67}, Rational(1));       // G, in

        const QList<int> strangers = Analysis::outside(score, Key::Signature{});
        QCOMPARE(strangers.size(), 1);
        QCOMPARE(score.notes.value(strangers.first()).midi, 66);

        // In a key that has it, the same note is not a stranger -- which is
        // the point of asking the question of a key rather than of a note. G
        // major holds all three of these.
        QVERIFY(Analysis::outside(score, Key::Signature{1, false}).isEmpty());
    }

    /**
     * The two tables are written out by hand and neither derives the other, so
     * the useful check is that they agree: the signature this picks for a
     * tonic has to be a signature named after that tonic.
     */
    void everySignatureIsNamedAfterTheTonicItWasChosenFor()
    {
        for (int tonic = 0; tonic < 12; ++tonic) {
            for (const bool minor : {false, true}) {
                const Key::Signature signature = Key::signatureFor(tonic, minor);
                const Key::Spelling named = Key::tonicOf(signature);
                if (Key::midiOf(named) % 12 != tonic) {
                    QFAIL(qPrintable(QStringLiteral("pitch class %1 %2 chose %3, named after %4")
                                         .arg(tonic)
                                         .arg(minor ? QStringLiteral("minor") : QStringLiteral("major"),
                                              Key::nameOf(signature), Key::nameOf(named))));
                }
                QCOMPARE(signature.minor, minor);
            }
        }
    }

    void whereTwoSpellingsAreEqualItPicksTheOneAMusicianWould()
    {
        // Six accidentals either way, and these are the ones people write.
        QCOMPARE(Key::nameOf(Key::signatureFor(1, false)), QStringLiteral("D♭ major"));
        QCOMPARE(Key::nameOf(Key::signatureFor(6, false)), QStringLiteral("F♯ major"));
        QCOMPARE(Key::nameOf(Key::signatureFor(3, true)), QStringLiteral("E♭ minor"));
        QCOMPARE(Key::nameOf(Key::signatureFor(8, true)), QStringLiteral("G♯ minor"));
    }
};

QTEST_GUILESS_MAIN(AnalysisTest)
#include "analysistest.moc"
