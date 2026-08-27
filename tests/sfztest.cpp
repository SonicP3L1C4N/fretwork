// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "sfz.h"

#include <QTest>

/**
 * Reading the file that says which recording is which note.
 *
 * The cases that matter are the ones every SFZ parser gets wrong the first
 * time: a sample path with a space in it, opcodes and headers sharing a line,
 * and what a region inherits from the group above it. A library that will not
 * load because of any of those is a library nobody can use, and the fault
 * looks like a missing note rather than like a parsing bug.
 */
class SfzTest : public QObject
{
    Q_OBJECT

private:
    static Sfz::Instrument of(const QString &text)
    {
        return Sfz::parse(text, QStringLiteral("/samples"));
    }

private Q_SLOTS:
    void readsARegionAndWhereItsSampleIs()
    {
        const Sfz::Instrument instrument = of(QStringLiteral(
            "<region> sample=e2.wav lokey=40 hikey=40 pitch_keycenter=40\n"));
        QCOMPARE(instrument.regions.size(), 1);
        const Sfz::Region &region = instrument.regions.first();
        QCOMPARE(region.sample, QStringLiteral("/samples/e2.wav"));
        QCOMPARE(region.lowKey, 40);
        QCOMPARE(region.highKey, 40);
        QCOMPARE(region.keyCentre, 40);
    }

    void keepsASamplePathWithSpacesInIt()
    {
        // The one genuinely awkward thing about the format: a value runs to
        // the next thing that looks like an opcode, not to the next space.
        const Sfz::Instrument instrument = of(QStringLiteral(
            "<region> sample=Clean Guitar/E2 v3.wav volume=-6 lokey=40\n"));
        QCOMPARE(instrument.regions.size(), 1);
        QCOMPARE(instrument.regions.first().sample,
                 QStringLiteral("/samples/Clean Guitar/E2 v3.wav"));
        QCOMPARE(instrument.regions.first().volumeDb, -6.0);
        QCOMPARE(instrument.regions.first().lowKey, 40);
    }

    void takesNotesByNameOrByNumber()
    {
        // Libraries use both, sometimes in the same file.
        const Sfz::Instrument instrument = of(QStringLiteral(
            "<region> sample=a.wav lokey=c4 hikey=e4 pitch_keycenter=d4\n"));
        const Sfz::Region &region = instrument.regions.first();
        QCOMPARE(region.lowKey, 60);
        QCOMPARE(region.highKey, 64);
        QCOMPARE(region.keyCentre, 62);
    }

    void aRegionInheritsFromTheGroupAboveIt()
    {
        const Sfz::Instrument instrument = of(QStringLiteral(
            "<global> volume=-3\n"
            "<group> lokey=40 hikey=52 ampeg_release=0.4\n"
            "<region> sample=a.wav pitch_keycenter=40\n"
            "<region> sample=b.wav pitch_keycenter=45 volume=-9\n"));
        QCOMPARE(instrument.regions.size(), 2);
        QCOMPARE(instrument.regions.at(0).lowKey, 40);
        QCOMPARE(instrument.regions.at(0).highKey, 52);
        QCOMPARE(instrument.regions.at(0).volumeDb, -3.0);
        QCOMPARE(instrument.regions.at(0).release, 0.4);
        // The nearer setting wins, which is the whole point of the levels.
        QCOMPARE(instrument.regions.at(1).volumeDb, -9.0);
    }

    void aNewGroupForgetsTheOldOne()
    {
        const Sfz::Instrument instrument = of(QStringLiteral(
            "<group> volume=-3 pan=50\n"
            "<region> sample=a.wav\n"
            "<group> lokey=60\n"
            "<region> sample=b.wav\n"));
        QCOMPARE(instrument.regions.at(0).pan, 50.0);
        // Not carried over: a group is a group, not a running total.
        QCOMPARE(instrument.regions.at(1).pan, 0.0);
        QCOMPARE(instrument.regions.at(1).volumeDb, 0.0);
        QCOMPARE(instrument.regions.at(1).lowKey, 60);
    }

    void readsARoundRobin()
    {
        // Four recordings of one note, played in turn. The reason this format
        // is worth reading: the same note twice is not the same waveform twice.
        QString text = QStringLiteral("<group> lokey=40 hikey=40 seq_length=4\n");
        for (int take = 1; take <= 4; ++take) {
            text += QStringLiteral("<region> sample=e2_%1.wav seq_position=%1\n").arg(take);
        }
        const Sfz::Instrument instrument = of(text);
        QCOMPARE(instrument.regions.size(), 4);
        for (int take = 0; take < 4; ++take) {
            QCOMPARE(instrument.regions.at(take).sequenceLength, 4);
            QCOMPARE(instrument.regions.at(take).sequencePosition, take + 1);
        }
    }

    void survivesHeadersAndOpcodesSharingALine()
    {
        const Sfz::Instrument instrument = of(QStringLiteral(
            "<group> volume=-2 <region> sample=a.wav lokey=40 <region> sample=b.wav lokey=41\n"));
        QCOMPARE(instrument.regions.size(), 2);
        QCOMPARE(instrument.regions.at(0).sample, QStringLiteral("/samples/a.wav"));
        QCOMPARE(instrument.regions.at(0).lowKey, 40);
        QCOMPARE(instrument.regions.at(1).sample, QStringLiteral("/samples/b.wav"));
        QCOMPARE(instrument.regions.at(1).lowKey, 41);
        QCOMPARE(instrument.regions.at(1).volumeDb, -2.0);
    }

    void takesTheDefaultPathAndWindowsSlashes()
    {
        const Sfz::Instrument instrument = of(QStringLiteral(
            "<control> default_path=Samples\\Clean\\\n"
            "<region> sample=E2\\take1.wav\n"));
        QCOMPARE(instrument.regions.first().sample,
                 QStringLiteral("/samples/Samples/Clean/E2/take1.wav"));
    }

    void ignoresWhatItDoesNotKnow()
    {
        // Several hundred opcodes exist and this reads a dozen. A library that
        // would not load because it mentioned a filter is a library nobody can
        // use, and the ignored opcode does no harm.
        const Sfz::Instrument instrument = of(QStringLiteral(
            "<region> sample=a.wav cutoff=800 fil_type=lpf_2p xfin_lokey=30 lokey=40\n"));
        QCOMPARE(instrument.regions.size(), 1);
        QCOMPARE(instrument.regions.first().lowKey, 40);
    }

    void skipsComments()
    {
        const Sfz::Instrument instrument = of(QStringLiteral(
            "// a clean electric, DI\n"
            "<region> sample=a.wav lokey=40 // the low E\n"));
        QCOMPARE(instrument.regions.size(), 1);
        QCOMPARE(instrument.regions.first().lowKey, 40);
    }

    void saysSoWhenThereIsNothingInIt()
    {
        QString why;
        const Sfz::Instrument instrument =
            Sfz::parse(QStringLiteral("// nothing but a comment\n"),
                       QStringLiteral("/samples"), &why);
        QVERIFY(instrument.isEmpty());
        QVERIFY2(why.contains(QLatin1String("region")), qPrintable(why));
    }
};

QTEST_GUILESS_MAIN(SfzTest)
#include "sfztest.moc"
