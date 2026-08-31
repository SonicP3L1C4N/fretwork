// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "tablayout.h"

#include "swing.h"
#include "timeline.h"

#include <KLocalizedString>

#include "notevalue.h"

#include <QHash>
#include <QStringList>
#include <QMap>

#include <algorithm>
#include <limits>
#include <cmath>

namespace
{
/**
 * How fast, written the way it has been written since Maelzel.
 *
 * A crotchet and a number: the unit is the quarter note whatever the time
 * signature is, which is the quantity the model keeps and the one a metronome
 * counts.
 */
QString tempoMarking(double quarterBpm)
{
    return QStringLiteral("\u2669 = %1").arg(QString::number(quarterBpm, 'g', 4));
}

/**
 * How much room a beat asks for, given how long it lasts.
 *
 * Not proportional. A whole note is four times a quarter in time and nothing
 * like four times as wide on paper -- engravers have used something closer to
 * the square root for centuries, because the eye reads the *order* of events
 * from spacing and the durations from the note heads. Proportional spacing
 * makes a bar of semiquavers unreadable and a bar of semibreves mostly blank.
 */
qreal widthFor(const Rational &duration, const Tab::Style &style)
{
    return style.beatSpacing * (0.6 + std::sqrt(std::max(0.05, duration.toDouble())));
}

/** The text that goes on the string: a fret number, or a cross for a dead note. */
QString textFor(const Note &note)
{
    const QString text = note.muted ? QStringLiteral("x") : QString::number(note.fret);
    // A ghost note is played but barely heard, and tablature has always drawn
    // that as brackets. It is the note text that changes rather than anything
    // above the staff, because the note is still there.
    return note.ghost ? QStringLiteral("(%1)").arg(text) : text;
}

/**
 * Where one kind of mark starts and stops across a line of music.
 *
 * A run carries while consecutive columns have it and ends at the first that
 * does not. Columns rather than beats: two voices palm-muting the same moment
 * are one mark, because there is one hand doing it.
 */
void appendRuns(Tab::System &system, Tab::Mark mark, const QList<qreal> &xs,
                const QList<bool> &on)
{
    int start = -1;
    for (int index = 0; index <= on.size(); ++index) {
        const bool marked = index < on.size() && on.at(index);
        if (marked && start < 0) {
            start = index;
        } else if (!marked && start >= 0) {
            system.runs.append(Tab::LaidRun{mark, xs.at(start), xs.at(index - 1)});
            start = -1;
        }
    }
}

/** The palm-muted and let-ring runs over a finished line, in reading order. */
void findRuns(Tab::System &system)
{
    QList<qreal> xs;
    QList<bool> palmMuted;
    QList<bool> letRing;
    for (const Tab::LaidBar &bar : system.bars) {
        for (const Tab::LaidBeat &beat : bar.beats) {
            bool palm = false;
            bool ring = false;
            for (const Tab::LaidNote &note : beat.notes) {
                palm = palm || note.palmMuted;
                ring = ring || note.letRing;
            }
            xs.append(bar.x + beat.x);
            palmMuted.append(palm);
            letRing.append(ring);
        }
    }
    appendRuns(system, Tab::Mark::PalmMute, xs, palmMuted);
    appendRuns(system, Tab::Mark::LetRing, xs, letRing);
}

/**
 * How a note value is drawn: its beams, its head, and whether it has a stem.
 *
 * Which symbol a duration was written as is `NoteValue`'s business, because
 * the editor has to ask the same question -- told to add a dot, it needs to
 * know how many are there now.
 */
Tab::LaidRhythm symbolFor(const Rational &duration)
{
    const NoteValue::Written written = NoteValue::of(duration);

    Tab::LaidRhythm rhythm;
    rhythm.dots = written.dots;
    rhythm.beams = NoteValue::beamsOf(written.value);
    rhythm.hollow = !(written.value < Rational(2));
    rhythm.stem = written.value < Rational(4);
    return rhythm;
}

/**
 * How the bar divides for the purpose of beaming.
 *
 * Compound time groups in threes: 6/8 is two dotted quarters and not six
 * quavers, and beaming it in twos makes it read as 3/4 -- which is a different
 * piece of music written with the same notes.
 */
Rational beamGroup(const MasterBar &master)
{
    if (master.denominator == 8 && master.numerator % 3 == 0) {
        return Rational(3, 2);
    }
    return Rational(1);
}

/** Which beat of the bar an offset falls in. Offsets are never negative. */
qint64 groupIndex(const Rational &offset, const Rational &group)
{
    return (offset.numerator * group.denominator) / (offset.denominator * group.numerator);
}

/**
 * Joins the columns that share beams, and flags the ones that do not.
 *
 * Two columns are beamed together when both are quavers or shorter, in the
 * same voice, next to each other in time, and inside the same beat of the bar.
 * A rest breaks a beam: beaming over one is legal engraving and unreadable in
 * a row this small.
 */
void beamColumns(Tab::LaidBar &bar, const QList<Rational> &offsets,
                 const QList<Rational> &durations, const Rational &group)
{
    for (int index = 0; index + 1 < bar.beats.size(); ++index) {
        Tab::LaidBeat &left = bar.beats[index];
        const Tab::LaidBeat &right = bar.beats.at(index + 1);
        const bool joined = left.voice == right.voice
            && left.rhythm.beams > 0 && right.rhythm.beams > 0
            && !left.rhythm.rest && !right.rhythm.rest
            && offsets.at(index) + durations.at(index) == offsets.at(index + 1)
            && groupIndex(offsets.at(index), group)
                == groupIndex(offsets.at(index + 1), group);
        if (!joined) {
            continue;
        }
        const int shared = std::min(left.rhythm.beams, right.rhythm.beams);
        left.rhythm.beamRight = shared;
        bar.beats[index + 1].rhythm.beamLeft = shared;
    }

    for (Tab::LaidBeat &beat : bar.beats) {
        Tab::LaidRhythm &rhythm = beat.rhythm;
        const int shared = std::max(rhythm.beamLeft, rhythm.beamRight);
        if (shared == 0) {
            // Nothing to beam to, so a lone quaver keeps its flags.
            rhythm.flags = rhythm.beams;
            continue;
        }
        rhythm.stubs = std::max(0, rhythm.beams - shared);
        rhythm.stubRight = rhythm.beamRight > rhythm.beamLeft;
    }
}

/**
 * One bar of one track, measured but not yet placed.
 *
 * Voices are laid out together: every voice starts at the bar line, so two
 * voices are two sequences over the same span, and a note from each may share
 * an x position. That is what they look like on paper.
 */
Tab::LaidBar measure(const Score &score, int trackIndex, int barIndex,
                     const Tab::Style &style)
{
    const MasterBar &master = score.masterBars.at(barIndex);

    Tab::LaidBar bar;
    bar.index = barIndex;
    bar.section = master.section;
    bar.repeatStart = master.repeatStart;
    bar.repeatEnd = master.repeatEnd;
    bar.repeatCount = master.repeatCount;

    if (trackIndex >= master.bars.size()) {
        bar.width = style.minimumBarWidth;
        return bar;
    }

    // Beats from every voice, gathered by where they fall in the bar, so that
    // notes sounding together are drawn in one column.
    // Where each column came from, so that clicking one can put a caret in a
    // place that exists: the first voice to reach that moment wins it.
    struct Column {
        QList<const Beat *> beats;
        int voice = 0;
        int index = 0;
    };
    QMap<Rational, Column> columns;
    Rational longestVoice;

    const Bar source = score.bars.value(master.bars.at(trackIndex));
    for (int voiceSlot = 0; voiceSlot < source.voices.size(); ++voiceSlot) {
        const int voiceId = source.voices.at(voiceSlot);
        if (voiceId < 0) {
            continue;
        }
        Rational offset;
        int index = 0;
        for (const int beatId : score.voices.value(voiceId).beats) {
            const auto beat = score.beats.constFind(beatId);
            if (beat == score.beats.constEnd()) {
                continue;
            }
            Column &column = columns[offset];
            if (column.beats.isEmpty()) {
                column.voice = voiceSlot;
                column.index = index;
            }
            column.beats.append(&*beat);
            offset += score.rhythms.value(beat->rhythm, Rational(1));
            ++index;
        }
        if (longestVoice < offset) {
            longestVoice = offset;
        }
    }

    // An empty bar is empty rather than wrong; one with music in it that does
    // not come to the time signature is worth saying so about.
    bar.incomplete = !columns.isEmpty() && !(longestVoice == master.length());

    // A column is drawn with the rhythm of the beat that claimed it -- the same
    // one a caret addresses -- while its width still comes from the shortest
    // note in it, because that is what has to fit before the next one starts.
    qreal x = style.barPadding;
    QList<Rational> offsets;
    QList<Rational> durations;
    for (auto column = columns.constBegin(); column != columns.constEnd(); ++column) {
        Tab::LaidBeat laid;
        laid.x = x;
        laid.voice = column.value().voice;
        laid.index = column.value().index;

        const Rational owned =
            score.rhythms.value(column.value().beats.constFirst()->rhythm, Rational(1));
        laid.rhythm = symbolFor(owned);

        Rational shortest(4);
        for (const Beat *beat : column.value().beats) {
            // Any beat in the column being tremolo picked makes the column
            // one: the slashes go on the stem, and the stem is shared.
            laid.rhythm.tremolo = laid.rhythm.tremolo || beat->tremolo;
            const Rational duration = score.rhythms.value(beat->rhythm, Rational(1));
            if (duration < shortest) {
                shortest = duration;
            }
            for (const int noteId : beat->notes) {
                const auto note = score.notes.constFind(noteId);
                if (note == score.notes.constEnd() || note->string < 0) {
                    continue;
                }
                Tab::LaidNote laidNote;
                laidNote.x = x;
                laidNote.string = note->string;
                laidNote.text = textFor(*note);
                laidNote.bend = note->bended;
                laidNote.palmMuted = note->palmMuted;
                laidNote.hammer = note->hammerOrigin || note->hammerDestination;
                laidNote.slide = note->slide != SlideType::None;
                laidNote.vibrato = note->vibrato;
                laidNote.letRing = note->letRing;
                laid.notes.append(laidNote);
            }
        }

        // A column with nothing on any string is a rest. The page decides
        // that rather than the document, because a beat whose notes all sit on
        // strings this track does not have looks exactly the same.
        laid.rhythm.rest = laid.notes.isEmpty();

        offsets.append(column.key());
        durations.append(owned);
        bar.beats.append(laid);
        x += widthFor(shortest, style);
    }

    beamColumns(bar, offsets, durations, beamGroup(master));

    bar.width = std::max(style.minimumBarWidth, x + style.barPadding);
    return bar;
}
}

Tab::Layout Tab::layOut(const Score &score, int trackIndex, const Style &style)
{
    Layout layout;
    layout.style = style;
    if (trackIndex < 0 || trackIndex >= score.tracks.size() || score.masterBars.isEmpty()) {
        return layout;
    }


    const Track &track = score.tracks.at(trackIndex);
    layout.title = score.title;
    layout.artist = score.artist;
    layout.trackName = track.name;
    layout.tuning = track.tuning;
    layout.strings = std::max(4, int(track.tuning.size()));
    if (track.isPercussion()) {
        layout.strings = 5;
    }

    QList<LaidBar> bars;
    bars.reserve(int(score.masterBars.size()));
    int numerator = 0;
    int denominator = 0;
    TripletFeel feel = TripletFeel::None;
    for (int index = 0; index < score.masterBars.size(); ++index) {
        LaidBar bar = measure(score, trackIndex, index, style);
        const MasterBar &master = score.masterBars.at(index);

        // Everything this bar has to say to the player, in the order printed
        // music says it: how fast, and then how to feel it.
        QStringList said;
        bool changesTempo = index == 0;
        for (const TempoChange &tempo : score.tempos) {
            changesTempo = changesTempo || tempo.bar == index;
        }
        if (changesTempo) {
            // The first bar carries one whether or not a change is written
            // there: a page that does not say how fast it goes is missing the
            // first thing a player looks for.
            said.append(tempoMarking(Timeline::tempoAtBar(score, index)));
        }
        if (master.tripletFeel != feel) {
            feel = master.tripletFeel;
            said.append(feel == TripletFeel::None
                            ? i18nc("a direction to the player: play it as it is written",
                                    "straight")
                            : Swing::nameOf(feel));
        }
        bar.direction = said.join(QStringLiteral("  ·  "));
        if (master.numerator != numerator || master.denominator != denominator) {
            numerator = master.numerator;
            denominator = master.denominator;
            bar.timeSignature = QStringLiteral("%1/%2").arg(numerator).arg(denominator);
            // A signature needs room of its own, before the music, and how
            // much depends on how many digits it has: a fixed gap fits "4/4"
            // and puts the 2 of "12/8" through the first fret number.
            const qreal room = style.labelSize * 0.68 * bar.timeSignature.size() + 6;
            bar.width += room;
            for (LaidBeat &beat : bar.beats) {
                beat.x += room;
                for (LaidNote &note : beat.notes) {
                    note.x += room;
                }
            }
        }
        bars.append(bar);
    }

    const Style &measured = layout.style;
    const qreal usable = measured.pageWidth - measured.margin * 2;
    // The room a line of music takes is the strings *and* the rhythm under
    // them; the spacing on top of that is what the next line's labels live in.
    const qreal systemHeight = layout.systemHeight();

    Page page;
    System system;
    qreal used = 0;
    // What every system needs over it: a section name, the bar numbers under
    // that, and the row the marks live in. Reserved whether or not this
    // particular line has any, so that one that does is not drawn on top of
    // whatever is above it.
    const qreal labelRoom =
        measured.labelSize * 4.2 + measured.markGap + measured.directionGap;
    qreal y = measured.margin + (measured.showTitle ? measured.titleHeight : 0) + labelRoom;

    const auto finishSystem = [&](bool justify) {
        if (system.bars.isEmpty()) {
            return;
        }
        if (justify && used > 0 && used < usable) {
            // Spread the slack across the bars in proportion to what each
            // already takes, so a line of dense bars does not end up looking
            // airier than the line above it.
            const qreal scale = usable / used;
            qreal x = 0;
            for (LaidBar &bar : system.bars) {
                bar.x = x;
                bar.width *= scale;
                for (LaidBeat &beat : bar.beats) {
                    beat.x *= scale;
                    for (LaidNote &note : beat.notes) {
                        note.x *= scale;
                    }
                }
                x += bar.width;
            }
        }
        // After the justifying, so the runs are measured where the columns
        // actually ended up rather than where they were before the line was
        // spread to the page.
        findRuns(system);
        system.y = y;
        system.page = int(layout.pages.size());
        page.systems.append(system);
        y += systemHeight + measured.systemSpacing;
        system = System();
        used = 0;
    };

    for (LaidBar &bar : bars) {
        if (used + bar.width > usable && !system.bars.isEmpty()) {
            finishSystem(true);
            if (y + systemHeight > measured.pageHeight - measured.margin) {
                layout.pages.append(page);
                page = Page();
                // The same room again: a new page starts with a system that
                // may carry a section name of its own.
                y = measured.margin + labelRoom;
            }
        }
        bar.x = used;
        system.bars.append(bar);
        used += bar.width;
    }

    // The last line is left as it is: stretching four bars across the page
    // because they happen to be the last four is the mark of a bad engraver.
    finishSystem(false);
    if (!page.systems.isEmpty()) {
        layout.pages.append(page);
    }
    return layout;
}


namespace
{
/** Every system of every page, which is what both lookups want to walk. */
QList<const Tab::System *> allSystems(const Tab::Layout &layout)
{
    QList<const Tab::System *> systems;
    for (const Tab::Page &page : layout.pages) {
        for (const Tab::System &system : page.systems) {
            systems.append(&system);
        }
    }
    return systems;
}
}

bool Tab::hitTest(const Layout &layout, qreal x, qreal y, int *bar, int *voice, int *beat,
                  int *string)
{
    if (layout.isEmpty()) {
        return false;
    }
    const qreal reach = layout.style.systemSpacing / 2;

    for (const System *system : allSystems(layout)) {
        // Down the whole stack rather than down one page: a window shows every
        // page at once, and a click arrives as a distance from the top of the
        // first one.
        const qreal top = layout.pageTop(system->page) + system->y;
        // The rhythm row belongs to the system above it, so a click on a stem
        // lands on the bar it describes rather than falling between two lines.
        if (y < top - reach || y > top + layout.systemHeight() + reach) {
            continue;
        }

        for (const LaidBar &laid : system->bars) {
            const qreal left = layout.style.margin + laid.x;
            if (x < left || x > left + laid.width) {
                continue;
            }

            // The nearest column rather than the one under the pointer: the
            // gaps between them are wide and clicking one should not miss.
            int nearest = 0;
            qreal best = std::numeric_limits<qreal>::max();
            for (int index = 0; index < laid.beats.size(); ++index) {
                const qreal distance = std::abs(left + laid.beats.at(index).x - x);
                if (distance < best) {
                    best = distance;
                    nearest = index;
                }
            }

            if (bar) {
                *bar = laid.index;
            }
            if (!laid.beats.isEmpty()) {
                if (voice) {
                    *voice = laid.beats.at(nearest).voice;
                }
                if (beat) {
                    *beat = laid.beats.at(nearest).index;
                }
            } else {
                if (voice) {
                    *voice = 0;
                }
                if (beat) {
                    *beat = 0;
                }
            }
            if (string) {
                const int fromTop = int(std::lround((y - top) / layout.style.stringSpacing));
                *string = std::clamp(layout.strings - 1 - fromTop, 0, layout.strings - 1);
            }
            return true;
        }
    }
    return false;
}

bool Tab::positionOf(const Layout &layout, int bar, int voice, int beat, qreal *x, qreal *y,
                     qreal *width)
{
    for (const System *system : allSystems(layout)) {
        for (const LaidBar &laid : system->bars) {
            if (laid.index != bar) {
                continue;
            }
            for (const LaidBeat &column : laid.beats) {
                if (column.voice != voice || column.index != beat) {
                    continue;
                }
                if (x) {
                    *x = layout.style.margin + laid.x + column.x;
                }
                if (y) {
                    *y = layout.pageTop(system->page) + system->y;
                }
                if (width) {
                    *width = layout.style.beatSpacing;
                }
                return true;
            }
        }
    }
    return false;
}
