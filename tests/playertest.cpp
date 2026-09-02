// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "player.h"
#include "renderer.h"
#include "timeline.h"

#include <QTest>

/**
 * The transport and the mixer.
 *
 * Neither needs an audio device: the channels exist before the driver is
 * opened, so mute, solo and gain can be asked and answered on a machine with
 * no sound card at all. What cannot be tested here is the callback -- checking
 * that requires listening, or the offline renderer, which shares the same
 * synth and is tested against the arithmetic instead.
 */
class PlayerTest : public QObject
{
    Q_OBJECT

private:
    /** Three tracks of one bar each, enough to have something to solo. */
    static Score threeTracks()
    {
        Score score;
        for (int index = 0; index < 3; ++index) {
            Track track;
            track.name = QStringLiteral("Track %1").arg(index);
            track.instrumentType = index == 2 ? QStringLiteral("drumKit")
                                              : QStringLiteral("electricGuitar");
            if (index != 2) {
                for (int string = 0; string < 6; ++string) {
                    track.tuning.append(40 + string * 5);
                }
            }
            score.tracks.append(track);
        }

        MasterBar bar;
        bar.bars = {0, 1, 2};
        score.masterBars.append(bar);
        score.rhythms.insert(0, Rational(1));
        score.tempos.append({0, 0, 120});

        for (int index = 0; index < 3; ++index) {
            Note note;
            note.midi = 64;
            note.string = index == 2 ? -1 : 5;
            score.notes.insert(index, note);
            score.beats.insert(index, Beat{0, {index}, Dynamic::F, false, false});
            score.voices.insert(index, Voice{{index}});
            score.bars.insert(index, Bar{{index, -1, -1, -1}});
        }
        return score;
    }

    static Player::Options options()
    {
        Player::Options out;
        // Ask for a driver that will not open, so the test never takes over the
        // speakers of whoever is running it. Everything under test is built
        // before the driver is.
        out.audioDriver = QStringLiteral("this-driver-does-not-exist");
        return out;
    }

private Q_SLOTS:
    void initTestCase()
    {
        if (Render::findSoundFont().isEmpty()) {
            QSKIP("no SoundFont on this machine; install fluid-soundfont-gm");
        }
    }

    void aStoppedTransportIsWhereItIsAtAnyMoment()
    {
        const Score score = threeTracks();
        Player player(score, Timeline::playedOrder(score), options());
        // Nothing is moving, so a moment on the clock -- any moment, even one
        // that is plainly wrong -- reads as the position and not past it.
        QCOMPARE(player.positionSecondsAt(0), player.positionSeconds());
        QCOMPARE(player.positionSecondsAt(1), player.positionSeconds());
        QCOMPARE(player.positionSecondsAt(1LL << 60), player.positionSeconds());
        // A block of 512 frames at 48 kHz.
        QVERIFY(std::abs(player.periodSeconds() - 512.0 / 48000.0) < 1e-9);
    }

    void knowsHowManyTracksItHas()
    {
        const Score score = threeTracks();
        Player player(score, Timeline::playedOrder(score), options());
        QCOMPARE(player.trackCount(), 3);
    }

    /** Everything is heard until something says otherwise. */
    void everyTrackIsAudibleToStartWith()
    {
        const Score score = threeTracks();
        Player player(score, Timeline::playedOrder(score), options());
        for (int track = 0; track < 3; ++track) {
            QVERIFY(player.isAudible(track));
            QVERIFY(!player.isMuted(track));
            QVERIFY(!player.isSolo(track));
            QCOMPARE(player.gain(track), 1.0f);
        }
    }

    void mutingSilencesOneTrackAndLeavesTheRest()
    {
        const Score score = threeTracks();
        Player player(score, Timeline::playedOrder(score), options());
        player.setMuted(1, true);

        QVERIFY(player.isAudible(0));
        QVERIFY(!player.isAudible(1));
        QVERIFY(player.isAudible(2));

        player.setMuted(1, false);
        QVERIFY(player.isAudible(1));
    }

    /**
     * Solo is not the opposite of mute: one track soloed silences every track
     * that is not, muted or otherwise.
     */
    void soloingOneTrackSilencesTheOthers()
    {
        const Score score = threeTracks();
        Player player(score, Timeline::playedOrder(score), options());
        player.setSolo(1, true);

        QVERIFY(!player.isAudible(0));
        QVERIFY(player.isAudible(1));
        QVERIFY(!player.isAudible(2));
    }

    void severalTracksCanBeSoloedTogether()
    {
        const Score score = threeTracks();
        Player player(score, Timeline::playedOrder(score), options());
        player.setSolo(0, true);
        player.setSolo(2, true);

        QVERIFY(player.isAudible(0));
        QVERIFY(!player.isAudible(1));
        QVERIFY(player.isAudible(2));
    }

    /** Taking the last solo off puts everything back, mutes included. */
    void unsoloingRestoresWhatTheMutesSay()
    {
        const Score score = threeTracks();
        Player player(score, Timeline::playedOrder(score), options());
        player.setMuted(0, true);
        player.setSolo(1, true);
        QVERIFY(!player.isAudible(0));

        player.setSolo(1, false);
        QVERIFY(!player.isAudible(0));      // still muted
        QVERIFY(player.isAudible(1));
        QVERIFY(player.isAudible(2));
    }

    /** Setting the same solo twice must not leave the count wrong for ever. */
    void soloingTwiceIsSoloingOnce()
    {
        const Score score = threeTracks();
        Player player(score, Timeline::playedOrder(score), options());
        player.setSolo(1, true);
        player.setSolo(1, true);
        player.setSolo(1, false);

        for (int track = 0; track < 3; ++track) {
            QVERIFY2(player.isAudible(track), "a solo was counted twice and never let go");
        }
    }

    void gainIsRememberedAndKeptSane()
    {
        const Score score = threeTracks();
        Player player(score, Timeline::playedOrder(score), options());
        player.setGain(0, 0.5f);
        QCOMPARE(player.gain(0), 0.5f);

        player.setGain(0, -3.0f);
        QCOMPARE(player.gain(0), 0.0f);
        player.setGain(0, 99.0f);
        QCOMPARE(player.gain(0), 4.0f);
    }

    void ignoresTracksThatAreNotThere()
    {
        const Score score = threeTracks();
        Player player(score, Timeline::playedOrder(score), options());
        player.setMuted(9, true);
        player.setSolo(-1, true);
        player.setGain(400, 2.0f);

        QVERIFY(!player.isAudible(9));
        QVERIFY(!player.isMuted(-1));
        QCOMPARE(player.gain(9), 0.0f);
        // None of that may have made the rest inaudible by counting a solo.
        QVERIFY(player.isAudible(0));
    }

    // ---- transport ----

    void lengthCoversTheScoreAndItsTail()
    {
        const Score score = threeTracks();
        Player player(score, Timeline::playedOrder(score), options());
        // One quarter at 120 is half a second, plus the decay.
        QVERIFY(player.lengthSeconds() > 3.0);
        QVERIFY(player.lengthSeconds() < 10.0);
    }

    void seekingStaysInsideThePiece()
    {
        const Score score = threeTracks();
        Player player(score, Timeline::playedOrder(score), options());

        player.seekSeconds(-5);
        QVERIFY(player.positionSeconds() >= 0);

        player.seekSeconds(1000);
        QVERIFY(player.positionSeconds() <= player.lengthSeconds());
    }

    void saysWhyWhenItCannotPlay()
    {
        const Score score = threeTracks();
        Player player(score, Timeline::playedOrder(score), options());
        QVERIFY(!player.isValid());
        QVERIFY2(player.error().contains(QStringLiteral("this-driver-does-not-exist")),
                 qPrintable(player.error()));
    }

    /**
     * The playback half of the same question the renderer answers.
     *
     * A stranger who presses play with no SoundFont installed hears nothing,
     * and the only thing standing between that and "the program is broken" is
     * this message. It has to name the file it could not load.
     */
    void saysWhichSoundFontItCouldNotLoad()
    {
        const Score score = threeTracks();
        Player::Options bad = options();
        bad.soundFont = QStringLiteral("/nowhere/absent.sf2");
        Player player(score, Timeline::playedOrder(score), bad);
        QVERIFY(!player.isValid());
        QVERIFY2(player.error().contains(QStringLiteral("absent.sf2")),
                 qPrintable(player.error()));
    }

    void refusesAnEmptyScore()
    {
        Player player(Score(), {}, options());
        QVERIFY(!player.isValid());
        QVERIFY(!player.error().isEmpty());
        QCOMPARE(player.trackCount(), 0);
    }
};

QTEST_GUILESS_MAIN(PlayerTest)
#include "playertest.moc"
