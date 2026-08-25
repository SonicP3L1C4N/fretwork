// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "midi.h"
#include "timeline.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>
#include <QtEndian>

/**
 * The MIDI writer, checked by reading back what it wrote.
 *
 * A file that a synth plays without complaining can still be wrong -- a note
 * left on, a bend never returned to centre, a track whose declared length does
 * not match its contents. So this parses the bytes rather than trusting them,
 * and the parser is deliberately strict: it insists every chunk ends exactly
 * where it said it would.
 */
class MidTest : public QObject
{
    Q_OBJECT

private:
    struct Message {
        qint64 tick = 0;
        int status = 0;     //< the high nibble: 0x90, 0x80, 0xE0, 0xB0, 0xC0
        int channel = 0;
        int first = 0;
        int second = 0;
    };

    struct File {
        int format = 0;
        int tracks = 0;
        int division = 0;
        QList<QList<Message>> messages;
    };

    static quint32 variable(const QByteArray &data, int &at)
    {
        quint32 value = 0;
        while (at < data.size()) {
            const quint8 byte = quint8(data.at(at++));
            value = (value << 7) | (byte & 0x7F);
            if (!(byte & 0x80)) {
                break;
            }
        }
        return value;
    }

    /** Parses, and fails the test rather than returning something half-read. */
    static File parse(const QString &path)
    {
        File file;
        QFile handle(path);
        [&] { QVERIFY(handle.open(QIODevice::ReadOnly)); }();
        const QByteArray data = handle.readAll();

        [&] { QCOMPARE(data.left(4), QByteArrayLiteral("MThd")); }();
        file.format = qFromBigEndian<quint16>(data.constData() + 8);
        file.tracks = qFromBigEndian<quint16>(data.constData() + 10);
        file.division = qFromBigEndian<quint16>(data.constData() + 12);

        int at = 14;
        for (int index = 0; index < file.tracks; ++index) {
            [&] { QCOMPARE(data.mid(at, 4), QByteArrayLiteral("MTrk")); }();
            const quint32 length = qFromBigEndian<quint32>(data.constData() + at + 4);
            const int end = at + 8 + int(length);
            [&] { QVERIFY(end <= data.size()); }();

            QList<Message> messages;
            qint64 tick = 0;
            int status = 0;
            at += 8;
            while (at < end) {
                tick += variable(data, at);
                const quint8 byte = quint8(data.at(at));
                if (byte & 0x80) {
                    status = byte;
                    ++at;
                }

                if (status == 0xFF) {
                    ++at;                       // meta type
                    const quint32 size = variable(data, at);
                    at += int(size);
                    continue;
                }

                Message message;
                message.tick = tick;
                message.status = status & 0xF0;
                message.channel = status & 0x0F;
                message.first = quint8(data.at(at++));
                if (message.status != 0xC0 && message.status != 0xD0) {
                    message.second = quint8(data.at(at++));
                }
                messages.append(message);
            }
            // The chunk must end exactly where its length said, or something
            // downstream will read the next track as data.
            [&] { QCOMPARE(at, end); }();
            file.messages.append(messages);
        }
        [&] { QCOMPARE(at, int(data.size())); }();
        return file;
    }

    /** A guitar, six strings, one bar, holding the notes given. */
    static Score oneBar(const QList<Note> &notes, int strings = 6)
    {
        Score score;
        Track guitar;
        guitar.name = QStringLiteral("Guitar");
        guitar.instrumentType = QStringLiteral("electricGuitar");
        guitar.program = 29;
        for (int string = 0; string < strings; ++string) {
            guitar.tuning.append(40 + string * 5);
        }
        score.tracks.append(guitar);

        MasterBar bar;
        bar.bars = {0};
        score.masterBars.append(bar);
        score.rhythms.insert(0, Rational(1));

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

    static Note at(int midi, int string)
    {
        Note note;
        note.midi = midi;
        note.string = string;
        return note;
    }

    QTemporaryDir m_directory;

    QString path(const QString &name) const
    {
        return m_directory.path() + QLatin1Char('/') + name;
    }

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(m_directory.isValid());
    }

    void writesAFileThatParsesToItsLastByte()
    {
        const Score score = oneBar({at(64, 5), at(59, 4)});
        const QString file = path(QStringLiteral("plain.mid"));
        QVERIFY(Midi::write(score, Timeline::playedOrder(score), file));

        const File parsed = parse(file);
        QCOMPARE(parsed.format, 1);
        QCOMPARE(parsed.division, 960);
        QCOMPARE(parsed.tracks, 2);     // the conductor, and the guitar
    }

    /** A note left on is the classic way a generated MIDI file misbehaves. */
    void everyNoteIsTurnedOffAgain()
    {
        const Score score = oneBar({at(64, 5), at(59, 4), at(50, 2)});
        const QString file = path(QStringLiteral("balanced.mid"));
        QVERIFY(Midi::write(score, Timeline::playedOrder(score), file));

        int on = 0;
        int off = 0;
        for (const QList<Message> &track : parse(file).messages) {
            for (const Message &message : track) {
                on += message.status == 0x90 ? 1 : 0;
                off += message.status == 0x80 ? 1 : 0;
            }
        }
        QCOMPARE(on, 3);
        QCOMPARE(off, 3);
    }

    /**
     * The bend range is widened before anything plays. Without it a synth
     * assumes two semitones and every bend comes out a sixth of its size.
     */
    void theBendRangeIsSetBeforeTheFirstNote()
    {
        const Score score = oneBar({at(64, 5)});
        const QString file = path(QStringLiteral("range.mid"));
        QVERIFY(Midi::write(score, Timeline::playedOrder(score), file));

        const QList<Message> guitar = parse(file).messages.at(1);
        qint64 firstNote = -1;
        QList<int> rpn;
        for (const Message &message : guitar) {
            if (message.status == 0x90 && firstNote < 0) {
                firstNote = message.tick;
            }
            if (message.status == 0xB0 && firstNote < 0) {
                rpn.append(message.first);
            }
        }
        // Registered parameter 0, then the range in semitones and cents.
        QCOMPARE(rpn.mid(0, 4), QList<int>({101, 100, 6, 38}));
    }

    void aBentNoteMovesAndComesBackToCentre()
    {
        Note bent = at(64, 5);
        bent.bended = true;
        bent.bendOriginValue = 0;
        bent.bendOriginOffset = 0;
        bent.bendDestinationValue = 200;    // a whole tone
        bent.bendDestinationOffset = 100;

        const Score score = oneBar({bent});
        const QString file = path(QStringLiteral("bent.mid"));
        QVERIFY(Midi::write(score, Timeline::playedOrder(score), file));

        QList<Message> bends;
        for (const Message &message : parse(file).messages.at(1)) {
            if (message.status == 0xE0) {
                bends.append(message);
            }
        }
        QVERIFY2(bends.size() > 4, "a bend should be drawn as a curve, not a jump");

        const auto value = [](const Message &message) {
            return message.first | (message.second << 7);
        };
        QCOMPARE(value(bends.first()), 8192);           // starts where it is written
        QVERIFY(value(bends.at(bends.size() - 2)) > 8192);
        // And back to centre once the note is over, or the next note on this
        // string inherits the bend.
        QCOMPARE(value(bends.last()), 8192);
    }

    void anUnbentNoteSendsNoBendAtAll()
    {
        const Score score = oneBar({at(64, 5)});
        const QString file = path(QStringLiteral("straight.mid"));
        QVERIFY(Midi::write(score, Timeline::playedOrder(score), file));

        for (const Message &message : parse(file).messages.at(1)) {
            QVERIFY(message.status != 0xE0);
        }
    }

    // ---- channels ----

    void aLoneGuitarGetsAChannelPerString()
    {
        const Score score = oneBar({at(40, 0), at(45, 1), at(50, 2),
                                    at(55, 3), at(59, 4), at(64, 5)});
        const QString file = path(QStringLiteral("wide.mid"));
        Midi::Compromises compromises;
        QVERIFY(Midi::write(score, Timeline::playedOrder(score), file, -1, nullptr,
                            &compromises));
        QVERIFY2(compromises.isEmpty(), qPrintable(compromises.join(QLatin1Char('\n'))));

        QSet<int> used;
        for (const Message &message : parse(file).messages.at(1)) {
            if (message.status == 0x90) {
                used.insert(message.channel);
            }
        }
        QCOMPARE(used.size(), 6);
    }

    void percussionIsOnChannelTen()
    {
        Score score = oneBar({at(36, -1), at(38, -1)}, 0);
        score.tracks[0].instrumentType = QStringLiteral("drumKit");
        score.tracks[0].name = QStringLiteral("Drums");

        const QString file = path(QStringLiteral("drums.mid"));
        QVERIFY(Midi::write(score, Timeline::playedOrder(score), file));

        int notes = 0;
        for (const Message &message : parse(file).messages.at(1)) {
            if (message.status == 0x90) {
                // Channel 9 counting from zero is the tenth, which is the one
                // the standard reserves for percussion.
                QCOMPARE(message.channel, 9);
                ++notes;
            }
            // A drum track must not be given a programme, or a synth may take
            // the hint and play a piano.
            QVERIFY(message.status != 0xC0);
        }
        QCOMPARE(notes, 2);
    }

    /**
     * Sixteen channels is a limit of the file format. A score that exceeds it
     * must say what it gave up rather than quietly playing a bend across a
     * whole chord.
     */
    void aCrowdedScoreSaysWhatItGaveUp()
    {
        Score score = oneBar({at(64, 5)});
        for (int extra = 0; extra < 3; ++extra) {
            Track another = score.tracks.first();
            another.name = QStringLiteral("Guitar %1").arg(extra + 2);
            score.tracks.append(another);
            score.masterBars[0].bars.append(0);
        }

        const QString file = path(QStringLiteral("crowded.mid"));
        Midi::Compromises compromises;
        QVERIFY(Midi::write(score, Timeline::playedOrder(score), file, -1, nullptr,
                            &compromises));
        QVERIFY(!compromises.isEmpty());
        QVERIFY2(compromises.first().contains(QStringLiteral("bend")),
                 qPrintable(compromises.first()));
    }

    /** One track alone is what a stem is. */
    void writesASingleTrackOnItsOwn()
    {
        Score score = oneBar({at(64, 5)});
        Track second = score.tracks.first();
        second.name = QStringLiteral("Second");
        score.tracks.append(second);
        score.masterBars[0].bars.append(0);

        const QString file = path(QStringLiteral("stem.mid"));
        QVERIFY(Midi::write(score, Timeline::playedOrder(score), file, 1));

        const File parsed = parse(file);
        QCOMPARE(parsed.tracks, 2);     // the conductor, and one instrument
    }

    void refusesToWriteAnEmptyScore()
    {
        QString why;
        QVERIFY(!Midi::write(Score(), {}, path(QStringLiteral("nothing.mid")), -1, &why));
        QVERIFY(!why.isEmpty());
    }

    void refusesATrackThatIsNotThere()
    {
        const Score score = oneBar({at(64, 5)});
        QString why;
        QVERIFY(!Midi::write(score, Timeline::playedOrder(score),
                             path(QStringLiteral("absent.mid")), 7, &why));
        QVERIFY(!why.isEmpty());
    }
};

QTEST_GUILESS_MAIN(MidTest)
#include "midtest.moc"
