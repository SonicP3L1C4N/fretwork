// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "tablayout.h"

#include <QHash>
#include <QMap>

#include <algorithm>
#include <limits>
#include <cmath>

namespace
{
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
    if (note.muted) {
        return QStringLiteral("x");
    }
    return QString::number(note.fret);
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
    }

    qreal x = style.barPadding;
    for (auto column = columns.constBegin(); column != columns.constEnd(); ++column) {
        Tab::LaidBeat laid;
        laid.x = x;
        laid.voice = column.value().voice;
        laid.index = column.value().index;

        Rational shortest(4);
        for (const Beat *beat : column.value().beats) {
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
                laidNote.letRing = note->letRing;
                laid.notes.append(laidNote);
            }
        }

        bar.beats.append(laid);
        // The column is as wide as its shortest note needs, because that is
        // what has to fit before the next column starts.
        x += widthFor(shortest, style);
    }

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
    for (int index = 0; index < score.masterBars.size(); ++index) {
        LaidBar bar = measure(score, trackIndex, index, style);
        const MasterBar &master = score.masterBars.at(index);
        if (master.numerator != numerator || master.denominator != denominator) {
            numerator = master.numerator;
            denominator = master.denominator;
            bar.timeSignature = QStringLiteral("%1/%2").arg(numerator).arg(denominator);
            // A signature needs room of its own, before the music.
            bar.width += style.beatSpacing;
            for (LaidBeat &beat : bar.beats) {
                beat.x += style.beatSpacing;
                for (LaidNote &note : beat.notes) {
                    note.x += style.beatSpacing;
                }
            }
        }
        bars.append(bar);
    }

    const qreal usable = style.pageWidth - style.margin * 2;
    const qreal staff = layout.staffHeight();

    Page page;
    System system;
    qreal used = 0;
    qreal y = style.margin + style.titleHeight;

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
        system.y = y;
        page.systems.append(system);
        y += staff + style.systemSpacing;
        system = System();
        used = 0;
    };

    for (LaidBar &bar : bars) {
        if (used + bar.width > usable && !system.bars.isEmpty()) {
            finishSystem(true);
            if (y + staff > style.pageHeight - style.margin) {
                layout.pages.append(page);
                page = Page();
                // The same room the labels want, since a new page starts with
                // a system that may carry a section name of its own.
                y = style.margin + style.labelSize * 4.2;
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
    const qreal staff = layout.staffHeight();
    const qreal reach = layout.style.systemSpacing / 2;

    for (const System *system : allSystems(layout)) {
        if (y < system->y - reach || y > system->y + staff + reach) {
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
                const int fromTop = int(std::lround((y - system->y) / layout.style.stringSpacing));
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
                    *y = system->y;
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
