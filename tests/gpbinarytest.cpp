// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "gpbinary.h"

#include <QDir>
#include <QTemporaryDir>
#include <QTest>

/**
 * The boundary between C++ and Rust, from the C++ side.
 *
 * The crate has its own tests and they are the ones that check the parsing.
 * These check the thing those cannot: that the two halves are linked to each
 * other, agree about the ABI, and agree about what the numbers crossing it
 * mean. A boundary that compiles is not a boundary that works, and the way
 * this one would fail is by returning plausible integers that mean something
 * else.
 *
 * Everything here has to pass on a build with no Rust at all, because that
 * build exists on purpose -- so the assertions are written as "what this says
 * when it is there" and skipped as a body rather than asserted as absent.
 */
class GpbinaryTest : public QObject
{
    Q_OBJECT

private:
    /** The first bytes of a GP3-to-GP5 file: a Pascal string, then padding. */
    static QByteArray header(const QByteArray &version)
    {
        const QByteArray text = QByteArrayLiteral("FICHIER GUITAR PRO v") + version;
        QByteArray out(1, char(text.size()));
        out.append(text);
        out.resize(31);
        return out;
    }

    static void skipWithoutRust()
    {
        if (!Gpbinary::isAvailable()) {
            QSKIP("built without cargo, so there is no Rust half to ask");
        }
    }

private Q_SLOTS:
    /**
     * The halves are joined, and they agree about the ABI.
     *
     * If this fails and the next few pass, the two were built from different
     * revisions and every answer below is a coincidence.
     */
    void theTwoHalvesAgreeAboutTheirAbi()
    {
        skipWithoutRust();
        QVERIFY(Gpbinary::isAvailable());
    }

    /** The version in the Pascal string is the version reported. */
    void readsTheVersionOutOfAGuitarProHeader()
    {
        skipWithoutRust();
        QCOMPARE(Gpbinary::formatOf(header("3.00")), Gpbinary::Format::Gp3);
        QCOMPARE(Gpbinary::formatOf(header("4.06")), Gpbinary::Format::Gp4);
        QCOMPARE(Gpbinary::formatOf(header("5.10")), Gpbinary::Format::Gp5);
        QCOMPARE(Gpbinary::nameOf(Gpbinary::Format::Gp5), QStringLiteral("Guitar Pro 5"));
    }

    /** Guitar Pro 6's container, stored or compressed. */
    void knowsTheGuitarProSixContainer()
    {
        skipWithoutRust();
        QCOMPARE(Gpbinary::formatOf(QByteArrayLiteral("BCFZ\x00\x00\x00\x00")),
                 Gpbinary::Format::Gpx);
        QCOMPARE(Gpbinary::formatOf(QByteArrayLiteral("BCFS\x00\x00\x00\x00")),
                 Gpbinary::Format::Gpx);
    }

    /** Nothing recognised is not an error: most files are not scores. */
    void saysNothingAboutFilesThatAreNotGuitarPro()
    {
        skipWithoutRust();
        QCOMPARE(Gpbinary::formatOf(QByteArray()), Gpbinary::Format::Unknown);
        QCOMPARE(Gpbinary::formatOf(QByteArrayLiteral("this is not a container")),
                 Gpbinary::Format::Unknown);
        QVERIFY(Gpbinary::nameOf(Gpbinary::Format::Unknown).isEmpty());

        // A version this program does not read is still not this program's to
        // claim: 6 lives in the other container entirely.
        QCOMPARE(Gpbinary::formatOf(header("6.00")), Gpbinary::Format::Unknown);
    }

    /**
     * Every prefix of a real header, which is the reason for the language.
     *
     * A truncated file is the ordinary case for a download that stopped, and
     * walking off the end of one is what this boundary exists to prevent. The
     * assertion is only that it answers at all: the test passing means nothing
     * read past the end.
     */
    void survivesEveryTruncationOfAHeader()
    {
        skipWithoutRust();
        const QByteArray whole = header("5.10");
        for (int length = 0; length <= whole.size(); ++length) {
            const Gpbinary::Format format = Gpbinary::formatOf(whole.left(length));
            QVERIFY(format == Gpbinary::Format::Unknown
                    || format == Gpbinary::Format::Gp5);
        }
    }

    /** A missing file is a question with no answer rather than a crash. */
    void aFileThatIsNotThereIsUnknown()
    {
        QTemporaryDir folder;
        QVERIFY(folder.isValid());
        QCOMPARE(Gpbinary::formatOf(folder.filePath(QStringLiteral("nothing.gp5"))),
                 Gpbinary::Format::Unknown);
    }

    /**
     * The real transcriptions, which must all come out as what they are.
     *
     * This is the half of the check that fixtures cannot do. A detector that
     * called a GP7 file a GP5 would be worse than no detector: the program
     * would refuse a file it can actually read, and explain the refusal
     * confidently.
     */
    void everyRealFileIsReportedAsGuitarProSevenOrEight()
    {
        skipWithoutRust();
        const QString corpus = qEnvironmentVariable("FRETWORK_CORPUS");
        if (corpus.isEmpty()) {
            QSKIP("set FRETWORK_CORPUS to a directory of .gp files to run this");
        }
        const QStringList files =
            QDir(corpus).entryList({QStringLiteral("*.gp")}, QDir::Files);
        QVERIFY2(!files.isEmpty(), qPrintable(QStringLiteral("no .gp files in ") + corpus));

        for (const QString &name : files) {
            const Gpbinary::Format format =
                Gpbinary::formatOf(QDir(corpus).filePath(name));
            QVERIFY2(format == Gpbinary::Format::Gp7,
                     qPrintable(name + QStringLiteral(" was called ")
                                + QString::number(int(format))));
        }
    }
};

QTEST_GUILESS_MAIN(GpbinaryTest)
#include "gpbinarytest.moc"
