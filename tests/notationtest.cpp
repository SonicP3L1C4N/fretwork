// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "notation.h"
#include "timeline.h"

#include <QJsonArray>
#include <QTest>

/**
 * The notation file, against the shape a real pack has.
 *
 * The schema here was read off the one real notation file on this machine --
 * the Für Elise pack -- rather than designed, and the checks below are mostly
 * that this writes what that file has: a version, an instrument, a staff with
 * a clef, and measures carrying bars carrying voices carrying beats.
 *
 * One check is stronger than the sample deserves. That file is a transcription
 * of a recording: its beats do not tile its bars, because a performer does not
 * play a bar's worth of notes in a bar's worth of time. A score read from a
 * `.gp` was written down rather than played, so its bars *do* tile, and this
 * asserts that they do -- which is the one thing the sample could never have
 * taught and the thing most likely to break.
 */
class NotationTest : public QObject
{
    Q_OBJECT

private:
    /** A part with a name, a type and a tuning. */
    static Track partOf(const QString &name, const QString &type,
                        const QList<int> &tuning)
    {
        Track track;
        track.name = name;
        track.instrumentType = type;
        track.tuning = tuning;
        return track;
    }

    /**
     * A guitar playing `values` quarters' worth of notes in one bar per entry.
     * Each inner list is one bar; each entry a duration in quarters, negative
     * for a rest.
     */
    static Score scoreOf(const QList<QList<Rational>> &bars,
                         const Track &part = partOf(QStringLiteral("Guitar"),
                                                    QStringLiteral("electricGuitar"),
                                                    {40, 45, 50, 55, 59, 64}))
    {
        Score score;
        score.title = QStringLiteral("A Piece");
        score.tracks.append(part);
        score.tempos.append({0, 0, 120});

        int id = 0;
        int rhythmId = 0;
        for (int index = 0; index < bars.size(); ++index) {
            MasterBar master;
            master.bars = {index};
            score.masterBars.append(master);

            QList<int> beats;
            for (const Rational &value : bars.at(index)) {
                const bool rest = value.numerator < 0;
                const Rational length =
                    rest ? Rational(-value.numerator, value.denominator) : value;
                score.rhythms.insert(rhythmId, length);

                QList<int> notes;
                if (!rest) {
                    Note note;
                    note.midi = 64;
                    note.string = 5;
                    note.fret = 0;
                    score.notes.insert(id, note);
                    notes.append(id);
                }
                score.beats.insert(id, Beat{rhythmId, notes, Dynamic::F, false, false});
                beats.append(id);
                ++id;
                ++rhythmId;
            }
            score.voices.insert(index, Voice{beats});
            score.bars.insert(index, Bar{{index, -1, -1, -1}});
        }
        return score;
    }

    static QJsonObject documentOf(const Score &score, int track = 0)
    {
        return Notation::documentFor(score, track, Timeline::playedOrder(score),
                                     QStringLiteral("part"));
    }

    static QJsonArray beatsOf(const QJsonObject &document, int measure)
    {
        return document.value(QStringLiteral("measures")).toArray()
            .at(measure).toObject()
            .value(QStringLiteral("staves")).toObject()
            .value(QStringLiteral("part")).toObject()
            .value(QStringLiteral("voices")).toArray()
            .at(0).toObject()
            .value(QStringLiteral("beats")).toArray();
    }

private Q_SLOTS:
    /**
     * The clef comes from the instrument, and it has to: the tunings overlap.
     *
     * This is the mistake the obvious rule makes. A guitar in B standard has
     * its highest open string at MIDI 59 and a six-string bass has its at 64 --
     * both of those are in the corpus -- so anything reading the range would
     * put a metal rhythm guitar in the bass clef and a jazz bass in the treble.
     */
    void theClefComesFromTheInstrumentAndNotTheTuning()
    {
        // Both of these are in the corpus. The guitar is in B standard, its
        // highest string at 59; the bass is a six-string whose highest is 64.
        const QList<int> bStandard = {35, 40, 45, 50, 54, 59};
        const QList<int> sixStringBass = {28, 33, 38, 43, 52, 64};

        QCOMPARE(Notation::clefFor(partOf(QStringLiteral("Rhythm"),
                                          QStringLiteral("electricGuitar"), bStandard)),
                 QStringLiteral("G2"));
        QCOMPARE(Notation::clefFor(partOf(QStringLiteral("Bass"),
                                          QStringLiteral("electricBass"), sixStringBass)),
                 QStringLiteral("F4"));

        // The guitar's highest string is *below* the bass's, so a rule reading
        // the range would put each of them in the other's clef. That is the
        // whole reason the instrument is asked instead.
        QVERIFY(bStandard.last() < sixStringBass.last());

        // Standard tuning still lands where anybody would expect.
        QCOMPARE(Notation::clefFor(partOf(QStringLiteral("Lead"),
                                          QStringLiteral("electricGuitar"),
                                          {40, 45, 50, 55, 59, 64})),
                 QStringLiteral("G2"));
        QCOMPARE(Notation::clefFor(partOf(QStringLiteral("Bass"),
                                          QStringLiteral("electricBass"),
                                          {28, 33, 38, 43})),
                 QStringLiteral("F4"));
    }

    /**
     * Every voice-bar adds up to its bar. The sample file's do not, because it
     * is a recording written down; a score is the other way round.
     */
    void everyBarAddsUpToItsBar()
    {
        const Score score = scoreOf({
            {Rational(1), Rational(1), Rational(1), Rational(1)},
            {Rational(2), Rational(1, 2), Rational(1, 2), Rational(1)},
            {Rational(3, 2), Rational(1, 2), Rational(-2)},   // dotted, then a rest
        });
        const QJsonObject document = documentOf(score);

        for (int measure = 0; measure < 3; ++measure) {
            double quarters = 0;
            const QJsonArray beats = beatsOf(document, measure);
            QVERIFY(!beats.isEmpty());
            for (const QJsonValue &value : beats) {
                const QJsonObject beat = value.toObject();
                const int denominator = beat.value(QStringLiteral("dur")).toInt();
                const int dots = beat.value(QStringLiteral("dot")).toInt();
                QVERIFY2(denominator > 0, "a beat with no written value");
                double length = 4.0 / denominator;
                for (int dot = 0; dot < dots; ++dot) {
                    length += 4.0 / denominator / (1 << (dot + 1));
                }
                quarters += length;
            }
            QVERIFY2(qAbs(quarters - 4.0) < 1e-9,
                     qPrintable(QStringLiteral("bar %1 came to %2 quarters")
                                    .arg(measure + 1).arg(quarters)));
        }
    }

    /** A crotchet is 4 and a dotted crotchet is 4 with one dot. */
    void writesTheValueMusiciansNameItBy()
    {
        const Score score = scoreOf({{Rational(1), Rational(1, 2), Rational(3, 2),
                                      Rational(1)}});
        const QJsonArray beats = beatsOf(documentOf(score), 0);
        QCOMPARE(beats.at(0).toObject().value(QStringLiteral("dur")).toInt(), 4);
        QCOMPARE(beats.at(0).toObject().value(QStringLiteral("dot")).toInt(), 0);
        QCOMPARE(beats.at(1).toObject().value(QStringLiteral("dur")).toInt(), 8);
        QCOMPARE(beats.at(2).toObject().value(QStringLiteral("dur")).toInt(), 4);
        QCOMPARE(beats.at(2).toObject().value(QStringLiteral("dot")).toInt(), 1);
    }

    /**
     * A rest is written as a beat with no notes rather than left out.
     *
     * The sample file leaves them out, and that is its second failing after
     * the tiling: a reader given only the sounding notes cannot tell a bar of
     * silence from a bar somebody forgot to transcribe.
     */
    void arestIsWrittenRatherThanSkipped()
    {
        const Score score = scoreOf({{Rational(1), Rational(-1), Rational(1),
                                      Rational(1)}});
        const QJsonArray beats = beatsOf(documentOf(score), 0);
        QCOMPARE(beats.size(), 4);
        QVERIFY(!beats.at(0).toObject().value(QStringLiteral("notes")).toArray().isEmpty());
        QVERIFY(beats.at(1).toObject().value(QStringLiteral("notes")).toArray().isEmpty());
    }

    /**
     * A time signature and a tempo are written where they change and nowhere
     * else, which is what the sample does and what printed music does.
     */
    void statesTheSignatureAndTempoOnlyWhereTheyChange()
    {
        Score score = scoreOf({{Rational(1), Rational(1), Rational(1), Rational(1)},
                               {Rational(1), Rational(1), Rational(1), Rational(1)},
                               {Rational(1), Rational(1), Rational(1), Rational(1)}});
        score.masterBars[2].numerator = 3;
        score.tempos.append({2, 0, 90});

        const QJsonArray measures =
            documentOf(score).value(QStringLiteral("measures")).toArray();
        QCOMPARE(measures.size(), 3);

        QVERIFY(measures.at(0).toObject().contains(QStringLiteral("ts")));
        QVERIFY(measures.at(0).toObject().contains(QStringLiteral("tempo")));
        QVERIFY(!measures.at(1).toObject().contains(QStringLiteral("ts")));
        QVERIFY(!measures.at(1).toObject().contains(QStringLiteral("tempo")));

        QCOMPARE(measures.at(2).toObject().value(QStringLiteral("ts")).toArray().at(0).toInt(), 3);
        QCOMPARE(measures.at(2).toObject().value(QStringLiteral("tempo")).toDouble(), 90.0);
    }

    /** Seconds, from the same clock the arrangement and the audio use. */
    void placesMeasuresInSecondsAtTheTempoTheyAreHeardAt()
    {
        const Score score = scoreOf({{Rational(1), Rational(1), Rational(1), Rational(1)},
                                     {Rational(1), Rational(1), Rational(1), Rational(1)}});
        const QJsonArray measures =
            documentOf(score).value(QStringLiteral("measures")).toArray();
        // 120 crotchets a minute: a 4/4 bar is two seconds.
        QCOMPARE(measures.at(0).toObject().value(QStringLiteral("t")).toDouble(), 0.0);
        QCOMPARE(measures.at(1).toObject().value(QStringLiteral("t")).toDouble(), 2.0);
        QCOMPARE(beatsOf(documentOf(score), 1).at(1).toObject()
                     .value(QStringLiteral("t")).toDouble(),
                 2.5);
    }

    /** Measures are numbered as they are heard, so a repeat is two of them. */
    void numbersMeasuresInThePlayedOrder()
    {
        Score score = scoreOf({{Rational(4)}, {Rational(4)}});
        score.masterBars[0].repeatStart = true;
        score.masterBars[1].repeatEnd = true;
        score.masterBars[1].repeatCount = 2;

        const QJsonArray measures =
            documentOf(score).value(QStringLiteral("measures")).toArray();
        QCOMPARE(measures.size(), 4);
        for (int index = 0; index < measures.size(); ++index) {
            QCOMPARE(measures.at(index).toObject().value(QStringLiteral("idx")).toInt(),
                     index + 1);
        }
    }

    /** A kit has no notation of this kind, and says so by being empty. */
    void aDrumKitGetsNoNotationFile()
    {
        Track kit = partOf(QStringLiteral("Drums"), QStringLiteral("drumKit"),
                           {0, 0, 0, 0, 0, 0});
        const Score score = scoreOf({{Rational(4)}}, kit);
        QVERIFY(documentOf(score).isEmpty());
    }

    /** The staff carries the id it was given, so a manifest can point at it. */
    void namesTheStaffWhateverTheCallerCallsIt()
    {
        const QJsonObject document = documentOf(scoreOf({{Rational(4)}}));
        const QJsonObject staff =
            document.value(QStringLiteral("staves")).toArray().at(0).toObject();
        QCOMPARE(staff.value(QStringLiteral("id")).toString(), QStringLiteral("part"));
        QCOMPARE(staff.value(QStringLiteral("label")).toString(), QStringLiteral("Guitar"));
        QCOMPARE(document.value(QStringLiteral("version")).toInt(), 1);
        QCOMPARE(document.value(QStringLiteral("instrument")).toString(),
                 QStringLiteral("electricGuitar"));
    }
};

QTEST_GUILESS_MAIN(NotationTest)
#include "notationtest.moc"
