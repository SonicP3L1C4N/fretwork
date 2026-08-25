// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "zipreader.h"

#include <KZip>

#include <QFile>
#include <QTemporaryDir>
#include <QTest>
#include <QtEndian>

#include <zlib.h>

namespace
{
void appendU16(QByteArray &out, quint16 value)
{
    char bytes[2];
    qToLittleEndian(value, bytes);
    out.append(bytes, 2);
}

void appendU32(QByteArray &out, quint32 value)
{
    char bytes[4];
    qToLittleEndian(value, bytes);
    out.append(bytes, 4);
}
}

/**
 * The container reader, against archives this file builds byte by byte.
 *
 * The case worth the trouble is the last one: Guitar Pro 8.1.4 writes its
 * archives in streaming mode, where the local header says the entry is of
 * length zero and the truth is recorded afterwards. KArchive's KZip rejects
 * those files outright, which is why this reader exists, and a hand-built
 * archive keeps that fixed without needing a transcription in the repository.
 */
class ZipReaderTest : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_directory;

    QString path(const QString &name) const
    {
        return m_directory.path() + QLatin1Char('/') + name;
    }

    /** An ordinary archive, written by a library that is not ours. */
    QString writtenByKZip(const QString &name, const QByteArray &contents)
    {
        const QString file = path(name);
        KZip archive(file);
        if (!archive.open(QIODevice::WriteOnly)) {
            return {};
        }
        archive.writeFile(QStringLiteral("Content/score.gpif"), contents);
        archive.writeFile(QStringLiteral("VERSION"), QByteArrayLiteral("8.1"));
        archive.close();
        return file;
    }

    /**
     * An archive in streaming mode: bit 3 set, sizes and CRC zeroed in the
     * local header and given only in the data descriptor and the central
     * directory. Stored rather than deflated, because what is under test is
     * where the reader looks for the sizes, not the decompressor.
     */
    QString writtenStreaming(const QString &name, const QString &entry,
                             const QByteArray &contents)
    {
        const QByteArray entryName = entry.toUtf8();
        const quint32 crc = ::crc32(0, reinterpret_cast<const uchar *>(contents.constData()),
                                    uInt(contents.size()));
        QByteArray out;

        const quint32 localOffset = 0;
        out.append("PK\x03\x04", 4);
        appendU16(out, 20);         // version needed
        appendU16(out, 0x0008);     // the flag this whole test exists for
        appendU16(out, 0);          // stored
        appendU16(out, 0);          // time
        appendU16(out, 0);          // date
        appendU32(out, 0);          // crc -- not known yet, says the writer
        appendU32(out, 0);          // compressed size -- likewise
        appendU32(out, 0);          // uncompressed size -- likewise
        appendU16(out, quint16(entryName.size()));
        appendU16(out, 0);          // extra
        out.append(entryName);
        out.append(contents);

        // The data descriptor, where a streaming writer tells the truth.
        out.append("PK\x07\x08", 4);
        appendU32(out, crc);
        appendU32(out, quint32(contents.size()));
        appendU32(out, quint32(contents.size()));

        const quint32 centralOffset = quint32(out.size());
        out.append("PK\x01\x02", 4);
        appendU16(out, 20);         // version made by
        appendU16(out, 20);         // version needed
        appendU16(out, 0x0008);
        appendU16(out, 0);          // stored
        appendU16(out, 0);
        appendU16(out, 0);
        appendU32(out, crc);
        appendU32(out, quint32(contents.size()));
        appendU32(out, quint32(contents.size()));
        appendU16(out, quint16(entryName.size()));
        appendU16(out, 0);          // extra
        appendU16(out, 0);          // comment
        appendU16(out, 0);          // disk
        appendU16(out, 0);          // internal attributes
        appendU32(out, 0);          // external attributes
        appendU32(out, localOffset);
        out.append(entryName);

        const quint32 centralSize = quint32(out.size()) - centralOffset;
        out.append("PK\x05\x06", 4);
        appendU16(out, 0);          // this disk
        appendU16(out, 0);          // disk with the central directory
        appendU16(out, 1);          // entries on this disk
        appendU16(out, 1);          // entries in total
        appendU32(out, centralSize);
        appendU32(out, centralOffset);
        appendU16(out, 0);          // comment length

        const QString file = path(name);
        QFile handle(file);
        if (!handle.open(QIODevice::WriteOnly)) {
            return {};
        }
        handle.write(out);
        handle.close();
        return file;
    }

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(m_directory.isValid());
    }

    void readsADeflatedEntry()
    {
        const QByteArray contents = QByteArrayLiteral("<GPIF><Score/></GPIF>").repeated(200);
        const QString file = writtenByKZip(QStringLiteral("normal.gp"), contents);
        QVERIFY(!file.isEmpty());

        QString why;
        QCOMPARE(Zip::readEntry(file, QStringLiteral("Content/score.gpif"), &why), contents);
        QVERIFY2(why.isEmpty(), qPrintable(why));
    }

    void listsWhatIsInside()
    {
        const QString file = writtenByKZip(QStringLiteral("list.gp"),
                                           QByteArrayLiteral("<GPIF/>"));
        const QStringList names = Zip::entries(file);
        QVERIFY(names.contains(QStringLiteral("Content/score.gpif")));
        QVERIFY(names.contains(QStringLiteral("VERSION")));
    }

    /**
     * Guitar Pro 8.1.4's shape. The local header claims nothing; the central
     * directory knows. A reader that trusts the local header sees an empty
     * file, and KZip refuses the archive outright.
     */
    void readsAnEntryWhoseLocalHeaderKnowsNothing()
    {
        const QByteArray contents =
            QByteArrayLiteral("<GPIF><GPVersion>8.1.4</GPVersion></GPIF>");
        const QString file = writtenStreaming(QStringLiteral("streamed.gp"),
                                              QStringLiteral("Content/score.gpif"), contents);
        QVERIFY(!file.isEmpty());

        QString why;
        QCOMPARE(Zip::readEntry(file, QStringLiteral("Content/score.gpif"), &why), contents);
        QVERIFY2(why.isEmpty(), qPrintable(why));

        // And the archive KZip cannot open is one this reads: same file, both
        // ways round, so the regression is impossible to reintroduce quietly.
        KZip rejected(file);
        if (rejected.open(QIODevice::ReadOnly)) {
            rejected.close();
            QSKIP("KZip has learned to read streaming archives; good");
        }
    }

    void anEmptyEntryIsEmptyRatherThanMissing()
    {
        const QString file = writtenStreaming(QStringLiteral("empty.gp"),
                                              QStringLiteral("Content/score.gpif"),
                                              QByteArray());
        QString why;
        const QByteArray read = Zip::readEntry(file, QStringLiteral("Content/score.gpif"), &why);
        QVERIFY2(!read.isNull(), qPrintable(why));
        QVERIFY(read.isEmpty());
    }

    // ---- the ways it is asked for something it cannot give ----

    void saysWhichEntryIsMissing()
    {
        const QString file = writtenByKZip(QStringLiteral("absent.gp"),
                                           QByteArrayLiteral("<GPIF/>"));
        QString why;
        QVERIFY(Zip::readEntry(file, QStringLiteral("Content/nothing"), &why).isNull());
        QVERIFY2(why.contains(QStringLiteral("Content/nothing")), qPrintable(why));
    }

    void refusesWhatIsNotAZip()
    {
        const QString file = path(QStringLiteral("plain.gp"));
        QFile handle(file);
        QVERIFY(handle.open(QIODevice::WriteOnly));
        handle.write(QByteArrayLiteral("PK is a state, not a container"));
        handle.close();

        QString why;
        QVERIFY(Zip::readEntry(file, QStringLiteral("anything"), &why).isNull());
        QCOMPARE(why, QStringLiteral("not a ZIP container"));
    }

    void refusesAFileThatIsNotThere()
    {
        QString why;
        QVERIFY(Zip::readEntry(path(QStringLiteral("nope.gp")),
                               QStringLiteral("anything"), &why).isNull());
        QCOMPARE(why, QStringLiteral("no such file"));
    }

    /** Truncation is the commonest corruption, and must not read past the end. */
    void survivesATruncatedArchive()
    {
        const QByteArray whole = [this] {
            const QString file = writtenByKZip(QStringLiteral("whole.gp"),
                                               QByteArrayLiteral("<GPIF/>").repeated(50));
            QFile handle(file);
            handle.open(QIODevice::ReadOnly);
            return handle.readAll();
        }();
        QVERIFY(whole.size() > 40);

        for (const int keep : {whole.size() / 4, whole.size() / 2, whole.size() - 8}) {
            const QString file = path(QStringLiteral("cut%1.gp").arg(keep));
            QFile handle(file);
            QVERIFY(handle.open(QIODevice::WriteOnly));
            handle.write(whole.left(keep));
            handle.close();

            QString why;
            // Any answer but a crash or a wrong one: it either reads the whole
            // entry or says it cannot.
            const QByteArray read =
                Zip::readEntry(file, QStringLiteral("Content/score.gpif"), &why);
            if (read.isNull()) {
                QVERIFY2(!why.isEmpty(), "a failure must say why");
            } else {
                QCOMPARE(read, QByteArrayLiteral("<GPIF/>").repeated(50));
            }
        }
    }

    /** A byte flipped inside the compressed data must not read as truth. */
    void refusesDamagedContents()
    {
        const QString source = writtenByKZip(QStringLiteral("sound.gp"),
                                             QByteArrayLiteral("<GPIF/>").repeated(200));
        QFile handle(source);
        QVERIFY(handle.open(QIODevice::ReadOnly));
        QByteArray bytes = handle.readAll();
        handle.close();

        // Well past the local header, and well before the central directory.
        bytes[bytes.size() / 2] = char(bytes.at(bytes.size() / 2) ^ 0xFF);
        const QString file = path(QStringLiteral("damaged.gp"));
        QFile broken(file);
        QVERIFY(broken.open(QIODevice::WriteOnly));
        broken.write(bytes);
        broken.close();

        QString why;
        const QByteArray read =
            Zip::readEntry(file, QStringLiteral("Content/score.gpif"), &why);
        if (!read.isNull()) {
            // Flipping one byte can land somewhere deflate tolerates; what it
            // must never do is return something shorter and call it complete.
            QCOMPARE(read, QByteArrayLiteral("<GPIF/>").repeated(200));
        } else {
            QVERIFY(!why.isEmpty());
        }
    }
};

QTEST_GUILESS_MAIN(ZipReaderTest)
#include "zipreadertest.moc"
