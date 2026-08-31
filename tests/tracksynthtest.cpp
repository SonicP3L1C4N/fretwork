// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "renderer.h"
#include "timeline.h"
#include "tracksynth.h"

#include <QTest>

#include <cmath>
#include <vector>

/**
 * The synth a track gets to itself, which is the thing this program is for.
 *
 * One instance per track is what makes a stem a stem, and it is the piece the
 * release assessment named as the worst gap in the suite: stem export is what
 * people would come to Fretwork for, and a stem quietly missing a note is the
 * failure it can least afford. Everything here is aimed at that -- not at
 * whether the audio sounds nice, which no test can say, but at whether the
 * notes that were asked for are in the buffer at all, in the right places, and
 * the same however the caller chops it up.
 *
 * Skipped without a SoundFont rather than failed: a machine with no
 * `fluid-soundfont-gm` is a missing dependency and not a broken program.
 */
class TrackSynthTest : public QObject
{
    Q_OBJECT

private:
    /** A guitar playing four crotchets, loud, from the top of the bar. */
    static Score oneBar(int notes = 4)
    {
        Score score;
        Track guitar;
        guitar.name = QStringLiteral("Guitar");
        guitar.instrumentType = QStringLiteral("electricGuitar");
        guitar.program = 29;
        for (int string = 0; string < 6; ++string) {
            guitar.tuning.append(40 + string * 5);
        }
        score.tracks.append(guitar);
        score.rhythms.insert(0, Rational(1));
        score.tempos.append({0, 0, 120});

        MasterBar master;
        master.bars = {0};
        score.masterBars.append(master);
        score.bars.insert(0, Bar{{0, -1, -1, -1}});

        QList<int> beats;
        for (int index = 0; index < notes; ++index) {
            Note note;
            note.string = 0;
            note.fret = index * 2;
            note.midi = 40 + note.fret;
            score.notes.insert(index, note);
            Beat beat;
            beat.rhythm = 0;
            beat.notes = {index};
            beat.dynamic = Dynamic::F;
            score.beats.insert(index, beat);
            beats.append(index);
        }
        score.voices.insert(0, Voice{beats});
        return score;
    }

    struct Buffer {
        std::vector<float> left;
        std::vector<float> right;
        Buffer(int frames)
            : left(size_t(frames), 0.0f)
            , right(size_t(frames), 0.0f)
        {
        }
    };

    /** How loud a stretch of one channel is, which is all "is there a note
     *  here" needs. */
    static double peak(const std::vector<float> &samples, int from, int to)
    {
        double loudest = 0;
        for (int index = from; index < to && index < int(samples.size()); ++index) {
            loudest = std::max(loudest, double(std::fabs(samples[size_t(index)])));
        }
        return loudest;
    }

    static bool haveSoundFont()
    {
        return !Render::findSoundFont().isEmpty();
    }

private Q_SLOTS:
    /**
     * A synth with no SoundFont to load says so rather than pretending, since
     * everything downstream of it would otherwise render silence and call it a
     * stem.
     */
    void withNothingToPlayItSaysSo()
    {
        const Score score = oneBar();
        const QList<int> order = Timeline::playedOrder(score);
        const Timeline::Clock clock(score, order);

        TrackSynth::Options options;
        options.soundFont = QStringLiteral("/nowhere/there/is/no/such.sf2");
        TrackSynth synth(score.tracks.first(), Timeline::messagesFor(score, 0, order), clock,
                         options);
        QVERIFY(!synth.isValid());
    }

    /** The last event is where a render may stop, so it has to be the last one. */
    void itKnowsWhereTheMusicEnds()
    {
        if (!haveSoundFont()) {
            QSKIP("no SoundFont on this machine; install fluid-soundfont-gm");
        }
        const Score score = oneBar();
        const QList<int> order = Timeline::playedOrder(score);
        const Timeline::Clock clock(score, order);
        const QList<Timeline::Message> messages = Timeline::messagesFor(score, 0, order);
        QVERIFY(!messages.isEmpty());

        TrackSynth::Options options;
        options.soundFont = Render::findSoundFont();
        TrackSynth synth(score.tracks.first(), messages, clock, options);
        QVERIFY(synth.isValid());

        const double seconds = clock.secondsAt(messages.constLast().at);
        QCOMPARE(synth.lastEventSample(), qint64(seconds * options.sampleRate));
    }

    /**
     * The one that matters: the notes are actually in the buffer.
     *
     * Nothing before the first note, and something during it. A stem that is
     * silent where a note was written is the failure this whole suite exists
     * for, and it is invisible in every other kind of test.
     */
    void theNotesAreInTheBuffer()
    {
        if (!haveSoundFont()) {
            QSKIP("no SoundFont on this machine; install fluid-soundfont-gm");
        }
        const Score score = oneBar();
        const QList<int> order = Timeline::playedOrder(score);
        const Timeline::Clock clock(score, order);

        TrackSynth::Options options;
        options.soundFont = Render::findSoundFont();
        TrackSynth synth(score.tracks.first(), Timeline::messagesFor(score, 0, order), clock,
                         options);
        QVERIFY(synth.isValid());

        // Two seconds at 120bpm is the whole bar and a little air after it.
        const int frames = options.sampleRate * 2;
        Buffer buffer(frames);
        synth.fill(buffer.left.data(), buffer.right.data(), frames, 0);

        // The first note is at sample nought, so the opening of the buffer is
        // the note rather than the silence before it.
        QVERIFY2(peak(buffer.left, 0, options.sampleRate / 4) > 0.001,
                 "the first note is not in the buffer at all");

        // And every later crotchet is there too. At 120bpm a crotchet is half
        // a second; each is sampled just after its attack.
        for (int note = 1; note < 4; ++note) {
            const int at = int(note * options.sampleRate * 0.5);
            QVERIFY2(peak(buffer.left, at, at + options.sampleRate / 8) > 0.001,
                     qPrintable(QStringLiteral("note %1 is missing from the stem").arg(note + 1)));
        }
    }

    /**
     * A note written half a second in is half a second in, whatever size the
     * blocks were.
     *
     * One note and three rests, because that is the only shape where the
     * answer cannot be mistaken for something else: either the buffer is
     * silent until the note or it is not.
     */
    void aNoteIsWhereItWasWritten()
    {
        if (!haveSoundFont()) {
            QSKIP("no SoundFont on this machine; install fluid-soundfont-gm");
        }
        Score score = oneBar(0);
        // Four beats, and only the second of them has a note on it.
        QList<int> beats;
        for (int index = 0; index < 4; ++index) {
            Beat beat;
            beat.rhythm = 0;
            beat.dynamic = Dynamic::F;
            if (index == 1) {
                Note note;
                note.string = 0;
                note.fret = 5;
                note.midi = 45;
                score.notes.insert(0, note);
                beat.notes = {0};
            }
            score.beats.insert(index, beat);
            beats.append(index);
        }
        score.voices.insert(0, Voice{beats});

        const QList<int> order = Timeline::playedOrder(score);
        const Timeline::Clock clock(score, order);
        TrackSynth::Options options;
        options.soundFont = Render::findSoundFont();
        const int frames = options.sampleRate;
        // At 120bpm the second crotchet falls half a second in.
        const int expected = options.sampleRate / 2;

        for (const int block : {frames, 373, 4096}) {
            TrackSynth synth(score.tracks.first(), Timeline::messagesFor(score, 0, order), clock,
                             options);
            QVERIFY(synth.isValid());
            Buffer buffer(frames);
            for (int at = 0; at < frames; at += block) {
                synth.fill(buffer.left.data() + at, buffer.right.data() + at,
                           std::min(block, frames - at), at);
            }
            // Nothing before the note, and something after it. A tenth of a
            // second of margin either side, so this is about where the note is
            // and not about the exact sample its attack crosses a threshold.
            const double before = peak(buffer.left, 0, expected - options.sampleRate / 10);
            const double after = peak(buffer.left, expected, expected + options.sampleRate / 4);
            QVERIFY2(after > 0.001,
                     qPrintable(QStringLiteral("in blocks of %1 the note is missing").arg(block)));
            QVERIFY2(before < 0.001,
                     qPrintable(QStringLiteral("in blocks of %1 the note sounds %2 samples early")
                                    .arg(block)
                                    .arg(expected - 1)));
        }
    }

    /** Seeking silences what was ringing, which is what every sequencer does. */
    void seekingSilencesWhatWasSounding()
    {
        if (!haveSoundFont()) {
            QSKIP("no SoundFont on this machine; install fluid-soundfont-gm");
        }
        const Score score = oneBar();
        const QList<int> order = Timeline::playedOrder(score);
        const Timeline::Clock clock(score, order);
        TrackSynth::Options options;
        options.soundFont = Render::findSoundFont();
        TrackSynth synth(score.tracks.first(), Timeline::messagesFor(score, 0, order), clock,
                         options);
        QVERIFY(synth.isValid());

        const int frames = options.sampleRate / 4;
        Buffer sounding(frames);
        synth.fill(sounding.left.data(), sounding.right.data(), frames, 0);
        QVERIFY(peak(sounding.left, 0, frames) > 0.001);

        // Somewhere with no note of its own, so anything heard there is a note
        // that was left ringing rather than one that belongs.
        synth.seek(qint64(options.sampleRate * 4));
        Buffer after(frames);
        synth.fill(after.left.data(), after.right.data(), frames, qint64(options.sampleRate * 4));
        const double was = peak(sounding.left, 0, frames);
        // Not silence, and the difference is worth stating. The voices are
        // cut, which is what a seek must do; what is left is the reverb the
        // notes were played into, and a reverb does not stop because somebody
        // moved the playhead. So what is asserted is that the notes are gone --
        // an order of magnitude down within an eighth of a second -- rather
        // than that the buffer is empty, which it never will be.
        const double ringing = peak(after.left, frames / 8, frames);
        QVERIFY2(ringing < was / 5,
                 qPrintable(QStringLiteral("a note was left ringing across a seek: %1 against %2")
                                .arg(ringing)
                                .arg(was)));
    }

    /**
     * Two tracks are two synths, and what comes out of them differs.
     *
     * The whole argument for one synth per track: if the same score gave the
     * same audio whichever track was asked for, there would be no stems in it.
     */
    void eachTrackIsItsOwnInstrument()
    {
        if (!haveSoundFont()) {
            QSKIP("no SoundFont on this machine; install fluid-soundfont-gm");
        }
        Score score = oneBar();
        Track bass;
        bass.name = QStringLiteral("Bass");
        bass.instrumentType = QStringLiteral("electricBass");
        bass.program = 33;
        bass.tuning = {28, 33, 38, 43};
        score.tracks.append(bass);
        // The second track plays the same bar, an octave down.
        for (MasterBar &master : score.masterBars) {
            master.bars.append(1);
        }
        score.bars.insert(1, Bar{{1, -1, -1, -1}});
        QList<int> beats;
        for (int index = 0; index < 4; ++index) {
            Note note;
            note.string = 0;
            note.fret = index * 2;
            note.midi = 28 + note.fret;
            score.notes.insert(100 + index, note);
            Beat beat;
            beat.rhythm = 0;
            beat.notes = {100 + index};
            score.beats.insert(100 + index, beat);
            beats.append(100 + index);
        }
        score.voices.insert(1, Voice{beats});

        const QList<int> order = Timeline::playedOrder(score);
        const Timeline::Clock clock(score, order);
        TrackSynth::Options options;
        options.soundFont = Render::findSoundFont();

        const int frames = options.sampleRate / 2;
        Buffer guitar(frames);
        Buffer lower(frames);
        {
            TrackSynth synth(score.tracks.at(0), Timeline::messagesFor(score, 0, order), clock,
                             options);
            synth.fill(guitar.left.data(), guitar.right.data(), frames, 0);
        }
        {
            TrackSynth synth(score.tracks.at(1), Timeline::messagesFor(score, 1, order), clock,
                             options);
            synth.fill(lower.left.data(), lower.right.data(), frames, 0);
        }

        QVERIFY(peak(guitar.left, 0, frames) > 0.001);
        QVERIFY(peak(lower.left, 0, frames) > 0.001);
        bool differ = false;
        for (int index = 0; index < frames && !differ; ++index) {
            differ = !qFuzzyCompare(1.0f + guitar.left[size_t(index)],
                                    1.0f + lower.left[size_t(index)]);
        }
        QVERIFY2(differ, "two tracks rendered identical audio");
    }
};

QTEST_GUILESS_MAIN(TrackSynthTest)
#include "tracksynthtest.moc"
