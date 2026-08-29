// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "gpif.h"
#include "timeline.h"

#include <KZip>

#include <QDir>
#include <QTemporaryDir>
#include <QElapsedTimer>
#include <QTest>

/**
 * The importer, against documents this file writes itself.
 *
 * Guitar Pro transcriptions are not ours to commit, and a test suite that only
 * passes on the author's machine is not a test suite. Everything structural is
 * checked against a score built here; the real corpus is checked separately,
 * and skipped where it is absent.
 */
class GpifTest : public QObject
{
    Q_OBJECT

private:
    /** A score.gpif holding one guitar and one drum kit, and four bars. */
    static QByteArray document()
    {
        return QByteArrayLiteral(R"(<?xml version="1.0" encoding="UTF-8"?>
<GPIF>
  <GPVersion>8.1.3</GPVersion>
  <Score><Title>Test Piece</Title><Artist>Nobody</Artist></Score>
  <MasterTrack>
    <Tracks>0 1</Tracks>
    <Automations>
      <Automation><Type>Tempo</Type><Bar>0</Bar><Position>0</Position><Value>120 2</Value></Automation>
      <Automation><Type>Tempo</Type><Bar>2</Bar><Position>0</Position><Value>60 2</Value></Automation>
    </Automations>
  </MasterTrack>
  <Tracks>
    <Track id="0">
      <Name>Guitar</Name>
      <InstrumentSet><Type>electricGuitar</Type></InstrumentSet>
      <Sounds><Sound><MIDI><Program>29</Program></MIDI></Sound></Sounds>
      <Staves><Staff><Properties>
        <Property name="Tuning"><Pitches>40 45 50 55 59 64</Pitches></Property>
        <Property name="CapoFret"><Fret>2</Fret></Property>
      </Properties></Staff></Staves>
    </Track>
    <Track id="1">
      <Name>Drums</Name>
      <InstrumentSet><Type>drumKit</Type></InstrumentSet>
      <Sounds><Sound><MIDI><Program>0</Program></MIDI></Sound></Sounds>
    </Track>
  </Tracks>
  <MasterBars>
    <MasterBar><Time>4/4</Time><Bars>0 4</Bars></MasterBar>
    <MasterBar><Time>4/4</Time><Bars>1 5</Bars><Repeat start="true" end="false" count="0"/></MasterBar>
    <MasterBar><Time>3/4</Time><Bars>2 6</Bars><Repeat start="false" end="true" count="2"/></MasterBar>
    <MasterBar><Time>4/4</Time><Bars>3 7</Bars></MasterBar>
  </MasterBars>
  <Bars>
    <Bar id="0"><Voices>0 -1 -1 -1</Voices></Bar>
    <Bar id="1"><Voices>1 -1 -1 -1</Voices></Bar>
    <Bar id="2"><Voices>2 -1 -1 -1</Voices></Bar>
    <Bar id="3"><Voices>-1 -1 -1 -1</Voices></Bar>
    <Bar id="4"><Voices>-1 -1 -1 -1</Voices></Bar>
    <Bar id="5"><Voices>-1 -1 -1 -1</Voices></Bar>
    <Bar id="6"><Voices>-1 -1 -1 -1</Voices></Bar>
    <Bar id="7"><Voices>3 -1 -1 -1</Voices></Bar>
  </Bars>
  <Voices>
    <Voice id="0"><Beats>0 1</Beats></Voice>
    <Voice id="1"><Beats>2</Beats></Voice>
    <Voice id="2"><Beats>3</Beats></Voice>
    <Voice id="3"><Beats>4</Beats></Voice>
  </Voices>
  <Beats>
    <Beat id="0"><Rhythm ref="0"/><Dynamic>F</Dynamic><Notes>0</Notes></Beat>
    <Beat id="1"><Rhythm ref="0"/><Dynamic>F</Dynamic><Notes>1</Notes></Beat>
    <Beat id="2"><Rhythm ref="1"/><Dynamic>PP</Dynamic><Notes>2</Notes></Beat>
    <Beat id="3"><Rhythm ref="0"/><Dynamic>MF</Dynamic></Beat>
    <Beat id="4"><Rhythm ref="0"/><Dynamic>MF</Dynamic><Notes>3</Notes></Beat>
  </Beats>
  <Notes>
    <Note id="0"><Properties>
      <Property name="Midi"><Number>64</Number></Property>
      <Property name="String"><String>5</String></Property>
      <Property name="Fret"><Fret>0</Fret></Property>
      <Property name="PalmMuted"><Enable/></Property>
      <Property name="Slide"><Flags>2</Flags></Property>
    </Properties><Tie origin="true" destination="false"/></Note>
    <Note id="1"><Properties>
      <Property name="Midi"><Number>64</Number></Property>
      <Property name="String"><String>5</String></Property>
    </Properties><Tie origin="false" destination="true"/></Note>
    <Note id="2"><Properties>
      <Property name="Midi"><Number>40</Number></Property>
      <Property name="String"><String>0</String></Property>
      <Property name="Bended"><Enable/></Property>
      <Property name="BendDestinationValue"><Float>200.000000</Float></Property>
    </Properties></Note>
    <Note id="3"><Properties>
      <Property name="Midi"><Number>50</Number></Property>
      <Property name="String"><String>2</String></Property>
    </Properties><Accent/></Note>
  </Notes>
  <Rhythms>
    <Rhythm id="0"><NoteValue>Quarter</NoteValue></Rhythm>
    <Rhythm id="1"><NoteValue>Eighth</NoteValue><AugmentationDot count="1"/><PrimaryTuplet num="3" den="2"/></Rhythm>
  </Rhythms>
</GPIF>
)");
    }

    /** Wraps a document the way Guitar Pro does, so read() has a file to open. */
    static QString wrap(const QString &directory, const QByteArray &contents,
                        const QString &entry = QStringLiteral("Content/score.gpif"))
    {
        const QString path = directory + QStringLiteral("/test.gp");
        KZip archive(path);
        if (!archive.open(QIODevice::WriteOnly)) {
            return {};
        }
        archive.writeFile(entry, contents);
        archive.close();
        return path;
    }

    QTemporaryDir m_directory;

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(m_directory.isValid());
    }

    // ---- the container ----

    void readsAScoreOutOfItsZip()
    {
        QString why;
        const Score score = Gpif::read(wrap(m_directory.path(), document()), &why);
        QVERIFY2(!score.isEmpty(), qPrintable(why));
        QCOMPARE(score.title, QStringLiteral("Test Piece"));
        QCOMPARE(score.artist, QStringLiteral("Nobody"));
        QCOMPARE(score.version, QStringLiteral("8.1.3"));
    }

    /**
     * A GP6 file is a ZIP too, and its contents are not this. Saying which
     * format it is not is the difference between a bug report and a shrug.
     */
    void saysSoWhenTheZipIsNotAGuitarPro7File()
    {
        QString why;
        const QString path = wrap(m_directory.path(), QByteArrayLiteral("nope"),
                                  QStringLiteral("BCFZ/whatever"));
        QVERIFY(Gpif::read(path, &why).isEmpty());
        QVERIFY2(why.contains(QStringLiteral("Guitar Pro 7")), qPrintable(why));
    }

    void refusesWhatIsNotAZipAtAll()
    {
        const QString path = m_directory.path() + QStringLiteral("/plain.gp");
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QByteArrayLiteral("this is not a container"));
        file.close();

        QString why;
        QVERIFY(Gpif::read(path, &why).isEmpty());
        QVERIFY(!why.isEmpty());
    }

    void refusesAFileThatIsNotThere()
    {
        QString why;
        QVERIFY(Gpif::read(m_directory.path() + QStringLiteral("/absent.gp"), &why).isEmpty());
        QCOMPARE(why, QStringLiteral("no such file"));
    }

    /** Malformed XML is a wrong answer, not a crash and not a silent empty score. */
    void refusesBrokenXmlWithAReason()
    {
        QString why;
        QVERIFY(Gpif::parse(QByteArrayLiteral("<GPIF><Score></GPIF>"), &why).isEmpty());
        QVERIFY(!why.isEmpty());

        QVERIFY(Gpif::parse(QByteArrayLiteral("<html><body/></html>"), &why).isEmpty());
        QVERIFY2(why.contains(QStringLiteral("gpif")), qPrintable(why));
    }

    /**
     * A document nested deeper than any tablature, refused before the tree is
     * built rather than after.
     *
     * QDomDocument's cost grows faster than the document does: 100,000 nested
     * empty elements are 875 bytes on disk and twelve seconds in setContent(),
     * 200,000 are a minute and a half, and neither is a crash -- the program
     * simply stops answering. The number here is far past the limit so that
     * the test says nothing about where exactly the limit sits, only that
     * there is one and that it is reached quickly.
     */
    void refusesADocumentNestedTooDeeply()
    {
        QByteArray xml = QByteArrayLiteral("<?xml version=\"1.0\"?>\n<GPIF>");
        const int depth = 50000;
        xml += QByteArray("<a>").repeated(depth);
        xml += QByteArray("</a>").repeated(depth);
        xml += QByteArrayLiteral("</GPIF>");

        QElapsedTimer timer;
        timer.start();
        QString why;
        QVERIFY(Gpif::parse(xml, &why).isEmpty());
        QVERIFY2(why.contains(QStringLiteral("nest")), qPrintable(why));
        // Generous by three orders of magnitude against the twelve seconds
        // this used to take at twice the depth: what is being asserted is that
        // it refuses without building anything, not how fast the machine is.
        QVERIFY2(timer.elapsed() < 2000, qPrintable(QString::number(timer.elapsed())));
    }

    /** And the ordinary nesting of a real document is nowhere near it. */
    void readsADocumentNestedAsDeeplyAsRealOnesAre()
    {
        QString why;
        QVERIFY(!Gpif::parse(document(), &why).isEmpty());
        QVERIFY2(why.isEmpty(), qPrintable(why));
    }

    // ---- the document ----

    void readsTuningLowestStringFirst()
    {
        const Score score = Gpif::parse(document());
        QCOMPARE(score.tracks.at(0).tuning, QList<int>({40, 45, 50, 55, 59, 64}));
        QCOMPARE(score.tracks.at(0).capo, 2);
        QCOMPARE(score.tracks.at(0).stringCount(), 6);
    }

    /**
     * A drum kit carries programme 0, which is an acoustic piano. Every score
     * in the corpus has one, so reading this from the programme rather than
     * the instrument type breaks every file there is.
     */
    void identifiesADrumKitByItsTypeAndNotItsProgramme()
    {
        const Score score = Gpif::parse(document());
        QVERIFY(!score.tracks.at(0).isPercussion());
        QVERIFY(score.tracks.at(1).isPercussion());
        QCOMPARE(score.tracks.at(1).program, 0);
        QCOMPARE(score.tracks.at(1).stringCount(), 0);
    }

    void readsTheTablesAsTablesRatherThanAsATree()
    {
        const Score score = Gpif::parse(document());
        QCOMPARE(score.masterBars.size(), 4);
        QCOMPARE(score.bars.size(), 8);
        QCOMPARE(score.beats.size(), 5);
        QCOMPARE(score.notes.size(), 4);
        // One master bar names one Bar id per track, in track order.
        QCOMPARE(score.masterBars.at(0).bars, QList<int>({0, 4}));
    }

    void readsRhythmsWithDotsAndTuplets()
    {
        const Score score = Gpif::parse(document());
        QCOMPARE(score.rhythms.value(0), Rational(1));
        // A dotted eighth in a triplet: 1/2 * 3/2 * 2/3 == 1/2.
        QCOMPARE(score.rhythms.value(1), Rational(1, 2));
    }

    void readsTechniquesOffTheirProperties()
    {
        const Score score = Gpif::parse(document());
        QVERIFY(score.notes.value(0).palmMuted);
        QCOMPARE(score.notes.value(0).slide, SlideType::Legato);
        QVERIFY(score.notes.value(0).tieOrigin);
        QVERIFY(score.notes.value(1).tieDestination);
        QVERIFY(score.notes.value(2).bended);
        QCOMPARE(score.notes.value(2).bendDestinationValue, 200);
        QVERIFY(score.notes.value(3).accent);
    }

    void readsTempoFromTheMasterTrackRatherThanTheBar()
    {
        const Score score = Gpif::parse(document());
        QCOMPARE(score.tempos.size(), 2);
        QCOMPARE(score.tempos.at(0).quarterBpm, 120.0);
        QCOMPARE(score.tempos.at(1).bar, 2);
        QCOMPARE(score.tempos.at(1).quarterBpm, 60.0);
    }

    // ---- the corpus, where there is one ----

    /**
     * Real files, if the machine running the tests has any. Set
     * FRETWORK_CORPUS to a directory of .gp files. Transcriptions are not ours
     * to redistribute, so this cannot be committed and must not fail when it
     * is absent.
     */
    void everyRealFileParses()
    {
        const QString corpus = qEnvironmentVariable("FRETWORK_CORPUS");
        if (corpus.isEmpty()) {
            QSKIP("set FRETWORK_CORPUS to a directory of .gp files to run this");
        }
        const QStringList files =
            QDir(corpus).entryList({QStringLiteral("*.gp")}, QDir::Files);
        QVERIFY2(!files.isEmpty(), qPrintable(QStringLiteral("no .gp files in ") + corpus));

        for (const QString &name : files) {
            QString why;
            const Score score = Gpif::read(QDir(corpus).filePath(name), &why);
            QVERIFY2(!score.isEmpty(), qPrintable(name + QStringLiteral(": ") + why));

            const QList<int> order = Timeline::playedOrder(score);
            QVERIFY(!order.isEmpty());

            for (int track = 0; track < score.tracks.size(); ++track) {
                const QList<Timeline::NoteEvent> notes =
                    Timeline::notesFor(score, track, order);
                for (const Timeline::NoteEvent &note : notes) {
                    QVERIFY(note.pitch >= 0 && note.pitch <= 127);
                    QVERIFY(note.velocity >= 1 && note.velocity <= 127);
                    QVERIFY(note.channel >= 0 && note.channel < 16);
                    QVERIFY(!(note.end < note.start));
                }
            }
        }
    }
};

QTEST_GUILESS_MAIN(GpifTest)
#include "gpiftest.moc"
