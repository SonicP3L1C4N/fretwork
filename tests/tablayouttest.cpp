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
                QVERIFY(system.y + layout.staffHeight() <= style.pageHeight - style.margin);
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

    void asksForNothingItCannotDraw()
    {
        QVERIFY(Tab::layOut(Score(), 0).isEmpty());
        QVERIFY(Tab::layOut(score(4), -1).isEmpty());
        QVERIFY(Tab::layOut(score(4), 9).isEmpty());
    }
};

QTEST_GUILESS_MAIN(TabLayoutTest)
#include "tablayouttest.moc"
