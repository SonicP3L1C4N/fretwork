// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "tablayout.h"
#include "tabpainter.h"

#include <QImage>
#include <QTest>

/**
 * The marks that actually reach the page.
 *
 * `tablayouttest.cpp` used to say that the painting could only be checked by
 * looking at it. That was wrong, and it was wrong in a way that shipped: half
 * the bar lines and five of the six string lines were not being drawn at all,
 * through a release, in a window that was looked at every day.
 *
 * What made it invisible is worth understanding, because it is what these
 * tests are shaped around. Whether a line appeared depended on where
 * justifying the system had left it -- a stroke thinner than a pixel drawn
 * between two of them puts half its ink in each and disappears against a pale
 * paper -- so the page always had *some* bar lines, in a pattern that changed
 * with every resize. Nothing about it read as broken. It read as a page of
 * tablature that was slightly hard to follow.
 *
 * Counting marks in a rendered image is not a substitute for looking at it. It
 * catches the one thing looking is worst at, which is the absence of something
 * nobody was expecting to count.
 *
 * Separate from the layout tests because painting needs fonts, and fonts need
 * a GUI application; the arithmetic next door needs neither.
 */
class TabPainterTest : public QObject
{
    Q_OBJECT

private:
    /** A guitar with `bars` bars, each holding four two-note beats. */
    static Score score(int bars)
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
                for (int note = 0; note < 2; ++note) {
                    Note written;
                    written.midi = 40 + note * 5 + beat;
                    written.string = note;
                    written.fret = beat + note;
                    out.notes.insert(id, written);
                    notes.append(id);
                    ++id;
                }
                Beat played;
                played.rhythm = 0;
                played.notes = notes;
                out.beats.insert(bar * 4 + beat, played);
                beats.append(bar * 4 + beat);
            }
            out.voices.insert(bar, Voice{beats});
            out.bars.insert(bar, Bar{{bar, -1, -1, -1}});
        }
        return out;
    }

    /**
     * A page laid out at a width that is deliberately not a round number, so
     * the bars land at fractions of a pixel. That is the ordinary case -- a
     * justified system almost never puts a bar line on a whole number -- and
     * it was the broken one.
     */
    static Tab::Layout page(int bars)
    {
        Tab::Style style;
        style.pageWidth = 613.7;
        style.pageHeight = 2000;
        style.showTitle = false;
        return Tab::layOut(score(bars), 0, style);
    }

    /** How many separate vertical lines cross a band of the image. */
    static int verticalLines(const QImage &image, int top, int bottom)
    {
        int found = 0;
        bool inLine = false;
        for (int x = 0; x < image.width(); ++x) {
            bool solid = true;
            for (int y = top; y < bottom && solid; ++y) {
                solid = qGray(image.pixel(x, y)) < 200;
            }
            if (solid && !inLine) {
                ++found;
            }
            inLine = solid;
        }
        return found;
    }

    enum class Mark { None, Vibrato, Tremolo };

    /** The same page, with one thing on it marked one way. */
    static Tab::Layout pageWith(int bars, Mark mark)
    {
        Score music = score(bars);
        if (mark == Mark::Vibrato) {
            Note note = music.notes.value(0);
            note.vibrato = true;
            music.notes.insert(0, note);
        } else if (mark == Mark::Tremolo) {
            Beat beat = music.beats.value(0);
            beat.tremolo = true;
            music.beats.insert(0, beat);
        }
        Tab::Style style;
        style.pageWidth = 613.7;
        style.pageHeight = 2000;
        style.showTitle = false;
        return Tab::layOut(music, 0, style);
    }

private Q_SLOTS:
    /**
     * A vibrato is drawn, and drawn on the note it belongs to.
     *
     * Asserted as ink that is there with the mark and not there without it,
     * rather than by matching a shape: what matters is that a reader can see
     * the hand is shaking, and a test that pinned the exact curve would fail
     * every time the wave was made prettier.
     */
    void aVibratoIsDrawnOverTheNoteItBelongsTo()
    {
        const Tab::Layout plain = pageWith(2, Mark::None);
        const Tab::Layout shaken = pageWith(2, Mark::Vibrato);
        const QImage without = Tab::toImage(plain, 0, 2.0);
        const QImage with = Tab::toImage(shaken, 0, 2.0);
        QCOMPARE(without.size(), with.size());

        int changed = 0;
        for (int y = 0; y < with.height(); ++y) {
            for (int x = 0; x < with.width(); ++x) {
                changed += without.pixel(x, y) != with.pixel(x, y) ? 1 : 0;
            }
        }
        QVERIFY2(changed > 0, "a note marked vibrato is drawn exactly like one that is not");
        // A mark on one note and not a change to the whole page: a wave is a
        // few dozen pixels, and anything like a thousand means the layout
        // moved rather than that something was added to it.
        QVERIFY2(changed < 1000, qPrintable(QStringLiteral("%1 pixels changed").arg(changed)));
    }

    /**
     * A tremolo is drawn too, and on the stem rather than on the note.
     *
     * Which is where printed music puts it, and it is a different place from
     * the vibrato's wave: one is about the pitch and belongs by the number,
     * the other is about the picking and belongs on the thing that says how
     * long the beat is.
     */
    void aTremoloIsDrawnOnTheStem()
    {
        const QImage without = Tab::toImage(pageWith(2, Mark::None), 0, 2.0);
        const QImage with = Tab::toImage(pageWith(2, Mark::Tremolo), 0, 2.0);
        QCOMPARE(without.size(), with.size());

        int changed = 0;
        int highest = with.height();
        for (int y = 0; y < with.height(); ++y) {
            for (int x = 0; x < with.width(); ++x) {
                if (without.pixel(x, y) != with.pixel(x, y)) {
                    ++changed;
                    highest = std::min(highest, y);
                }
            }
        }
        QVERIFY2(changed > 0, "a beat marked tremolo is drawn exactly like one that is not");
        QVERIFY2(changed < 1000, qPrintable(QStringLiteral("%1 pixels changed").arg(changed)));

        // Below the staff, where the stems are, rather than up among the
        // numbers.
        const Tab::Layout layout = pageWith(2, Mark::Tremolo);
        const Tab::System &system = layout.pages.constFirst().systems.constFirst();
        QVERIFY2(highest > int((system.y + layout.staffHeight()) * 2.0) - 8,
                 qPrintable(QStringLiteral("the mark starts at y %1").arg(highest)));
    }

    void everyBarLineIsDrawnWhereverItFalls()
    {
        const Tab::Layout layout = page(6);
        QCOMPARE(layout.pages.size(), 1);
        const Tab::System &system = layout.pages.constFirst().systems.constFirst();
        const QImage image = Tab::toImage(layout, 0, 1.0);

        // Across the middle of the staff, clear of the rows the fret numbers
        // sit on, so that only full-height lines are counted.
        const int top = int(system.y) + 2;
        const int bottom = int(system.y + layout.staffHeight()) - 2;

        // One opening each bar, and one closing the system.
        QCOMPARE(verticalLines(image, top, bottom), int(system.bars.size()) + 1);
    }

    void everyStringIsDrawn()
    {
        const Tab::Layout layout = page(6);
        const Tab::System &system = layout.pages.constFirst().systems.constFirst();

        // At several sizes, because where a line falls between pixels is the
        // whole question: the six strings of one staff all land on the same
        // fraction as each other, so at any one size they are all drawn or all
        // missing. This layout happens to land favourably at every size tried
        // here, so unlike the bar lines above it does not reproduce the fault
        // that shipped -- it is here to keep the strings on the page, which is
        // the thing that went missing in the window.
        for (const qreal scale : {1.0, 1.5, 2.0, 3.0}) {
            const QImage image = Tab::toImage(layout, 0, scale);
            // Down a column just inside the first bar, before its first beat,
            // where the string lines are the only ink there is.
            const int x = int(layout.style.margin * scale) + 2;
            int lines = 0;
            bool inLine = false;
            const int from = int((system.y - 2) * scale);
            const int to = int((system.y + layout.staffHeight() + 3) * scale);
            for (int y = from; y < to; ++y) {
                const bool ink = qGray(image.pixel(x, y)) < 235;
                if (ink && !inLine) {
                    ++lines;
                }
                inLine = ink;
            }
            if (lines != layout.strings) {
                QFAIL(qPrintable(QStringLiteral("at %1x there are %2 string lines and not %3")
                                     .arg(scale)
                                     .arg(lines)
                                     .arg(layout.strings)));
            }
        }
    }

    /**
     * The same page at twice the size has the same marks on it.
     *
     * Snapping puts lines on whole device pixels, so it moves them by up to
     * half a pixel. That must stay a nudge: if it ever swallowed or invented a
     * line at some particular scale, this is where it would show.
     */
    void theSamePageAtAnotherSizeHasTheSameLines()
    {
        const Tab::Layout layout = page(6);
        const Tab::System &system = layout.pages.constFirst().systems.constFirst();
        const int expected = int(system.bars.size()) + 1;

        for (const qreal scale : {1.0, 1.5, 2.0, 3.0}) {
            const QImage image = Tab::toImage(layout, 0, scale);
            const int top = int(system.y * scale) + int(2 * scale);
            const int bottom = int((system.y + layout.staffHeight()) * scale) - int(2 * scale);
            if (verticalLines(image, top, bottom) != expected) {
                QFAIL(qPrintable(QStringLiteral("at %1x there are %2 bar lines and not %3")
                                     .arg(scale)
                                     .arg(verticalLines(image, top, bottom))
                                     .arg(expected)));
            }
        }
    }
};

QTEST_MAIN(TabPainterTest)
#include "tabpaintertest.moc"
