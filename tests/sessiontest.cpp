// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "session.h"

#include "fwformat.h"
#include "lv2chain.h"
#include "renderer.h"
#include "rigfile.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

/**
 * A chain being edited in the middle of itself.
 *
 * The window can append a plugin, move one along the signal path and take one
 * off from anywhere, and each of those rebuilds the chain -- so the question
 * every test here asks is the same one: did the settings go with the stage
 * they belong to, or did they stay at the number that stage used to be?
 *
 * It is asked of a real `Session` driving a real `Player`, because the fault
 * this guards against is not in the list arithmetic. Reordering a list is
 * three lines nobody gets wrong. What was easy to get wrong was the four
 * containers that used to be keyed by a stage's position in that list, and
 * anything that reads them afterwards.
 *
 * The plugins are whatever this machine has rather than any named one, since
 * a suite that needs guitarix installed is a suite that does not run.
 */
class SessionTest : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_directory;
    QStringList m_uris;

    // A score of its own for each test, because a rig is kept beside the score
    // and a second Session opened on the same path would start with the first
    // one's chain already on it. That is the feature working; here it is one
    // test standing in the next one's way.
    int m_scores = 0;

    static Score oneTrack()
    {
        Score score;
        Track track;
        track.name = QStringLiteral("Guitar");
        track.instrumentType = QStringLiteral("electricGuitar");
        for (int string = 0; string < 6; ++string) {
            track.tuning.append(40 + string * 5);
        }
        score.tracks.append(track);

        MasterBar bar;
        bar.bars = {0};
        score.masterBars.append(bar);
        score.rhythms.insert(0, Rational(1));
        score.tempos.append({0, 0, 120});

        Note note;
        note.midi = 64;
        note.string = 5;
        score.notes.insert(0, note);
        score.beats.insert(0, Beat{0, {0}, Dynamic::F, false, false});
        score.voices.insert(0, Voice{{0}});
        score.bars.insert(0, Bar{{0, -1, -1, -1}});
        return score;
    }

    /** The first control on a stage that is a knob rather than a switch. */
    static int knobOn(const QVariantList &chain, int stage)
    {
        const QVariantList controls =
            chain.at(stage).toMap().value(QStringLiteral("controls")).toList();
        for (const QVariant &one : controls) {
            const QVariantMap control = one.toMap();
            if (!control.value(QStringLiteral("toggled")).toBool()
                && control.value(QStringLiteral("choices")).toStringList().isEmpty()) {
                return control.value(QStringLiteral("index")).toInt();
            }
        }
        return -1;
    }

    static double valueOn(const QVariantList &chain, int stage, int index)
    {
        const QVariantList controls =
            chain.at(stage).toMap().value(QStringLiteral("controls")).toList();
        for (const QVariant &one : controls) {
            const QVariantMap control = one.toMap();
            if (control.value(QStringLiteral("index")).toInt() == index) {
                return control.value(QStringLiteral("value")).toDouble();
            }
        }
        return -1;
    }

    /** A score on disk with two plugins on its only part, and where it is. */
    QString twoOnTheChain(Session &session)
    {
        const QString path =
            m_directory.path() + QStringLiteral("/score%1.fw").arg(++m_scores);
        [&] {
            QVERIFY(Fw::write(oneTrack(), path));
            QVERIFY(session.open(path));
        }();
        session.addEffect(m_uris.at(0));
        session.addEffect(m_uris.at(1));
        [&] { QCOMPARE(session.chainOn(0), m_uris); }();
        return path;
    }

private Q_SLOTS:
    void initTestCase()
    {
        if (Render::findSoundFont().isEmpty()) {
            QSKIP("no SoundFont on this machine; install fluid-soundfont-gm");
        }
        // Two plugins with a knob each is the smallest chain that can be put
        // in the wrong order, and mono ones are taken first because a mono
        // plugin in a stereo chain is instantiated twice -- which is the case
        // where a stage's settings have two places to go astray rather than
        // one.
        for (const Lv2::Description &plugin : Lv2::installed()) {
            if (plugin.audioInputs == 1 && plugin.audioOutputs == 1
                && knobCount(plugin.uri) > 0) {
                m_uris.append(plugin.uri);
            }
            if (m_uris.size() == 2) {
                break;
            }
        }
        if (m_uris.size() < 2) {
            QSKIP("fewer than two usable LV2 plugins on this machine");
        }
        QVERIFY(m_directory.isValid());
    }

    static int knobCount(const QString &uri)
    {
        int knobs = 0;
        for (const Lv2::Control &control : Lv2::controlsOf(uri)) {
            if (!control.toggled && control.choices.isEmpty()) {
                ++knobs;
            }
        }
        return knobs;
    }

    /**
     * The order is the point of the panel, and it was the one thing that could
     * not be said without emptying the chain.
     */
    void movesAStageAlongTheSignalPath()
    {
        Session session;
        twoOnTheChain(session);

        session.moveEffect(0, 1);
        QCOMPARE(session.chainOn(0), QStringList({m_uris.at(1), m_uris.at(0)}));

        session.moveEffect(1, -1);
        QCOMPARE(session.chainOn(0), m_uris);
    }

    /** A move off either end is not a move, and must not be a crash. */
    void refusesAMoveThatWouldLeaveTheChain()
    {
        Session session;
        twoOnTheChain(session);

        session.moveEffect(0, -1);
        session.moveEffect(1, 1);
        session.moveEffect(7, -1);
        session.moveEffect(-3, 1);
        QCOMPARE(session.chainOn(0), m_uris);
    }

    /**
     * The whole reason a stage owns its settings.
     *
     * A knob is held by port index, and a port index means something only
     * inside the plugin it came from. Kept beside the chain rather than in it,
     * the number stays where it was while the plugin moves out from under it,
     * and the reading afterwards belongs to whatever is standing at that
     * position now.
     */
    void carriesTheKnobsWithTheStageThatWasMoved()
    {
        Session session;
        twoOnTheChain(session);

        const int knob = knobOn(session.chainHere(), 0);
        QVERIFY(knob >= 0);
        const double before = valueOn(session.chainHere(), 0, knob);
        const double wanted = before + 0.25;
        session.setEffectControl(0, knob, wanted);
        QCOMPARE(valueOn(session.chainHere(), 0, knob), wanted);

        session.moveEffect(0, 1);

        // Where the plugin went, and nowhere else.
        QCOMPARE(valueOn(session.chainHere(), 1, knob), wanted);
        QVERIFY(!qFuzzyCompare(valueOn(session.chainHere(), 0, knob), wanted)
                || m_uris.at(0) == m_uris.at(1));
    }

    /**
     * Taking one off from the middle, which "Remove last" could only do by
     * taking off everything after it first.
     */
    void takesAStageOffFromAnywhereInTheChain()
    {
        Session session;
        twoOnTheChain(session);

        const int knob = knobOn(session.chainHere(), 1);
        QVERIFY(knob >= 0);
        const double wanted = valueOn(session.chainHere(), 1, knob) + 0.25;
        session.setEffectControl(1, knob, wanted);

        session.removeEffect(0);
        QCOMPARE(session.chainOn(0), QStringList({m_uris.at(1)}));

        // The survivor kept what was set on it, having moved down a place.
        QCOMPARE(valueOn(session.chainHere(), 0, knob), wanted);
    }

    void refusesToRemoveAStageThatIsNotThere()
    {
        Session session;
        twoOnTheChain(session);

        session.removeEffect(9);
        session.removeEffect(-1);
        QCOMPARE(session.chainOn(0), m_uris);
    }

    /**
     * The rig beside the score is written from the same stages, so an order
     * changed in the window is the order that comes back tomorrow.
     */
    void keepsAReorderedChainInTheRig()
    {
        Session session;
        const QString path = twoOnTheChain(session);
        session.moveEffect(0, 1);
        const QStringList moved = session.chainOn(0);

        // A rig is written the better part of a second after it stops
        // changing, because a knob under a finger moves continuously and the
        // file is worth writing once when the finger comes off. Nothing on
        // screen waits for that; a test reading the file does.
        QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(Rig::pathFor(path)), 4000);

        Session reopened;
        QVERIFY(reopened.open(path));
        QCOMPARE(reopened.chainOn(0), moved);
    }
};

QTEST_MAIN(SessionTest)
#include "sessiontest.moc"
