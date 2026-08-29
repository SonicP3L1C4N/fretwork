// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "jacktransport.h"

#include <QTest>

/**
 * The one runtime-loaded piece: dlopen'd rather than linked, so the thing
 * most likely to behave differently on somebody else's machine than on the
 * one it was built on.
 *
 * This suite never calls start(), stop() or locate(). Fretwork opens a real
 * JACK client the same way Player does the moment a JackTransport is
 * constructed -- that much is no more than the running application already
 * does on this machine every time playback starts -- but rolling a transport
 * that other software on this desktop may be following is not something a
 * regression pass should do on the side. So what is asserted here is the
 * contract the header promises regardless of what this machine has
 * installed: it never crashes, and it is never silent about which state it
 * ended up in.
 */
class JackTransportTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    /**
     * Either it found a transport to speak to, and says which library
     * answered with nothing left unexplained, or it found none, and says so
     * by name rather than leaving both empty.
     */
    void isEitherConnectedAndNamedOrRefusedAndExplained()
    {
        JackTransport transport;
        if (transport.isValid()) {
            QVERIFY(!transport.library().isEmpty());
            QVERIFY(transport.error().isEmpty());
        } else {
            QVERIFY(transport.library().isEmpty());
            QVERIFY(!transport.error().isEmpty());
            QVERIFY2(transport.error().contains(QStringLiteral("JACK"),
                                                 Qt::CaseInsensitive),
                     qPrintable(transport.error()));
        }
    }

    /**
     * Opening and closing several in a row -- what happens every time
     * playback starts and stops -- must not corrupt the next one's state or
     * crash on the way out.
     */
    void opensAndClosesRepeatedlyWithoutCrashing()
    {
        for (int i = 0; i < 3; ++i) {
            JackTransport transport;
            // Constructed and immediately destroyed: never started, so the
            // destructor takes the "never rolled" path rather than the one
            // that calls stop().
            QCOMPARE(transport.isValid(), !transport.library().isEmpty());
        }
    }

};

QTEST_GUILESS_MAIN(JackTransportTest)
#include "jacktransporttest.moc"
