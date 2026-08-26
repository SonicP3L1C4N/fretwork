// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "tablayout.h"

#include <QTest>

/**
 * Where things go, checked by reading the numbers.
 *
 * The painting is checked by looking at it, which is the only way and not a
 * test. What can be tested is that bars run left to right without overlapping,
 * that a full line fills the page and the last one does not, and that a note
 * ends up on the string it was written on -- the mistakes that are obvious in
 * a picture and invisible in a diff.
 */
class TabLayoutTest : public QObject
{
    Q_OBJECT

private:
    /** A guitar with `bars` bars, each holding four quarter-note chords. */
    static Score score(int bars, int notesPerBeat = 2)
    {
        Score out;
        Track guitar;
        guitar.name = QStringLiteral("Guitar");
        guitar.instrumentType = QStringLiteral("electricGuitar");
        for (int string = 0; string < 6; ++string) {
            guitar.tuning.append(40 + string * 5);
        }
        out.tracks.append(guitar);
        out.rhythms.insert(0, Rational(1));

        int id = 0;
        for (int bar = 0; bar < bars; ++bar) {
            MasterBar master;
            master.bars = {bar};
            out.masterBars.append(master);

            QList<int> beats;
            for (int beat = 0; beat < 4; ++beat) {
                QList<int> notes;
                for (int note = 0; note < notesPerBeat; ++note) {
                    Note written;
                    written.midi = 40 + note * 5;
                    written.string = note;
                    written.fret = beat + note;
                    out.notes.insert(id, written);
                    notes.append(id);
                    ++id;
                }
                out.beats.insert(id, Beat{0, notes, Dynamic::F, false, false});
                beats.append(id);
                ++id;
            }
            out.voices.insert(bar, Voice{beats});
            out.bars.insert(bar, Bar{{bar, -1, -1, -1}});
        }
        return out;
    }

    /**
     * One bar of one voice with the durations given, one note in each beat.
     *
     * Rhythm is the whole point of these, so they are written as a list of
     * durations rather than built out of a score with a shape.
     */
    static Score oneBar(const QList<Rational> &durations, int numerator = 4,
                        int denominator = 4, const QList<int> &silent = {})
    {
        Score out;
        Track guitar;
        guitar.name = QStringLiteral("Guitar");
        guitar.instrumentType = QStringLiteral("electricGuitar");
        for (int string = 0; string < 6; ++string) {
            guitar.tuning.append(40 + string * 5);
        }
        out.tracks.append(guitar);

        MasterBar master;
        master.bars = {0};
        master.numerator = numerator;
        master.denominator = denominator;
        out.masterBars.append(master);

        QList<int> beats;
        int noteId = 0;
        for (int index = 0; index < durations.size(); ++index) {
            out.rhythms.insert(index, durations.at(index));
            QList<int> notes;
            if (!silent.contains(index)) {
                Note written;
                written.string = 0;
                written.fret = 5;
                written.midi = 45;
                out.notes.insert(noteId, written);
                notes.append(noteId);
                ++noteId;
            }
            out.beats.insert(index, Beat{index, notes, Dynamic::MF, false, false});
            beats.append(index);
        }
        out.voices.insert(0, Voice{beats});
        out.bars.insert(0, Bar{{0, -1, -1, -1}});
        return out;
    }

    /** Marks every note of the given columns of bar 0 as palm muted. */
    static void palmMute(Score &score, const QList<int> &columns)
    {
        const QList<int> beats = score.voices.value(0).beats;
        for (const int column : columns) {
            for (const int noteId : score.beats.value(beats.value(column)).notes) {
                score.notes[noteId].palmMuted = true;
            }
        }
    }

    /** The rhythm of every column of a one-bar score, in order. */
    static QList<Tab::LaidRhythm> rhythms(const Score &score)
    {
        QList<Tab::LaidRhythm> out;
        const Tab::Layout layout = Tab::layOut(score, 0);
        for (const Tab::LaidBeat &beat :
             layout.pages.constFirst().systems.constFirst().bars.constFirst().beats) {
            out.append(beat.rhythm);
        }
        return out;
    }

    /** Every bar's direction, in order, for the bars that carry one. */
    static QList<QPair<int, QString>> directionsOf(const Tab::Layout &layout)
    {
        QList<QPair<int, QString>> out;
        for (const Tab::Page &page : layout.pages) {
            for (const Tab::System &system : page.systems) {
                for (const Tab::LaidBar &bar : system.bars) {
                    if (!bar.direction.isEmpty()) {
                        out.append({bar.index, bar.direction});
                    }
                }
            }
        }
        return out;
    }

private Q_SLOTS:
    void laysBarsLeftToRightWithoutOverlapping()
    {
        const Tab::Layout layout = Tab::layOut(score(12), 0);
        QVERIFY(!layout.isEmpty());

        for (const Tab::Page &page : layout.pages) {
            for (const Tab::System &system : page.systems) {
                qreal previous = -1;
                for (const Tab::LaidBar &bar : system.bars) {
                    QVERIFY2(bar.x >= previous, "a bar starts before the one before it ends");
                    QVERIFY(bar.width > 0);
                    previous = bar.x + bar.width;
                }
            }
        }
    }

    /** A justified line fills the page; the last one is left alone. */
    void everyLineButTheLastFillsTheWidth()
    {
        const Tab::Style style;
        const Tab::Layout layout = Tab::layOut(score(40), 0, style);
        const qreal usable = style.pageWidth - style.margin * 2;

        QList<const Tab::System *> systems;
        for (const Tab::Page &page : layout.pages) {
            for (const Tab::System &system : page.systems) {
                systems.append(&system);
            }
        }
        QVERIFY(systems.size() > 2);

        for (int index = 0; index < systems.size() - 1; ++index) {
            const Tab::LaidBar &last = systems.at(index)->bars.constLast();
            QVERIFY2(qAbs(last.x + last.width - usable) < 0.5,
                     qPrintable(QStringLiteral("system %1 ends at %2, not %3")
                                    .arg(index)
                                    .arg(last.x + last.width)
                                    .arg(usable)));
        }

        // Stretching the final four bars across the page because they happen
        // to be last is the mark of a bad engraver.
        const Tab::LaidBar &tail = systems.constLast()->bars.constLast();
        QVERIFY(tail.x + tail.width <= usable + 0.5);
    }

    void breaksIntoSystemsAndThenIntoPages()
    {
        QCOMPARE(Tab::layOut(score(2), 0).pages.size(), 1);

        const Tab::Layout many = Tab::layOut(score(300), 0);
        QVERIFY2(many.pages.size() > 3,
                 qPrintable(QStringLiteral("300 bars fitted on %1 pages")
                                .arg(many.pages.size())));

        int bars = 0;
        for (const Tab::Page &page : many.pages) {
            QVERIFY(!page.systems.isEmpty());
            for (const Tab::System &system : page.systems) {
                bars += int(system.bars.size());
            }
        }
        // Every bar is laid out exactly once, which line breaking is the
        // easiest thing in the world to get wrong.
        QCOMPARE(bars, 300);
    }

    void systemsDoNotRunOffTheBottomOfThePage()
    {
        const Tab::Style style;
        const Tab::Layout layout = Tab::layOut(score(300), 0, style);
        for (const Tab::Page &page : layout.pages) {
            for (const Tab::System &system : page.systems) {
                QVERIFY(system.y >= style.margin);
                QVERIFY(system.y + layout.systemHeight() <= style.pageHeight - style.margin);
            }
        }
    }

    void notesLandOnTheStringTheyWereWrittenOn()
    {
        const Tab::Layout layout = Tab::layOut(score(4, 6), 0);
        const Tab::LaidBar &bar = layout.pages.constFirst().systems.constFirst().bars.constFirst();
        QCOMPARE(bar.beats.size(), 4);

        for (const Tab::LaidBeat &beat : bar.beats) {
            QCOMPARE(beat.notes.size(), 6);
            QList<int> strings;
            for (const Tab::LaidNote &note : beat.notes) {
                strings.append(note.string);
                QVERIFY(note.x >= 0);
                QVERIFY(!note.text.isEmpty());
            }
            std::sort(strings.begin(), strings.end());
            QCOMPARE(strings, QList<int>({0, 1, 2, 3, 4, 5}));
        }
    }

    void beatsRunLeftToRightInsideTheirBar()
    {
        const Tab::Layout layout = Tab::layOut(score(4), 0);
        for (const Tab::System &system : layout.pages.constFirst().systems) {
            for (const Tab::LaidBar &bar : system.bars) {
                qreal previous = -1;
                for (const Tab::LaidBeat &beat : bar.beats) {
                    QVERIFY(beat.x > previous);
                    QVERIFY(beat.x < bar.width);
                    previous = beat.x;
                }
            }
        }
    }

    /** A signature is printed where it changes and nowhere else. */
    void theTimeSignatureIsDrawnOnlyWhereItChanges()
    {
        Score changing = score(6);
        changing.masterBars[3].numerator = 3;
        changing.masterBars[4].numerator = 3;

        const Tab::Layout layout = Tab::layOut(changing, 0);
        QList<QString> signatures;
        for (const Tab::Page &page : layout.pages) {
            for (const Tab::System &system : page.systems) {
                for (const Tab::LaidBar &bar : system.bars) {
                    if (!bar.timeSignature.isEmpty()) {
                        signatures.append(QStringLiteral("%1:%2")
                                              .arg(bar.index)
                                              .arg(bar.timeSignature));
                    }
                }
            }
        }
        QCOMPARE(signatures, QList<QString>({QStringLiteral("0:4/4"),
                                             QStringLiteral("3:3/4"),
                                             QStringLiteral("5:4/4")}));
    }

    void carriesSectionsAndRepeats()
    {
        Score marked = score(4);
        marked.masterBars[0].section = QStringLiteral("Intro");
        marked.masterBars[1].repeatStart = true;
        marked.masterBars[3].repeatEnd = true;
        marked.masterBars[3].repeatCount = 4;

        const Tab::System &system = Tab::layOut(marked, 0).pages.constFirst().systems.constFirst();
        QCOMPARE(system.bars.at(0).section, QStringLiteral("Intro"));
        QVERIFY(system.bars.at(1).repeatStart);
        QVERIFY(system.bars.at(3).repeatEnd);
        QCOMPARE(system.bars.at(3).repeatCount, 4);
    }

    /** The score is drawn as notated: a repeat is a sign, not eight more bars. */
    void repeatsAreNotExpanded()
    {
        Score repeated = score(4);
        repeated.masterBars[0].repeatStart = true;
        repeated.masterBars[3].repeatEnd = true;
        repeated.masterBars[3].repeatCount = 4;

        int bars = 0;
        for (const Tab::Page &page : Tab::layOut(repeated, 0).pages) {
            for (const Tab::System &system : page.systems) {
                bars += int(system.bars.size());
            }
        }
        QCOMPARE(bars, 4);
    }

    /**
     * The written symbol is worked back out of a duration.
     *
     * The document keeps durations with their dots already multiplied in,
     * which is what playback wants; the page needs to know it was written as a
     * dotted crotchet, and there is only one way three-quarters of a minim can
     * have been written down.
     */
    void readsTheWrittenSymbolBackOutOfADuration()
    {
        const auto row = rhythms(oneBar({Rational(4), Rational(2), Rational(1), Rational(1, 2),
                                         Rational(1, 4), Rational(3, 2), Rational(7, 4)},
                                        15, 4));
        QCOMPARE(row.size(), 7);

        // A semibreve is an open head and no stem at all.
        QVERIFY(!row.at(0).stem);
        QVERIFY(row.at(0).hollow);
        QCOMPARE(row.at(0).beams, 0);

        // A minim is that head on a stem; a crotchet is a stem alone.
        QVERIFY(row.at(1).stem);
        QVERIFY(row.at(1).hollow);
        QVERIFY(!row.at(2).hollow);
        QCOMPARE(row.at(2).beams, 0);

        // A quaver has one beam and a semiquaver two.
        QCOMPARE(row.at(3).beams, 1);
        QCOMPARE(row.at(4).beams, 2);

        // Dots, which are the only way those two durations can be written.
        QCOMPARE(row.at(5).beams, 0);
        QCOMPARE(row.at(5).dots, 1);
        QCOMPARE(row.at(6).dots, 2);
    }

    /** A triplet quaver is a third of a crotchet and still written as a quaver. */
    void drawsATripletAsTheValueItIsWrittenAs()
    {
        const auto row = rhythms(oneBar({Rational(1, 3), Rational(1, 3), Rational(1, 3)}));
        for (const Tab::LaidRhythm &rhythm : row) {
            QCOMPARE(rhythm.beams, 1);
            QCOMPARE(rhythm.dots, 0);
        }
    }

    void beamsWithinABeatAndNeverAcrossOne()
    {
        const auto row = rhythms(oneBar(QList<Rational>(8, Rational(1, 2))));
        QCOMPARE(row.size(), 8);
        for (int index = 0; index < row.size(); ++index) {
            // In pairs: the first quaver of each crotchet carries the beam to
            // the second, and the second carries nothing into the next beat.
            const bool first = index % 2 == 0;
            QCOMPARE(row.at(index).beamRight, first ? 1 : 0);
            QCOMPARE(row.at(index).beamLeft, first ? 0 : 1);
            QCOMPARE(row.at(index).flags, 0);
        }
    }

    /** 6/8 is two dotted crotchets. Beaming it in twos makes it read as 3/4. */
    void beamsCompoundTimeInThrees()
    {
        const auto row = rhythms(oneBar(QList<Rational>(6, Rational(1, 2)), 6, 8));
        QCOMPARE(row.size(), 6);
        QCOMPARE(row.at(1).beamLeft, 1);
        QCOMPARE(row.at(1).beamRight, 1);
        QCOMPARE(row.at(2).beamRight, 0);
        QCOMPARE(row.at(3).beamLeft, 0);
        QCOMPARE(row.at(4).beamRight, 1);
    }

    /** A lone quaver has nothing to beam to, so it keeps its flag. */
    void flagsWhatItCannotBeam()
    {
        const auto row = rhythms(oneBar({Rational(1, 2), Rational(1), Rational(1), Rational(1)}));
        QCOMPARE(row.at(0).flags, 1);
        QCOMPARE(row.at(0).beamRight, 0);
        QCOMPARE(row.at(1).flags, 0);
    }

    void pointsAPartialBeamAtTheNoteItBelongsWith()
    {
        // A dotted quaver and a semiquaver share one beam. The semiquaver's
        // second beam has nowhere to run to, and belongs with the note behind
        // it rather than with the empty space in front.
        const auto row = rhythms(oneBar({Rational(3, 4), Rational(1, 4), Rational(1),
                                         Rational(1), Rational(1)}));
        QCOMPARE(row.at(0).beams, 1);
        QCOMPARE(row.at(0).dots, 1);
        QCOMPARE(row.at(0).beamRight, 1);

        QCOMPARE(row.at(1).beams, 2);
        QCOMPARE(row.at(1).beamLeft, 1);
        QCOMPARE(row.at(1).stubs, 1);
        QVERIFY(!row.at(1).stubRight);

        // The other way round, the stub points forwards.
        const auto reversed = rhythms(oneBar({Rational(1, 4), Rational(3, 4), Rational(1),
                                              Rational(1), Rational(1)}));
        QCOMPARE(reversed.at(0).stubs, 1);
        QVERIFY(reversed.at(0).stubRight);
    }

    void marksARestAndDoesNotBeamOverIt()
    {
        const auto row = rhythms(oneBar(QList<Rational>(4, Rational(1, 2)), 4, 4, {1}));
        QVERIFY(!row.at(0).rest);
        QVERIFY(row.at(1).rest);

        // The pair is broken: the quaver before the rest has nothing to beam
        // to and keeps its flag, and the rest still says how long it lasts.
        QCOMPARE(row.at(0).beamRight, 0);
        QCOMPARE(row.at(0).flags, 1);
        QCOMPARE(row.at(1).flags, 1);
        QCOMPARE(row.at(2).beamRight, 1);
    }

    /**
     * A bar that does not come to its time signature says so.
     *
     * Marked, not corrected: taking the difference out of the neighbouring
     * note would be rewriting music nobody asked it to touch. An empty bar is
     * empty rather than wrong, and is left alone.
     */
    void marksABarThatDoesNotAddUp()
    {
        const auto complete = Tab::layOut(oneBar(QList<Rational>(4, Rational(1))), 0);
        QVERIFY(!complete.pages.constFirst().systems.constFirst().bars.constFirst().incomplete);

        const auto missing = Tab::layOut(oneBar(QList<Rational>(3, Rational(1))), 0);
        QVERIFY(missing.pages.constFirst().systems.constFirst().bars.constFirst().incomplete);

        const auto over = Tab::layOut(oneBar(QList<Rational>(5, Rational(1))), 0);
        QVERIFY(over.pages.constFirst().systems.constFirst().bars.constFirst().incomplete);

        // Six quavers is a whole bar of 6/8 and three quarters of one in 3/4.
        const auto compound = Tab::layOut(oneBar(QList<Rational>(6, Rational(1, 2)), 6, 8), 0);
        QVERIFY(!compound.pages.constFirst().systems.constFirst().bars.constFirst().incomplete);

        const auto empty = Tab::layOut(oneBar({}), 0);
        QVERIFY(!empty.pages.constFirst().systems.constFirst().bars.constFirst().incomplete);
    }

    /** The stems need room, and the line below must not be drawn through them. */
    void leavesRoomBelowTheStringsForTheRhythm()
    {
        const Tab::Layout layout = Tab::layOut(score(40), 0);
        QVERIFY(layout.systemHeight() > layout.staffHeight());

        const Tab::Page &page = layout.pages.constFirst();
        QVERIFY(page.systems.size() > 1);
        for (int index = 1; index < page.systems.size(); ++index) {
            QVERIFY2(page.systems.at(index).y - page.systems.at(index - 1).y
                         >= layout.systemHeight(),
                     "a system's stems run into the line below it");
        }
    }

    void asksForNothingItCannotDraw()
    {
        QVERIFY(Tab::layOut(Score(), 0).isEmpty());
        QVERIFY(Tab::layOut(score(4), -1).isEmpty());
        QVERIFY(Tab::layOut(score(4), 9).isEmpty());
    }

    // ---- the marks that carry over a run of notes ----

    /** A run is drawn from the first column it covers to the last. */
    void marksARunFromItsFirstColumnToItsLast()
    {
        Score marked = score(2);
        palmMute(marked, {1, 2, 3});

        const Tab::Layout layout = Tab::layOut(marked, 0);
        const Tab::System &system = layout.pages.constFirst().systems.constFirst();
        QCOMPARE(int(system.runs.size()), 1);
        QCOMPARE(system.runs.constFirst().mark, Tab::Mark::PalmMute);

        const Tab::LaidBar &bar = system.bars.constFirst();
        QCOMPARE(system.runs.constFirst().from, bar.x + bar.beats.at(1).x);
        QCOMPARE(system.runs.constFirst().to, bar.x + bar.beats.at(3).x);
    }

    /** A column without it ends the run, and the label starts again after. */
    void aGapEndsTheRunRatherThanBeingDrawnThrough()
    {
        Score marked = score(2);
        palmMute(marked, {0, 2, 3});

        const Tab::Layout layout = Tab::layOut(marked, 0);
        const Tab::System &system = layout.pages.constFirst().systems.constFirst();
        QCOMPARE(int(system.runs.size()), 2);

        const Tab::LaidBar &bar = system.bars.constFirst();
        // The first covers one column and so has nowhere to draw a line to.
        QCOMPARE(system.runs.at(0).from, bar.x + bar.beats.at(0).x);
        QCOMPARE(system.runs.at(0).to, system.runs.at(0).from);
        QCOMPARE(system.runs.at(1).from, bar.x + bar.beats.at(2).x);
        QCOMPARE(system.runs.at(1).to, bar.x + bar.beats.at(3).x);
    }

    /** Two voices palm-muting one moment are one mark: there is one hand. */
    void oneColumnMarkedInEitherVoiceIsOneRun()
    {
        Score marked = score(1);
        palmMute(marked, {0, 1, 2, 3});
        // Only the second note of each chord keeps the mark, which is still
        // the same column being palm muted.
        for (auto note = marked.notes.begin(); note != marked.notes.end(); ++note) {
            note->palmMuted = note->string == 1;
        }

        const Tab::Layout layout = Tab::layOut(marked, 0);
        QCOMPARE(int(layout.pages.constFirst().systems.constFirst().runs.size()), 1);
    }

    /**
     * A dashed line cannot cross a line break, so the run stops at the end of
     * the system and the label is printed again on the next one.
     */
    void aRunStopsAtTheEndOfALine()
    {
        Score marked = score(40);
        for (auto note = marked.notes.begin(); note != marked.notes.end(); ++note) {
            note->palmMuted = true;
        }

        const Tab::Layout layout = Tab::layOut(marked, 0);
        int systems = 0;
        for (const Tab::Page &page : layout.pages) {
            for (const Tab::System &system : page.systems) {
                ++systems;
                // One unbroken run over the whole line, every line.
                QCOMPARE(int(system.runs.size()), 1);
                QCOMPARE(system.runs.constFirst().from,
                         system.bars.constFirst().x + system.bars.constFirst().beats.constFirst().x);
                QCOMPARE(system.runs.constFirst().to,
                         system.bars.constLast().x + system.bars.constLast().beats.constLast().x);
            }
        }
        QVERIFY(systems > 2);
    }

    /** A ghost note is drawn in brackets, and a dead one is still a cross. */
    void ghostNotesAreDrawnInBrackets()
    {
        Score marked = score(1, 1);
        marked.notes[0].ghost = true;
        marked.notes[2].muted = true;
        marked.notes[4].muted = true;
        marked.notes[4].ghost = true;

        const Tab::LaidBar &bar =
            Tab::layOut(marked, 0).pages.constFirst().systems.constFirst().bars.constFirst();
        QCOMPARE(bar.beats.at(0).notes.constFirst().text, QStringLiteral("(0)"));
        QCOMPARE(bar.beats.at(1).notes.constFirst().text, QStringLiteral("x"));
        QCOMPARE(bar.beats.at(2).notes.constFirst().text, QStringLiteral("(x)"));
    }

    // ---- directions to the player ----

    void saysWhenTheFeelStartsAndWhenItStops()
    {
        Score swung = score(8);
        for (int bar = 0; bar < 4; ++bar) {
            swung.masterBars[bar].tripletFeel = TripletFeel::Triplet8th;
        }

        // Once where it starts and once where it stops, and not on the bars
        // in between: a direction printed over every bar is one nobody reads.
        // The bar that goes back to playing as written says so, because a
        // shuffle that merely stopped being printed reads as one carrying on.
        const QList<QPair<int, QString>> said = directionsOf(Tab::layOut(swung, 0));
        QCOMPARE(said.size(), 2);
        QCOMPARE(said.at(0).first, 0);
        QCOMPARE(said.at(1).first, 4);
        QCOMPARE(said.at(1).second, QStringLiteral("straight"));
    }

    void aScoreWithNothingToSayIsDrawnExactlyAsItWas()
    {
        // The whole argument for a row that is only sometimes there: adding it
        // must not move a single system on a score that has no directions in
        // it, or every page of every piece is repaginated for a feature it
        // does not use.
        const Score plain = score(12);
        Score swung = plain;
        swung.masterBars[0].tripletFeel = TripletFeel::Triplet8th;

        const Tab::Layout without = Tab::layOut(plain, 0);
        const Tab::Layout with = Tab::layOut(swung, 0);
        QCOMPARE(without.style.systemSpacing, Tab::Style().systemSpacing);
        QCOMPARE(with.style.systemSpacing,
                 Tab::Style().systemSpacing + Tab::Style().directionGap);

        const qreal first = without.pages.first().systems.first().y;
        QCOMPARE(with.pages.first().systems.first().y, first + Tab::Style().directionGap);
    }

    void everySystemGetsTheRoomAndNotJustTheFirst()
    {
        // A direction can start on any line, so the room has to be under every
        // one of them: reserving it only where a direction lands would put the
        // one on line four through the stems of line three.
        Score swung = score(40);
        swung.masterBars[0].tripletFeel = TripletFeel::Triplet16th;
        const Tab::Layout layout = Tab::layOut(swung, 0);

        const Tab::Style style;
        const qreal step = layout.systemHeight() + style.systemSpacing + style.directionGap;
        const QList<Tab::System> &systems = layout.pages.first().systems;
        QVERIFY(systems.size() > 2);
        for (int index = 1; index < systems.size(); ++index) {
            QCOMPARE(systems.at(index).y - systems.at(index - 1).y, step);
        }
    }
};

QTEST_GUILESS_MAIN(TabLayoutTest)
#include "tablayouttest.moc"
