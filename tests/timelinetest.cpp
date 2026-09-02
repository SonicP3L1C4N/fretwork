// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "gpif.h"
#include "timeline.h"

#include <QDir>
#include <QTest>

/**
 * The pass between the document and the sound, built here from scores made in
 * code rather than read from files -- the two questions are separate and their
 * tests should be too.
 */
class TimelineTest : public QObject
{
    Q_OBJECT

private:
    /** A guitar, six strings, and as many empty 4/4 bars as asked for. */
    static Score blank(int bars, int strings = 6)
    {
        Score score;
        Track guitar;
        guitar.name = QStringLiteral("Guitar");
        guitar.instrumentType = QStringLiteral("electricGuitar");
        for (int string = 0; string < strings; ++string) {
            guitar.tuning.append(40 + string * 5);
        }
        score.tracks.append(guitar);

        for (int index = 0; index < bars; ++index) {
            MasterBar bar;
            bar.bars = {index};
            score.masterBars.append(bar);
            score.bars.insert(index, Bar{{-1, -1, -1, -1}});
        }
        score.rhythms.insert(0, Rational(1));   // a quarter
        return score;
    }

    /** Puts one voice of `beats` into a bar, each beat holding one note. */
    static void fill(Score &score, int barIndex, const QList<Note> &notes)
    {
        const int voiceId = barIndex;
        QList<int> beatIds;
        for (int index = 0; index < notes.size(); ++index) {
            const int id = barIndex * 100 + index;
            score.notes.insert(id, notes.at(index));
            score.beats.insert(id, Beat{0, {id}, Dynamic::F, false, false});
            beatIds.append(id);
        }
        score.voices.insert(voiceId, Voice{beatIds});
        score.bars[barIndex] = Bar{{voiceId, -1, -1, -1}};
    }

    static Note at(int midi, int string)
    {
        Note note;
        note.midi = midi;
        note.string = string;
        return note;
    }

    /** The same, with the fret said out loud, which is what a shift is measured in. */
    static Note fretted(int string, int fret)
    {
        Note note;
        note.string = string;
        note.fret = fret;
        note.midi = 40 + string * 5 + fret;
        return note;
    }

    /** Emily's noises, as this program discovers them from the library. */
    static Noises::Map emily()
    {
        Noises::Map map;
        map.fingering = {90};
        map.muted = {91, 92, 93, 94, 95};
        map.pickRest = {96};
        return map;
    }

    /** The key of the first thing this part is told to play. */
    static int firstNoteOn(const QList<Timeline::Message> &messages)
    {
        for (const Timeline::Message &message : messages) {
            if (message.kind == Timeline::MessageKind::NoteOn) {
                return message.data1;
            }
        }
        return -1;
    }

    static QList<int> keysOf(const QList<Timeline::NoteEvent> &events)
    {
        QList<int> keys;
        for (const Timeline::NoteEvent &event : events) {
            keys.append(event.pitch);
        }
        return keys;
    }

private Q_SLOTS:
    // ---- repeats ----

    void aScoreWithoutRepeatsIsPlayedAsWritten()
    {
        const Score score = blank(4);
        QCOMPARE(Timeline::playedOrder(score), QList<int>({0, 1, 2, 3}));
    }

    void aRepeatedSectionIsPlayedAsManyTimesAsItSays()
    {
        Score score = blank(4);
        score.masterBars[1].repeatStart = true;
        score.masterBars[2].repeatEnd = true;
        score.masterBars[2].repeatCount = 2;
        QCOMPARE(Timeline::playedOrder(score), QList<int>({0, 1, 2, 1, 2, 3}));
    }

    void aRepeatWithoutAStartGoesBackToTheBeginning()
    {
        Score score = blank(3);
        score.masterBars[1].repeatEnd = true;
        score.masterBars[1].repeatCount = 2;
        QCOMPARE(Timeline::playedOrder(score), QList<int>({0, 1, 0, 1, 2}));
    }

    void theScoreCanStillBeReadAsNotated()
    {
        Score score = blank(3);
        score.masterBars[0].repeatStart = true;
        score.masterBars[1].repeatEnd = true;
        score.masterBars[1].repeatCount = 4;
        QCOMPARE(Timeline::playedOrder(score, false), QList<int>({0, 1, 2}));
    }

    /** Approximate is fine; approximate and silent is not. */
    void alternateEndingsAreReportedRatherThanQuietlyPlayedWrongly()
    {
        Score score = blank(2);
        QVERIFY(!Timeline::hasAlternateEndings(score));
        score.masterBars[1].alternateEndings = true;
        QVERIFY(Timeline::hasAlternateEndings(score));
    }

    // ---- notes ----

    /**
     * Vibrato is played as what it is: a wobble of the pitch.
     *
     * It reuses the bend path rather than inventing one, so what is asserted
     * is that the curve exists, that it goes both ways, and that it starts and
     * ends where the note is written -- a vibrato that left the string sharp
     * would be a vibrato that retuned the guitar.
     */
    void vibratoWobblesThePitchAndPutsItBack()
    {
        Score score = blank(1);
        score.tempos.append({0, 0, 120});
        Note note;
        note.midi = 64;
        note.string = 5;
        note.vibrato = true;
        fill(score, 0, {note});

        const QList<int> order = Timeline::playedOrder(score);
        const QList<Timeline::NoteEvent> notes = Timeline::notesFor(score, 0, order);
        QCOMPARE(notes.size(), 1);

        const QList<Timeline::BendPoint> curve = notes.first().bend;
        QVERIFY2(!curve.isEmpty(), "a vibrato note has no pitch movement at all");
        QCOMPARE(curve.first().cents, 0);
        QCOMPARE(curve.last().cents, 0);

        int highest = 0;
        int lowest = 0;
        for (const Timeline::BendPoint &point : curve) {
            highest = std::max(highest, point.cents);
            lowest = std::min(lowest, point.cents);
        }
        QVERIFY2(highest > 0 && lowest < 0, "a vibrato that only goes one way is a bend");
        // A wrist and not a whammy bar: well under a semitone either side.
        QVERIFY(highest < 100 && lowest > -100);
    }

    /**
     * A slide that connects two notes arrives at the second one's pitch.
     *
     * This is the only slide gpif gives a destination for: the hand ends up on
     * the next note on that string, so the glide has somewhere real to go and
     * nothing has to be invented. Two frets down is two hundred cents down,
     * and the curve has to *end* there rather than pass through it.
     */
    void aConnectingSlideGlidesToTheNextNote()
    {
        Score score = blank(1);
        score.tempos.append({0, 0, 120});
        Note first = fretted(2, 5);
        first.slide = SlideType::Shift;
        fill(score, 0, {first, fretted(2, 3)});

        const QList<int> order = Timeline::playedOrder(score);
        const QList<Timeline::NoteEvent> notes = Timeline::notesFor(score, 0, order);
        QCOMPARE(notes.size(), 2);

        const QList<Timeline::BendPoint> curve = notes.first().bend;
        QVERIFY2(!curve.isEmpty(), "a slide with a destination does not move at all");
        QCOMPARE(curve.first().cents, 0);
        QCOMPARE(curve.last().cents, -200);

        // The hand moves at the end of the note, not across the whole of it:
        // the pitch is still where it was written for most of the duration.
        const Rational duration = notes.first().end - notes.first().start;
        QVERIFY2(duration * Rational(1, 2) < curve.first().at,
                 "the glide starts before the note is half over");
        QCOMPARE(curve.last().at, duration);
    }

    /** Upwards, to prove the direction is read off the notes and not assumed. */
    void aConnectingSlideGoesUpWhenTheNextNoteIsHigher()
    {
        Score score = blank(1);
        score.tempos.append({0, 0, 120});
        Note first = fretted(2, 3);
        first.slide = SlideType::Legato;
        fill(score, 0, {first, fretted(2, 8)});

        const QList<int> order = Timeline::playedOrder(score);
        const QList<Timeline::NoteEvent> notes = Timeline::notesFor(score, 0, order);
        QCOMPARE(notes.first().bend.last().cents, 500);
    }

    /**
     * A slide out of a note travels a fixed distance, because the file never
     * says how far. A slide into one starts away and arrives.
     *
     * The numbers are invented -- that is stated where they are defined -- so
     * what is worth testing is the shape rather than the size: which end of
     * the note moves, and which way.
     */
    void anUnwrittenSlideSweepsOffTheNoteOrOntoIt()
    {
        const auto curveFor = [](SlideType slide) {
            Score score = blank(1);
            score.tempos.append({0, 0, 120});
            Note note = fretted(2, 7);
            note.slide = slide;
            fill(score, 0, {note});
            const QList<int> order = Timeline::playedOrder(score);
            return Timeline::notesFor(score, 0, order).first().bend;
        };

        // Out: holds the written pitch, then leaves it.
        const QList<Timeline::BendPoint> down = curveFor(SlideType::OutDown);
        QCOMPARE(down.first().cents, 0);
        QVERIFY(down.last().cents < 0);
        QVERIFY(Rational(0) < down.first().at);

        QVERIFY(curveFor(SlideType::OutUp).last().cents > 0);

        // In: starts away from the written pitch and arrives on it.
        const QList<Timeline::BendPoint> below = curveFor(SlideType::InFromBelow);
        QCOMPARE(below.first().at, Rational(0));
        QVERIFY(below.first().cents < 0);
        QCOMPARE(below.last().cents, 0);

        QVERIFY(curveFor(SlideType::InFromAbove).first().cents > 0);

        // A pick scrape is a pitch sweep along the string, so it behaves like
        // a slide out -- which is also where gpif keeps it.
        QVERIFY(curveFor(SlideType::PickScrapeDown).last().cents < 0);
    }

    /**
     * A slide with nothing to slide to is left alone.
     *
     * gpif would more usually have called that a slide out. Inventing a
     * direction for it here would be answering a question the file did not
     * ask, and a note that quietly wanders off pitch at the end of a phrase is
     * hard to trace back to a line of code.
     */
    void aSlideWithNoNextNoteOnTheStringDoesNotMove()
    {
        Score score = blank(1);
        score.tempos.append({0, 0, 120});
        Note only = fretted(2, 5);
        only.slide = SlideType::Legato;
        // The next note is on a different string, so the hand did not slide.
        fill(score, 0, {only, fretted(4, 3)});

        const QList<int> order = Timeline::playedOrder(score);
        const QList<Timeline::NoteEvent> notes = Timeline::notesFor(score, 0, order);
        QVERIFY(notes.first().bend.isEmpty());
    }

    /**
     * A written bend beats a slide, the same way it beats a vibrato: they are
     * one curve on one channel, and two gestures at once is a third shape
     * nobody has designed.
     */
    void aWrittenBendBeatsASlide()
    {
        Score score = blank(1);
        score.tempos.append({0, 0, 120});
        Note first = fretted(2, 5);
        first.slide = SlideType::Shift;
        first.bended = true;
        first.bendDestinationValue = 200;
        fill(score, 0, {first, fretted(2, 3)});

        const QList<int> order = Timeline::playedOrder(score);
        const QList<Timeline::NoteEvent> notes = Timeline::notesFor(score, 0, order);
        // Up a whole tone, which is the bend. The slide would have gone down.
        QCOMPARE(notes.first().bend.last().cents, 200);
    }

    /**
     * A note too short to hold a cycle gets none.
     *
     * One lurch of pitch on a semiquaver is not vibrato, and playing it as one
     * makes fast passages sound out of tune rather than expressive.
     */
    void aNoteTooShortToVibrateDoesNot()
    {
        Score score = blank(1);
        // Fast enough that a crotchet is well under a fifth of a second.
        score.tempos.append({0, 0, 400});
        Note note;
        note.midi = 64;
        note.string = 5;
        note.vibrato = true;
        fill(score, 0, {note});

        const QList<Timeline::NoteEvent> notes =
            Timeline::notesFor(score, 0, Timeline::playedOrder(score));
        QCOMPARE(notes.size(), 1);
        QVERIFY(notes.first().bend.isEmpty());
    }

    /** A written bend is not replaced by a wobble. */
    void aBentNoteKeepsItsBend()
    {
        Score score = blank(1);
        score.tempos.append({0, 0, 120});
        Note note;
        note.midi = 64;
        note.string = 5;
        note.vibrato = true;
        note.bended = true;
        note.bendOriginValue = 0;
        note.bendDestinationValue = 200;
        fill(score, 0, {note});

        const QList<Timeline::NoteEvent> notes =
            Timeline::notesFor(score, 0, Timeline::playedOrder(score));
        const QList<Timeline::BendPoint> curve = notes.first().bend;
        QVERIFY(!curve.isEmpty());
        // The written bend arrives a whole tone up, which no vibrato does.
        QCOMPARE(curve.last().cents, 200);
    }

    /**
     * And on a real transcription, where the vibratos were put there by a
     * person rather than by this test.
     *
     * Gated on the corpus like the importer's own checks: transcriptions are
     * not ours to commit. What it asserts is the whole path -- a `.gp` read,
     * the flag imported, and a curve on the far side of it.
     */
    void realScoresVibrateWhereTheySayTheyDo()
    {
        const QString corpus = qEnvironmentVariable("FRETWORK_CORPUS");
        if (corpus.isEmpty()) {
            QSKIP("set FRETWORK_CORPUS to a directory of .gp files to run this");
        }
        int marked = 0;
        int wobbling = 0;
        for (const QString &name :
             QDir(corpus).entryList({QStringLiteral("*.gp")}, QDir::Files)) {
            const Score score = Gpif::read(QDir(corpus).filePath(name));
            for (auto note = score.notes.constBegin(); note != score.notes.constEnd(); ++note) {
                marked += note->vibrato ? 1 : 0;
            }
            const QList<int> order = Timeline::playedOrder(score);
            for (int track = 0; track < score.tracks.size(); ++track) {
                for (const Timeline::NoteEvent &note : Timeline::notesFor(score, track, order)) {
                    // Counted by shape rather than by "has a curve", or a
                    // score full of written bends would pass this without a
                    // single vibrato being played: a bend goes one way and
                    // stays there, and only a wobble goes both.
                    bool up = false;
                    bool down = false;
                    for (const Timeline::BendPoint &point : note.bend) {
                        up = up || point.cents > 0;
                        down = down || point.cents < 0;
                    }
                    wobbling += up && down ? 1 : 0;
                }
            }
        }
        QVERIFY2(marked > 0, "no transcription in the corpus has a vibrato in it");
        QVERIFY2(wobbling > 0, "the corpus has vibratos written in it and none of them move");
    }

    /**
     * A tremolo is picked again and again, and each strike is a note.
     *
     * Which is why it cannot be a curve the way vibrato is: what changes is
     * not the pitch but how many times the string is hit.
     */
    void aTremoloIsPickedAgainAndAgain()
    {
        Score score = blank(1);
        score.tempos.append({0, 0, 120});
        Note note;
        note.midi = 64;
        note.string = 5;
        fill(score, 0, {note});
        // A crotchet, repicked in quavers: two strikes.
        for (auto beat = score.beats.begin(); beat != score.beats.end(); ++beat) {
            beat->tremolo = true;
            // A quaver, in quarters.
            beat->tremoloValue = Rational(1, 2);
        }

        const QList<Timeline::NoteEvent> notes =
            Timeline::notesFor(score, 0, Timeline::playedOrder(score));
        QCOMPARE(notes.size(), 2);
        QCOMPARE(notes.at(0).pitch, 64);
        QCOMPARE(notes.at(1).pitch, 64);
        // Each strike begins where the last ended, and the run fills the beat
        // rather than overrunning it.
        QVERIFY(notes.at(0).end == notes.at(1).start);
        QVERIFY(notes.at(1).end == Rational(1));
        QVERIFY(notes.first().tremolo);
    }

    /** How fast it is picked is what the file said, not a guess. */
    void howFastItIsPickedIsWhatWasWritten()
    {
        Score score = blank(1);
        score.tempos.append({0, 0, 120});
        Note note;
        note.midi = 64;
        note.string = 5;
        fill(score, 0, {note});
        for (auto beat = score.beats.begin(); beat != score.beats.end(); ++beat) {
            beat->tremolo = true;
            // A semiquaver: four to a crotchet.
            beat->tremoloValue = Rational(1, 4);
        }
        QCOMPARE(Timeline::notesFor(score, 0, Timeline::playedOrder(score)).size(), 4);
    }

    /**
     * A tremolo no faster than the note it is on is one strike.
     *
     * Not nought, and not a strike hanging off the end: a quaver marked as
     * repicked in quavers is a quaver.
     */
    void aTremoloNoFasterThanTheNoteIsJustTheNote()
    {
        Score score = blank(1);
        score.tempos.append({0, 0, 120});
        Note note;
        note.midi = 64;
        note.string = 5;
        fill(score, 0, {note});
        for (auto beat = score.beats.begin(); beat != score.beats.end(); ++beat) {
            beat->tremolo = true;
            // A minim, which is longer than the crotchet it is written on.
            beat->tremoloValue = Rational(2);
        }
        const QList<Timeline::NoteEvent> notes =
            Timeline::notesFor(score, 0, Timeline::playedOrder(score));
        QCOMPARE(notes.size(), 1);
        QVERIFY(notes.first().end == Rational(1));
    }

    void everyStringGetsAChannelOfItsOwn()
    {
        Score score = blank(1);
        fill(score, 0, {at(40, 0), at(64, 5), at(50, 2)});

        const QList<Timeline::NoteEvent> notes =
            Timeline::notesFor(score, 0, Timeline::playedOrder(score));
        QCOMPARE(notes.size(), 3);
        for (const Timeline::NoteEvent &note : notes) {
            QCOMPARE(note.channel, note.string);
        }
    }

    /**
     * Pitch bend is per channel. Six strings sharing one would bend the chord
     * under the note being bent, which is the failure this whole arrangement
     * exists to avoid -- so it is asserted rather than assumed.
     */
    void twoNotesOnDifferentStringsNeverShareAChannel()
    {
        Score score = blank(1);
        fill(score, 0, {at(40, 0), at(45, 1)});

        const QList<Timeline::NoteEvent> notes =
            Timeline::notesFor(score, 0, Timeline::playedOrder(score));
        QCOMPARE(notes.size(), 2);
        QVERIFY(notes.at(0).channel != notes.at(1).channel);
    }

    void percussionHasNoStringsAndOneChannel()
    {
        Score score = blank(1, 0);
        score.tracks[0].instrumentType = QStringLiteral("drumKit");
        fill(score, 0, {at(36, -1), at(38, -1)});

        const QList<Timeline::NoteEvent> notes =
            Timeline::notesFor(score, 0, Timeline::playedOrder(score));
        QCOMPARE(notes.size(), 2);
        QCOMPARE(notes.at(0).channel, 0);
        QCOMPARE(notes.at(1).channel, 0);
    }

    /** The most audible mistake available: a tie that restrikes. */
    void aTieLengthensTheNoteRatherThanPlayingItAgain()
    {
        Score score = blank(2);
        Note held = at(64, 5);
        held.tieOrigin = true;
        Note continues = at(64, 5);
        continues.tieDestination = true;

        fill(score, 0, {held});
        fill(score, 1, {continues});
        // The first bar's single quarter is followed by three of silence, so
        // the tie in bar two starts four quarters in -- not one.
        score.rhythms.insert(1, Rational(4));
        score.beats[0] = Beat{1, {0}, Dynamic::F, false, false};
        score.beats[100] = Beat{1, {100}, Dynamic::F, false, false};

        const QList<Timeline::NoteEvent> notes =
            Timeline::notesFor(score, 0, Timeline::playedOrder(score));
        QCOMPARE(notes.size(), 1);
        QCOMPARE(notes.at(0).start, Rational(0));
        QCOMPARE(notes.at(0).end, Rational(8));
    }

    void aDeadNoteIsShortenedAndADeadenedOneHalved()
    {
        Score score = blank(1);
        Note dead = at(40, 0);
        dead.muted = true;
        Note palm = at(45, 1);
        palm.palmMuted = true;
        fill(score, 0, {dead, palm});

        const QList<Timeline::NoteEvent> notes =
            Timeline::notesFor(score, 0, Timeline::playedOrder(score));
        QCOMPARE(notes.at(0).end + Rational(-1, 1) < Rational(0), true);
        QCOMPARE(notes.at(1).end, Rational(1) + Rational(1, 2));
    }

    void accentsAndGhostNotesMoveTheVelocity()
    {
        Score score = blank(1);
        Note loud = at(40, 0);
        loud.accent = true;
        Note quiet = at(45, 1);
        quiet.ghost = true;
        fill(score, 0, {at(50, 2), loud, quiet});

        const QList<Timeline::NoteEvent> notes =
            Timeline::notesFor(score, 0, Timeline::playedOrder(score));
        const int plain = notes.at(0).velocity;
        QVERIFY(notes.at(1).velocity > plain);
        QVERIFY(notes.at(2).velocity < plain);
        for (const Timeline::NoteEvent &note : notes) {
            QVERIFY(note.velocity >= 1 && note.velocity <= 127);
        }
    }

    // ---- time ----

    void barsOfDifferentLengthsAddUp()
    {
        Score score = blank(3);
        score.masterBars[1].numerator = 3;
        score.masterBars[2].denominator = 8;
        score.masterBars[2].numerator = 6;
        // 4/4 + 3/4 + 6/8 == 4 + 3 + 3 quarters.
        QCOMPARE(Timeline::length(score, Timeline::playedOrder(score)), Rational(10));
    }

    void theClockRunsBothWays()
    {
        Score score = blank(4);
        score.tempos.append({0, 0, 120});
        score.tempos.append({2, 0, 60});
        const QList<int> order = Timeline::playedOrder(score);
        const Timeline::Clock clock(score, order);

        // Either side of the change, on it, and past it: what the clock says
        // a quarter is in seconds, it reads back as the same quarter.
        for (const Rational &quarters : {Rational(0), Rational(3, 2), Rational(8), Rational(9),
                                         Rational(13, 2), Rational(15)}) {
            QVERIFY2(std::abs(clock.quartersAt(clock.secondsAt(quarters)) - quarters.toDouble()) < 1e-9,
                     qPrintable(QString::number(quarters.toDouble())));
        }
        // Two bars at 120 is four seconds; a quarter more at 60 is a second.
        QCOMPARE(clock.quartersAt(5.0), 9.0);
        QCOMPARE(clock.quartersAt(-1.0), 0.0);
    }

    void aPassIsFoundFromQuartersAsWellAsSeconds()
    {
        Score score = blank(3);
        score.masterBars[1].numerator = 3;
        const QList<int> order = Timeline::playedOrder(score);
        QCOMPARE(Timeline::passAt(score, order, Rational(0)), 0);
        QCOMPARE(Timeline::passAt(score, order, Rational(4)), 1);
        QCOMPARE(Timeline::passAt(score, order, Rational(7)), 2);
        QCOMPARE(Timeline::passAt(score, order, Rational(11)), -1);
        QCOMPARE(Timeline::passAt(score, order, Rational(-1)), -1);
    }

    void aTempoInsideARepeatTakesEffectTheFirstTimeThrough()
    {
        Score score = blank(3);
        score.masterBars[1].repeatStart = true;
        score.masterBars[1].repeatEnd = true;
        score.masterBars[1].repeatCount = 2;
        score.tempos.append({0, 0, 120});
        score.tempos.append({1, 0, 60});

        const QList<int> order = Timeline::playedOrder(score);
        QCOMPARE(order, QList<int>({0, 1, 1, 2}));

        const QList<Timeline::TempoEvent> tempos = Timeline::tempoMap(score, order);
        QCOMPARE(tempos.size(), 2);
        QCOMPARE(tempos.at(1).at, Rational(4));      // where bar 2 first falls
        QCOMPARE(tempos.at(1).quarterBpm, 60.0);

        // Four quarters at 120, then twelve at 60.
        QCOMPARE(Timeline::seconds(score, order), 2.0 + 12.0);
    }

    void aScoreWithNoTempoIsPlayedAtOneHundredAndTwenty()
    {
        const Score score = blank(2);
        const QList<int> order = Timeline::playedOrder(score);
        QCOMPARE(Timeline::tempoMap(score, order).size(), 1);
        QCOMPARE(Timeline::seconds(score, order), 4.0);
    }

    /** Where a bar starts and which bar is sounding are the same question. */
    void whereABarStartsIsWhereTheBarBeforeItEnded()
    {
        Score score = blank(4);
        score.tempos.append({0, 0, 120});
        score.tempos.append({2, 0, 60});     // half speed from the third bar

        const QList<int> order = Timeline::playedOrder(score);
        const Timeline::Clock clock(score, order);

        QCOMPARE(Timeline::secondsAtPass(score, order, clock, 0), 0.0);
        QCOMPARE(Timeline::secondsAtPass(score, order, clock, 1), 2.0);
        QCOMPARE(Timeline::secondsAtPass(score, order, clock, 2), 4.0);
        // Four quarters at sixty rather than at a hundred and twenty.
        QCOMPARE(Timeline::secondsAtPass(score, order, clock, 3), 8.0);

        // And the two directions agree: asking which bar is sounding at the
        // moment a bar starts gives that bar back.
        for (int pass = 0; pass < order.size(); ++pass) {
            const double at = Timeline::secondsAtPass(score, order, clock, pass);
            QCOMPARE(Timeline::barAt(score, order, clock, at), pass);
        }
    }

    /** A bar inside a repeat starts more than once, and this is asked by pass. */
    void aRepeatedBarStartsOncePerPass()
    {
        Score score = blank(2);
        score.masterBars[1].repeatEnd = true;
        score.masterBars[1].repeatCount = 2;

        const QList<int> order = Timeline::playedOrder(score);
        QCOMPARE(order.size(), 4);
        const Timeline::Clock clock(score, order);

        // The same notated bar, two different moments.
        QCOMPARE(order.at(1), order.at(3));
        QCOMPARE(Timeline::secondsAtPass(score, order, clock, 1), 2.0);
        QCOMPARE(Timeline::secondsAtPass(score, order, clock, 3), 6.0);
    }

    // ---- the click ----

    /** Where the clicks fall, and how hard each is struck. */
    static QList<QPair<Rational, int>> clicksOf(const Score &score, const QList<int> &order)
    {
        QList<QPair<Rational, int>> out;
        for (const Timeline::Message &message : Timeline::clickFor(score, order)) {
            if (message.kind == Timeline::MessageKind::NoteOn) {
                // The velocity, not the pitch: the accent is a lean on one
                // sound rather than a second sound.
                out.append({message.at, message.data2});
            }
        }
        return out;
    }

    void clicksOnEveryBeatAndLeansOnTheFirst()
    {
        const Score score = blank(2);
        const QList<QPair<Rational, int>> clicks = clicksOf(score, {0, 1});
        QCOMPARE(clicks.size(), 8);
        for (int index = 0; index < clicks.size(); ++index) {
            QCOMPARE(clicks.at(index).first, Rational(index));
        }
        // The accent is the first beat of every bar and not only of the piece:
        // a bar line you cannot hear is a bar line that is no use to count by.
        QCOMPARE(clicks.at(0).second, clicks.at(4).second);
        QVERIFY(clicks.at(0).second != clicks.at(1).second);
    }

    void countsCompoundTimeInDottedBeats()
    {
        // 6/8 is two beats of three quavers, not six of one. Everybody who
        // plays a jig knows this and no denominator says it.
        Score score = blank(1);
        score.masterBars[0].numerator = 6;
        score.masterBars[0].denominator = 8;
        QCOMPARE(Timeline::beatOf(score.masterBars.at(0)), Rational(3, 2));
        QCOMPARE(clicksOf(score, {0}).size(), 2);

        score.masterBars[0].numerator = 12;
        QCOMPARE(clicksOf(score, {0}).size(), 4);

        score.masterBars[0].numerator = 9;
        QCOMPARE(clicksOf(score, {0}).size(), 3);
    }

    void countsEverythingElseByItsDenominator()
    {
        Score score = blank(1);
        // 3/8 has a numerator of three rather than a multiple of it, and is
        // counted in quavers by everybody who plays it.
        score.masterBars[0].numerator = 3;
        score.masterBars[0].denominator = 8;
        QCOMPARE(Timeline::beatOf(score.masterBars.at(0)), Rational(1, 2));
        QCOMPARE(clicksOf(score, {0}).size(), 3);

        // 7/8 does not divide into threes at all.
        score.masterBars[0].numerator = 7;
        QCOMPARE(clicksOf(score, {0}).size(), 7);

        score.masterBars[0].numerator = 3;
        score.masterBars[0].denominator = 4;
        QCOMPARE(clicksOf(score, {0}).size(), 3);
    }

    void clicksThroughARepeatAsManyTimesAsItIsPlayed()
    {
        Score score = blank(2);
        score.masterBars[0].repeatStart = true;
        score.masterBars[1].repeatEnd = true;
        score.masterBars[1].repeatCount = 2;

        const QList<int> order = Timeline::playedOrder(score);
        QCOMPARE(order.size(), 4);
        // Four bars heard is four bars counted, however many are written.
        QCOMPARE(clicksOf(score, order).size(), 16);
        QCOMPARE(clicksOf(score, order).last().first, Rational(15));
    }

    void countsABarByItsOwnLengthAndNotByTheOneBefore()
    {
        // A pickup bar is shorter than the bars after it, and a click that
        // carried its beat across the bar line would put every beat in the
        // piece a quaver out from there on.
        Score score = blank(2);
        score.masterBars[0].numerator = 1;
        score.masterBars[0].denominator = 4;

        const QList<QPair<Rational, int>> clicks = clicksOf(score, {0, 1});
        QCOMPARE(clicks.size(), 5);
        QCOMPARE(clicks.at(1).first, Rational(1));
        QCOMPARE(clicks.at(1).second, clicks.at(0).second);   // both are first beats
        QCOMPARE(clicks.at(2).first, Rational(2));
        QVERIFY(clicks.at(2).second != clicks.at(1).second);
    }
    // ---- the sounds that are not notes ----

    void aPositionShiftOnAWoundStringIsHeard()
    {
        Score score = blank(2);
        // The low E string, first fret and then the twelfth: a hand that has
        // travelled the length of the neck along a wound string.
        fill(score, 0, {fretted(0, 1), fretted(0, 12)});

        Noises::Map map;
        map.fingering = {90};
        const QList<Timeline::NoteEvent> noises =
            Timeline::noisesFor(score, 0, {0, 1}, map);
        QCOMPARE(noises.size(), 1);
        QCOMPARE(noises.first().pitch, 90);
        // It ends where the note it was travelling towards begins, because
        // that is where the hand arrives and stops making the noise.
        QCOMPARE(noises.first().end, Rational(1));
        QCOMPARE(noises.first().start, Rational(3, 4));
    }

    void aHandThatBarelyMovesIsNotHeardMoving()
    {
        Score score = blank(2);
        fill(score, 0, {fretted(0, 5), fretted(0, 6)});

        Noises::Map map;
        map.fingering = {90};
        QVERIFY(Timeline::noisesFor(score, 0, {0, 1}, map).isEmpty());
    }

    void aShiftOnAPlainStringIsNotHeard()
    {
        Score score = blank(2);
        // The top string of a guitar is plain wire, and a fingertip on it
        // catches on nothing. The three below it are wound.
        fill(score, 0, {fretted(5, 1), fretted(5, 12)});

        Noises::Map map;
        map.fingering = {90};
        QVERIFY(Timeline::noisesFor(score, 0, {0, 1}, map).isEmpty());

        fill(score, 0, {fretted(2, 1), fretted(2, 12)});
        QCOMPARE(Timeline::noisesFor(score, 0, {0, 1}, map).size(), 1);
    }

    void aShiftToOrFromAnOpenStringIsNotHeard()
    {
        Score score = blank(2);
        // Nothing is on the string to slide along it: the hand left the neck
        // rather than moved along it.
        fill(score, 0, {fretted(0, 0), fretted(0, 12)});

        Noises::Map map;
        map.fingering = {90};
        QVERIFY(Timeline::noisesFor(score, 0, {0, 1}, map).isEmpty());

        fill(score, 0, {fretted(0, 12), fretted(0, 0)});
        QVERIFY(Timeline::noisesFor(score, 0, {0, 1}, map).isEmpty());
    }

    void aShiftAcrossASilenceIsTwoPhrasesRatherThanAShift()
    {
        Score score = blank(3);
        // A note, then two bars of nothing, then a note somewhere else. The
        // hand moved at its leisure and nobody heard it.
        fill(score, 0, {fretted(0, 1)});
        fill(score, 2, {fretted(0, 12)});

        Noises::Map map;
        map.fingering = {90};
        QVERIFY(Timeline::noisesFor(score, 0, {0, 1, 2}, map).isEmpty());
    }

    void aPartComingToAStopRestsThePick()
    {
        Score score = blank(3);
        fill(score, 0, {fretted(0, 5)});

        Noises::Map map;
        map.pickRest = {96};
        const QList<Timeline::NoteEvent> noises =
            Timeline::noisesFor(score, 0, {0, 1, 2}, map);
        QCOMPARE(keysOf(noises), QList<int>({96}));
        // Where the last note stops ringing, which is where a hand actually
        // puts the pick down.
        QCOMPARE(noises.first().start, Rational(1));
    }

    void aRestInsideAPhraseIsNotAStop()
    {
        Score score = blank(2);
        // The first note is palm-muted, so it stops halfway through its beat
        // and there is a rest before the next one. The pick has not gone
        // anywhere and nothing has come to rest on the strings.
        Note damped = fretted(0, 5);
        damped.palmMuted = true;
        fill(score, 0, {damped, fretted(0, 5)});

        Noises::Map map;
        map.pickRest = {96};
        const QList<Timeline::NoteEvent> noises =
            Timeline::noisesFor(score, 0, {0, 1}, map);
        // One where the part actually stops, and none in the middle of it.
        QCOMPARE(noises.size(), 1);
        QCOMPARE(noises.first().start, Rational(2));
    }

    void aDeadNoteIsPlayedAsTheLibrarysDeadNote()
    {
        Score score = blank(1);
        Note dead = fretted(0, 5);
        dead.muted = true;
        fill(score, 0, {dead});

        // Without a library that has one, it stays the short quiet note it
        // always was -- which is the approximation, and is what a General MIDI
        // programme gets.
        const QList<Timeline::Message> plain = Timeline::messagesFor(score, 0, {0});
        QCOMPARE(firstNoteOn(plain), 45);

        // With one, the recording is played instead of the note rather than
        // beside it. The lowest string takes the first of the five.
        const QList<Timeline::Message> sampled =
            Timeline::messagesFor(score, 0, {0}, emily());
        QCOMPARE(firstNoteOn(sampled), 91);
    }

    void eachStringGetsItsOwnDeadNote()
    {
        Score score = blank(1);
        QList<Note> notes;
        for (int string = 0; string < 6; ++string) {
            Note dead = fretted(string, 5);
            dead.muted = true;
            notes.append(dead);
        }
        fill(score, 0, notes);

        Noises::Map map;
        map.muted = emily().muted;
        QList<int> keys;
        for (const Timeline::Message &message : Timeline::messagesFor(score, 0, {0}, map)) {
            if (message.kind == Timeline::MessageKind::NoteOn) {
                keys.append(message.data1);
            }
        }
        // Five recordings spread across six strings, in order, so the lowest
        // gets the first and the highest gets the last.
        QCOMPARE(keys, QList<int>({91, 91, 92, 93, 94, 95}));
    }

    void aPartWithNoLibraryIsPlayedExactlyAsItWas()
    {
        Score score = blank(2);
        fill(score, 0, {fretted(0, 1), fretted(0, 12)});
        const QList<Timeline::Message> was = Timeline::messagesFor(score, 0, {0, 1});
        const QList<Timeline::Message> still =
            Timeline::messagesFor(score, 0, {0, 1}, Noises::Map{});
        QCOMPARE(was.size(), still.size());
        for (int index = 0; index < was.size(); ++index) {
            QCOMPARE(was.at(index).at, still.at(index).at);
            QCOMPARE(was.at(index).data1, still.at(index).data1);
        }
        // And the squeak the same notes would have made with one.
        Noises::Map map;
        map.fingering = {90};
        QVERIFY(Timeline::messagesFor(score, 0, {0, 1}, map).size() > was.size());
    }

    void aDrumKitMakesNoneOfThisNoise()
    {
        Score score = blank(2);
        score.tracks[0].instrumentType = QStringLiteral("drumKit");
        fill(score, 0, {fretted(0, 1), fretted(0, 12)});
        QVERIFY(Timeline::noisesFor(score, 0, {0, 1}, emily()).isEmpty());
    }
};

QTEST_GUILESS_MAIN(TimelineTest)
#include "timelinetest.moc"
