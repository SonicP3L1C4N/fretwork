// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "rigfile.h"

#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

/**
 * The rig file that sits beside a score.
 *
 * What is being tested is mostly what happens to a file nobody wrote by hand:
 * a rig arrives from an earlier version, a later version, a text editor
 * somebody was brave in, or not at all. A rig that loads half-way is a sound
 * nobody chose, so the choices between refusing and reading past are the ones
 * worth pinning down.
 */
class RigTest : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_directory;

    QString path(const QString &name) const
    {
        return m_directory.path() + QLatin1Char('/') + name;
    }

    QString writeText(const QString &name, const QByteArray &text) const
    {
        const QString where = path(name);
        QFile file(where);
        [&] { QVERIFY(file.open(QIODevice::WriteOnly)); }();
        file.write(text);
        file.close();
        return where;
    }

    /** A rig with a sampler on one track and an amplifier on the other. */
    static Rig::Document twoTracks()
    {
        Rig::Document rig;

        Rig::Track guitar;
        guitar.track = 0;
        guitar.chain = {QStringLiteral("urn:example:amp"),
                        QStringLiteral("urn:example:cabinet")};
        guitar.knobs = {{0, QStringLiteral("Drive"), 0.8f},
                        {0, QStringLiteral("Bass"), 0.25f},
                        {1, QStringLiteral("Level"), 0.5f}};

        Rig::Track bass;
        bass.track = 1;
        bass.sampler = QStringLiteral("/home/somebody/samples/Growlybass.sfz");

        rig.tracks = {guitar, bass};
        return rig;
    }

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(m_directory.isValid());
    }

    /**
     * The whole name and then the suffix, so that a score imported from
     * `Horses.gp` and saved as `Horses.fw` do not fight over one rig.
     */
    void sitsBesideTheScoreUnderItsWholeName()
    {
        QCOMPARE(Rig::pathFor(QStringLiteral("/music/Horses.gp")),
                 QStringLiteral("/music/Horses.gp.rig"));
        QCOMPARE(Rig::pathFor(QStringLiteral("/music/Horses.fw")),
                 QStringLiteral("/music/Horses.fw.rig"));
        QVERIFY(Rig::pathFor(QString()).isEmpty());
    }

    void carriesEveryPartOfARigThroughARoundTrip()
    {
        const QString file = path(QStringLiteral("round.rig"));
        QString why;
        QVERIFY2(Rig::write(twoTracks(), file, &why), qPrintable(why));

        const Rig::Document back = Rig::read(file, &why);
        QVERIFY2(why.isEmpty(), qPrintable(why));
        QCOMPARE(back.tracks.size(), 2);

        QCOMPARE(back.tracks.at(0).track, 0);
        QCOMPARE(back.tracks.at(0).chain,
                 QStringList({QStringLiteral("urn:example:amp"),
                              QStringLiteral("urn:example:cabinet")}));
        QCOMPARE(back.tracks.at(0).knobs.size(), 3);
        QCOMPARE(back.tracks.at(0).knobs.at(0).stage, 0);
        QCOMPARE(back.tracks.at(0).knobs.at(0).symbol, QStringLiteral("Drive"));
        QCOMPARE(back.tracks.at(0).knobs.at(0).value, 0.8f);
        QCOMPARE(back.tracks.at(0).knobs.at(2).stage, 1);

        QCOMPARE(back.tracks.at(1).track, 1);
        QCOMPARE(back.tracks.at(1).sampler,
                 QStringLiteral("/home/somebody/samples/Growlybass.sfz"));
        QVERIFY(back.tracks.at(1).chain.isEmpty());
    }

    /** The order of a chain is the signal path, so it is not a set. */
    void keepsTheChainInTheOrderItWasBuilt()
    {
        Rig::Document rig;
        Rig::Track track;
        track.track = 0;
        for (int stage = 0; stage < 6; ++stage) {
            track.chain.append(QStringLiteral("urn:example:%1").arg(stage));
        }
        rig.tracks = {track};

        const QString file = path(QStringLiteral("order.rig"));
        QVERIFY(Rig::write(rig, file));
        QCOMPARE(Rig::read(file).tracks.at(0).chain, track.chain);
    }

    /**
     * A knob is filed under the plugin's own name for it, because a port index
     * is where a control sits in one build and the symbol is what the plugin
     * publishes. This is the assertion that keeps it that way.
     */
    void namesKnobsBySymbolInTheFileItself()
    {
        const QString file = path(QStringLiteral("symbols.rig"));
        QVERIFY(Rig::write(twoTracks(), file));

        QFile handle(file);
        QVERIFY(handle.open(QIODevice::ReadOnly));
        const QByteArray text = handle.readAll();
        QVERIFY2(text.contains("\"symbol\""), text.constData());
        QVERIFY2(text.contains("Drive"), text.constData());
    }

    /** A score nobody has built a rig for is not a problem to report. */
    void aMissingRigIsEmptyAndQuiet()
    {
        QString why = QStringLiteral("untouched");
        const Rig::Document rig = Rig::read(path(QStringLiteral("nothing-here.rig")), &why);
        QVERIFY(rig.isEmpty());
        QCOMPARE(why, QStringLiteral("untouched"));
    }

    /**
     * An empty rig is no file rather than an empty file, and clearing a rig
     * takes the old one away -- otherwise a person who stripped a score back to
     * dry would find the amplifier again the next time they opened it.
     */
    void anEmptyRigLeavesNoFileBehind()
    {
        const QString file = path(QStringLiteral("cleared.rig"));
        QVERIFY(Rig::write(twoTracks(), file));
        QVERIFY(QFileInfo::exists(file));

        QVERIFY(Rig::write(Rig::Document(), file));
        QVERIFY2(!QFileInfo::exists(file), "a cleared rig left its old file behind");
    }

    /** A track with nothing on it says nothing, so it is not written. */
    void doesNotWriteTracksThatSayNothing()
    {
        Rig::Document rig;
        Rig::Track bare;
        bare.track = 0;
        Rig::Track real;
        real.track = 1;
        real.chain = {QStringLiteral("urn:example:amp")};
        rig.tracks = {bare, real};

        const QString file = path(QStringLiteral("sparse.rig"));
        QVERIFY(Rig::write(rig, file));

        const Rig::Document back = Rig::read(file);
        QCOMPARE(back.tracks.size(), 1);
        QCOMPARE(back.tracks.at(0).track, 1);
    }

    /**
     * A key a later version added is read past, on the same terms as `Fw`: a
     * rig with more in it than this reader knows about still opens.
     */
    void readsPastKeysItDoesNotKnow()
    {
        const QString file = writeText(QStringLiteral("newer-keys.rig"), R"({
            "rig": 1,
            "mystery": "something a later version cared about",
            "tracks": [
                { "track": 0,
                  "chain": ["urn:example:amp"],
                  "cabinet": "an impulse response, one day",
                  "knobs": [ { "stage": 0, "symbol": "Drive", "value": 0.7,
                               "curve": "logarithmic" } ] }
            ]
        })");

        QString why;
        const Rig::Document rig = Rig::read(file, &why);
        QVERIFY2(why.isEmpty(), qPrintable(why));
        QCOMPARE(rig.tracks.size(), 1);
        QCOMPARE(rig.tracks.at(0).knobs.size(), 1);
        QCOMPARE(rig.tracks.at(0).knobs.at(0).value, 0.7f);
    }

    /**
     * A version this reader cannot vouch for is refused whole rather than read
     * as far as it goes. Half a rig is a sound nobody chose.
     */
    void refusesARigFromALaterVersion()
    {
        const QString file = writeText(QStringLiteral("from-the-future.rig"), R"({
            "rig": 99,
            "tracks": [ { "track": 0, "chain": ["urn:example:amp"] } ]
        })");

        QString why;
        const Rig::Document rig = Rig::read(file, &why);
        QVERIFY(rig.isEmpty());
        QVERIFY2(!why.isEmpty(), "a rig from the future was read without a word");
    }

    void saysWhichRigItCouldNotParse()
    {
        const QString file = writeText(QStringLiteral("mangled.rig"),
                                       "{ \"rig\": 1, \"tracks\": [ oh dear");
        QString why;
        const Rig::Document rig = Rig::read(file, &why);
        QVERIFY(rig.isEmpty());
        QVERIFY2(why.contains(QStringLiteral("mangled.rig")), qPrintable(why));
    }

    /**
     * Entries that name nothing are read past rather than turned into knobs on
     * plugin -1 or tracks nobody has.
     */
    void readsPastEntriesThatNameNothing()
    {
        const QString file = writeText(QStringLiteral("nonsense.rig"), R"({
            "rig": 1,
            "tracks": [
                { "track": -3, "chain": ["urn:example:ghost"] },
                { "chain": ["urn:example:nameless"] },
                { "track": 0,
                  "chain": ["urn:example:amp", "", "urn:example:cabinet"],
                  "knobs": [ { "stage": 0, "symbol": "", "value": 1 },
                             { "stage": -1, "symbol": "Drive", "value": 1 },
                             { "stage": 0, "symbol": "Bass", "value": 0.4 } ] }
            ]
        })");

        QString why;
        const Rig::Document rig = Rig::read(file, &why);
        QVERIFY2(why.isEmpty(), qPrintable(why));
        QCOMPARE(rig.tracks.size(), 1);
        QCOMPARE(rig.tracks.at(0).track, 0);
        // The empty URI in the middle goes; the two real ones stay in order.
        QCOMPARE(rig.tracks.at(0).chain,
                 QStringList({QStringLiteral("urn:example:amp"),
                              QStringLiteral("urn:example:cabinet")}));
        QCOMPARE(rig.tracks.at(0).knobs.size(), 1);
        QCOMPARE(rig.tracks.at(0).knobs.at(0).symbol, QStringLiteral("Bass"));
    }

    void saysSoWhenItCannotWrite()
    {
        QString why;
        QVERIFY(!Rig::write(twoTracks(),
                            path(QStringLiteral("no-such-directory/deep.rig")), &why));
        QVERIFY(!why.isEmpty());
    }
};

QTEST_GUILESS_MAIN(RigTest)
#include "rigtest.moc"
