// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "musicxml.h"

#include "key.h"

#include <QDomDocument>
#include <QTest>

/**
 * The MusicXML exporter, read back with an XML parser rather than by matching
 * strings.
 *
 * Checking a document by looking for substrings in it passes on documents that
 * are not documents, so everything here goes through QDomDocument: if it does
 * not parse, every test fails at once, which is the right amount of noise for
 * that particular mistake.
 *
 * The thing most worth testing is the one thing MusicXML needs that MIDI does
 * not. A note here is a letter, an accidental and an octave, and no pitch
 * number decides which letter -- MIDI 66 is an F sharp in G major and a G flat
 * in D flat major, and both are correct. That is `Key::spell`, built for the
 * harmony work, and this is the first thing to need it.
 */
class MusicxmlTest : public QObject
{
    Q_OBJECT

private:
    static Track guitar()
    {
        Track track;
        track.name = QStringLiteral("Guitar");
        track.instrumentType = QStringLiteral("electricGuitar");
        track.tuning = {40, 45, 50, 55, 59, 64};
        return track;
    }

    /** One bar of one part, holding `notes` as crotchets in one voice. */
    static Score scoreOf(const QList<Note> &notes, const Track &part = guitar(),
                         const Key::Signature &key = {})
    {
        Score score;
        score.title = QStringLiteral("A Piece");
        score.artist = QStringLiteral("Somebody");
        score.tracks.append(part);
        score.tempos.append({0, 0, 120});
        score.rhythms.insert(0, Rational(1));

        MasterBar master;
        master.bars = {0};
        master.key = key;
        score.masterBars.append(master);

        QList<int> beats;
        for (int index = 0; index < notes.size(); ++index) {
            score.notes.insert(index, notes.at(index));
            score.beats.insert(index, Beat{0, {index}, Dynamic::F, false, false});
            beats.append(index);
        }
        score.voices.insert(0, Voice{beats});
        score.bars.insert(0, Bar{{0, -1, -1, -1}});
        return score;
    }

    static Note noteOf(int midi, int string = 5, int fret = 0)
    {
        Note note;
        note.midi = midi;
        note.string = string;
        note.fret = fret;
        return note;
    }

    /** Parses, or fails the test saying so. */
    static QDomDocument parse(const QByteArray &xml)
    {
        QDomDocument document;
        const auto result = document.setContent(xml);
        if (!result) {
            qWarning("%s", qPrintable(result.errorMessage));
        }
        return document;
    }

    static QDomElement firstNoteOf(const QDomDocument &document)
    {
        return document.elementsByTagName(QStringLiteral("note")).at(0).toElement();
    }

    static QString textOf(const QDomElement &parent, const QString &tag)
    {
        const QDomNodeList found = parent.elementsByTagName(tag);
        return found.isEmpty() ? QString() : found.at(0).toElement().text();
    }

private Q_SLOTS:
    /** It parses, and it says what it is. */
    void writesADocumentThatParses()
    {
        const QDomDocument document =
            parse(Musicxml::documentFor(scoreOf({noteOf(64)})));
        QVERIFY(!document.isNull());
        QCOMPARE(document.documentElement().tagName(), QStringLiteral("score-partwise"));
        QCOMPARE(document.documentElement().attribute(QStringLiteral("version")),
                 QStringLiteral("4.0"));
        QCOMPARE(textOf(document.documentElement(), QStringLiteral("work-title")),
                 QStringLiteral("A Piece"));
    }

    /**
     * The same pitch, written differently in two keys.
     *
     * This is the whole reason MusicXML needed something MIDI export did not.
     * MIDI 66 is an F sharp where the key has sharps in it and a G flat where
     * it has flats, and a file that says the wrong one is not wrong about the
     * sound -- it is wrong about the music, which is worse, because it will be
     * read and played by somebody who trusts it.
     */
    void spellsAPitchTheWayItsKeySpellsIt()
    {
        const Key::Signature gMajor{1, false};      // one sharp
        const Key::Signature dFlatMajor{-5, false}; // five flats

        const QDomElement sharp =
            firstNoteOf(parse(Musicxml::documentFor(
                scoreOf({noteOf(66)}, guitar(), gMajor))));
        QCOMPARE(textOf(sharp, QStringLiteral("step")), QStringLiteral("F"));
        QCOMPARE(textOf(sharp, QStringLiteral("alter")), QStringLiteral("1"));

        const QDomElement flat =
            firstNoteOf(parse(Musicxml::documentFor(
                scoreOf({noteOf(66)}, guitar(), dFlatMajor))));
        QCOMPARE(textOf(flat, QStringLiteral("step")), QStringLiteral("G"));
        QCOMPARE(textOf(flat, QStringLiteral("alter")), QStringLiteral("-1"));

        // The same sound either way, which is the point of the disagreement.
        QCOMPARE(Key::midiOf(Key::spell(66, gMajor)), 66);
        QCOMPARE(Key::midiOf(Key::spell(66, dFlatMajor)), 66);
    }

    /**
     * Strings are numbered from the other end, and the file says so.
     *
     * MusicXML counts from the highest sounding string down and this program
     * counts from the lowest up. Both conventions are in use; what would be
     * wrong is letting them meet without a conversion.
     */
    void turnsTheStringNumberingRoundTheOtherWay()
    {
        const QDomDocument document =
            parse(Musicxml::documentFor(scoreOf({noteOf(40, 0, 0), noteOf(64, 5, 0)})));
        const QDomNodeList notes = document.elementsByTagName(QStringLiteral("note"));
        // The low E is string 0 here and string 6 there; the high E the reverse.
        QCOMPARE(textOf(notes.at(0).toElement(), QStringLiteral("string")),
                 QStringLiteral("6"));
        QCOMPARE(textOf(notes.at(1).toElement(), QStringLiteral("string")),
                 QStringLiteral("1"));
    }

    /** A guitar sounds an octave below where it is written, and says so. */
    void writesTheGuitarsOctaveTransposition()
    {
        const QDomDocument document =
            parse(Musicxml::documentFor(scoreOf({noteOf(64)})));
        QCOMPARE(textOf(document.documentElement(), QStringLiteral("sign")),
                 QStringLiteral("G"));
        QCOMPARE(textOf(document.documentElement(),
                        QStringLiteral("clef-octave-change")),
                 QStringLiteral("-1"));

        Track bass;
        bass.name = QStringLiteral("Bass");
        bass.instrumentType = QStringLiteral("electricBass");
        bass.tuning = {28, 33, 38, 43};
        const QDomDocument low =
            parse(Musicxml::documentFor(scoreOf({noteOf(28, 0, 0)}, bass)));
        QCOMPARE(textOf(low.documentElement(), QStringLiteral("sign")),
                 QStringLiteral("F"));
        QVERIFY(low.elementsByTagName(QStringLiteral("clef-octave-change")).isEmpty());
    }

    /** Divisions are worked out from the rhythms, not picked in advance. */
    void countsDivisionsFromWhatTheScoreActuallyUses()
    {
        Score simple = scoreOf({noteOf(64)});
        QCOMPARE(Musicxml::divisionsFor(simple), 1);

        // A triplet quaver is a third of a crotchet, so thirds have to fit.
        simple.rhythms.insert(1, Rational(1, 3));
        QCOMPARE(Musicxml::divisionsFor(simple), 3);
        simple.rhythms.insert(2, Rational(1, 4));
        QCOMPARE(Musicxml::divisionsFor(simple), 12);
    }

    /** Notes struck together are one chord, not several notes in a row. */
    void stacksAChordWithTheChordElement()
    {
        Score score = scoreOf({noteOf(64)});
        score.notes.insert(1, noteOf(59, 4, 0));
        score.beats.insert(0, Beat{0, {0, 1}, Dynamic::F, false, false});

        const QDomDocument document = parse(Musicxml::documentFor(score));
        const QDomNodeList notes = document.elementsByTagName(QStringLiteral("note"));
        QCOMPARE(notes.size(), 2);
        QVERIFY(notes.at(0).toElement()
                    .elementsByTagName(QStringLiteral("chord")).isEmpty());
        QCOMPARE(notes.at(1).toElement()
                     .elementsByTagName(QStringLiteral("chord")).size(), 1);
    }

    /** A beat with no notes is a rest, and is written as one. */
    void writesARestForABeatWithNoNotes()
    {
        Score score = scoreOf({noteOf(64)});
        score.beats.insert(1, Beat{0, {}, Dynamic::F, false, false});
        score.voices.insert(0, Voice{{0, 1}});

        const QDomDocument document = parse(Musicxml::documentFor(score));
        const QDomNodeList notes = document.elementsByTagName(QStringLiteral("note"));
        QCOMPARE(notes.size(), 2);
        QCOMPARE(notes.at(1).toElement()
                     .elementsByTagName(QStringLiteral("rest")).size(), 1);
    }

    /**
     * A kit is unpitched, and every drum it uses is declared.
     *
     * A drum's note number is which drum rather than which pitch, so writing
     * one as a pitch would produce a tune nobody played on a staff nobody
     * reads. The part list declares one instrument per drum and each note
     * points at one.
     */
    void writesADrumKitAsUnpitchedInstruments()
    {
        Track kit;
        kit.name = QStringLiteral("Drums");
        kit.instrumentType = QStringLiteral("drumKit");
        kit.tuning = {0, 0, 0, 0, 0, 0};
        const Score score = scoreOf({noteOf(36, -1, 0), noteOf(38, -1, 0)}, kit);

        const QDomDocument document = parse(Musicxml::documentFor(score));
        QCOMPARE(textOf(document.documentElement(), QStringLiteral("sign")),
                 QStringLiteral("percussion"));
        QVERIFY(document.elementsByTagName(QStringLiteral("pitch")).isEmpty());
        QCOMPARE(document.elementsByTagName(QStringLiteral("unpitched")).size(), 2);

        // Two drums used, so two instruments declared, and the numbers are one
        // greater than the note numbers.
        QCOMPARE(document.elementsByTagName(QStringLiteral("score-instrument")).size(), 2);
        const QDomNodeList unpitched =
            document.elementsByTagName(QStringLiteral("midi-unpitched"));
        QCOMPARE(unpitched.size(), 2);
        QCOMPARE(unpitched.at(0).toElement().text(), QStringLiteral("37"));
        QCOMPARE(unpitched.at(1).toElement().text(), QStringLiteral("39"));
    }

    /** A repeat is a barline, because this is a document and not a performance. */
    void writesRepeatsAsBarlinesRatherThanPlayingThem()
    {
        Score score = scoreOf({noteOf(64)});
        score.masterBars[0].repeatStart = true;
        score.masterBars[0].repeatEnd = true;
        score.masterBars[0].repeatCount = 3;

        const QDomDocument document = parse(Musicxml::documentFor(score));
        const QDomNodeList repeats =
            document.elementsByTagName(QStringLiteral("repeat"));
        QCOMPARE(repeats.size(), 2);
        QCOMPARE(repeats.at(0).toElement().attribute(QStringLiteral("direction")),
                 QStringLiteral("forward"));
        QCOMPARE(repeats.at(1).toElement().attribute(QStringLiteral("direction")),
                 QStringLiteral("backward"));
        QCOMPARE(repeats.at(1).toElement().attribute(QStringLiteral("times")),
                 QStringLiteral("3"));

        // One measure written, not three played.
        QCOMPARE(document.elementsByTagName(QStringLiteral("measure")).size(), 1);
    }

    /** A harmonic says it is one, and is written at the pitch it sounds. */
    void writesAHarmonicAsAHarmonic()
    {
        Note note = noteOf(76, 0, 8);
        note.harmonic = Harmonic::Type::Natural;
        note.harmonicFret = 8.2;

        const QDomDocument document = parse(Musicxml::documentFor(scoreOf({note})));
        QCOMPARE(document.elementsByTagName(QStringLiteral("harmonic")).size(), 1);
        QCOMPARE(document.elementsByTagName(QStringLiteral("natural")).size(), 1);
        QCOMPARE(textOf(firstNoteOf(document), QStringLiteral("octave")),
                 QStringLiteral("5"));
    }

    /** Ties are two halves of one thing, and each says which half it is. */
    void writesBothEndsOfATie()
    {
        Note first = noteOf(64);
        first.tieOrigin = true;
        Note second = noteOf(64);
        second.tieDestination = true;

        const QDomDocument document =
            parse(Musicxml::documentFor(scoreOf({first, second})));
        const QDomNodeList ties = document.elementsByTagName(QStringLiteral("tie"));
        QCOMPARE(ties.size(), 2);
        QCOMPARE(ties.at(0).toElement().attribute(QStringLiteral("type")),
                 QStringLiteral("start"));
        QCOMPARE(ties.at(1).toElement().attribute(QStringLiteral("type")),
                 QStringLiteral("stop"));
    }
};

QTEST_GUILESS_MAIN(MusicxmlTest)
#include "musicxmltest.moc"
