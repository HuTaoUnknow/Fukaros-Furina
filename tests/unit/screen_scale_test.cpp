#include <QtTest>
#include <QSettings>
#include <QMessageAuthenticationCode>
#include <QTcpServer>
#include "screen_scale.h"
#include "screen_scale_settings.h"

class ScaleTest : public QObject {
    Q_OBJECT
  private slots:

    void initTestCase() {
        QCoreApplication::setOrganizationName("FurinaTests");
        QCoreApplication::setApplicationName("ScreenScale");
        QSettings().clear();
    }

    void init() {
        QSettings().clear();
    }

    void relativeSlotsRejectBadAndUnsupportedValues() {
        int slot = 999;
        QVERIFY(fr_screen_scale_relative(150, -2, 1, &slot));
        QCOMPARE(slot, 0);
        QVERIFY(fr_screen_scale_relative(100, -2, 1, &slot));
        QCOMPARE(slot, -2);
        QVERIFY(!fr_screen_scale_relative(200, -2, 1, &slot));
        QVERIFY(!fr_screen_scale_relative(151, -2, 3, &slot));
        QVERIFY(!fr_screen_scale_relative(150, 1, 3, &slot));
        QVERIFY(!fr_screen_scale_relative(150, INT_MIN, 3, &slot));
        QVERIFY(!fr_screen_scale_relative(150, -1, 100, &slot));
        QCOMPARE(fr_screen_scale_absolute(-2, 0), 150);
        QCOMPARE(fr_screen_scale_absolute(INT_MIN, 0), 0);
        QCOMPARE(fr_screen_scale_absolute(-2, 10), 0);
    }

    void bidirectionalSyncStaleReplayAndPersistence() {
        QWidget pc, phone;
        QComboBox a(&pc), b(&phone);
        for (auto* box : {&a, &b}) {
            box->setObjectName("screenScale");
            for (int p : {100, 125, 150, 175, 200, 225, 250, 300})
                box->addItem(QString::number(p) + "%");
        }
        Peer server, client;
        ScreenScaleSettings desktop(&client, &pc, true), mobile(&server, &phone, false);
        QCOMPARE(desktop.percent(), 150);
        QCOMPARE(a.currentData().toInt(), 150);
        b.setCurrentIndex(b.findData(200));
        QVERIFY(server.listen(29937, "ABCD-EFGH-JKLM-NPQR-ST23", "127.0.0.1"));
        client.connectTo("127.0.0.1", 29937, "ABCD-EFGH-JKLM-NPQR-ST23");
        QTRY_VERIFY(client.ready());
        QTRY_COMPARE(desktop.percent(), 200);
        a.setCurrentIndex(a.findData(175));
        QTRY_COMPARE(mobile.percent(), 175);
        QSignalSpy changes(&desktop, &ScreenScaleSettings::changed);
        server.sendScreenScale(300, 0);
        QTest::qWait(40);
        QCOMPARE(desktop.percent(), 175);
        QCOMPARE(changes.size(), 0);
        desktop.confirmApplied(150);
        QTRY_COMPARE(mobile.percent(), 150);
        QCOMPARE(changes.size(), 0);
        QCOMPARE(QSettings().value("screenScalePercent").toInt(), 150);
        QSignalSpy packets(&client, &Peer::screenScale);
        server.sendScreenScale(151, 1);
        server.sendScreenScale(99, 1);
        QTest::qWait(30);
        QCOMPARE(packets.size(), 0);
        QVERIFY(client.ready());
        QWidget restored;
        QComboBox c(&restored);
        c.setObjectName("screenScale");
        for (int p : {100, 125, 150, 175, 200, 225, 250, 300})
            c.addItem(QString::number(p) + "%");
        Peer offline;
        ScreenScaleSettings persisted(&offline, &restored, true);
        QCOMPARE(persisted.percent(), 150);
    }

    void legacyPeerIsRejected() {
        QTcpServer legacy;
        QVERIFY(legacy.listen(QHostAddress::LocalHost, 0));
        Peer client;
        QSignalSpy status(&client, &Peer::status);
        client.connectTo("127.0.0.1", legacy.serverPort(), "ABCD-EFGH-JKLM-NPQR-ST23");
        QTRY_VERIFY(legacy.hasPendingConnections());
        auto* socket = legacy.nextPendingConnection();
        uint8_t header[FR_HEADER_SIZE];
        QVERIFY(fr_encode_header(header, FR_CHALLENGE, 32));
        socket->write(reinterpret_cast<char*>(header), sizeof header);
        socket->write(QByteArray(32, 'x'));
        QTRY_VERIFY(!client.ready());
        QTRY_VERIFY_WITH_TIMEOUT(socket->state() == QAbstractSocket::UnconnectedState, 3000);
        bool reported = false;
        for (const auto& row : status)
            if (row[0].toString().contains("0.4.21"))
                reported = true;
        QVERIFY(reported);
    }

    void toggleSynchronizesAndRestoresSavedScale() {
        QWidget pc, phone;
        QComboBox a(&pc), b(&phone);
        QCheckBox at(&pc), bt(&phone);
        at.setObjectName("screenScaleEnabled");
        bt.setObjectName("screenScaleEnabled");
        for (auto* box : {&a, &b}) {
            box->setObjectName("screenScale");
            for (int p : {100, 125, 150, 200})
                box->addItem(QString::number(p) + "%");
        }
        Peer server, client;
        ScreenScaleSettings desktop(&client, &pc, true), mobile(&server, &phone, false);
        QVERIFY(at.isChecked());
        QVERIFY(server.listen(29937, "ABCD-EFGH-JKLM-NPQR-ST23", "127.0.0.1"));
        client.connectTo("127.0.0.1", 29937, "ABCD-EFGH-JKLM-NPQR-ST23");
        QTRY_VERIFY(client.ready());
        a.setCurrentIndex(a.findData(200));
        QTRY_COMPARE(mobile.percent(), 200);
        at.setChecked(false);
        QTRY_VERIFY(!bt.isChecked());
        QCOMPARE(desktop.percent(), 100);
        QCOMPARE(mobile.percent(), 100);
        QCOMPARE(b.currentData().toInt(), 200);
        QVERIFY(!b.isEnabled());
        desktop.confirmApplied(100);
        QCOMPARE(a.currentData().toInt(), 200);
        bt.setChecked(true);
        QTRY_VERIFY(at.isChecked());
        QCOMPARE(desktop.percent(), 200);
        QVERIFY(a.isEnabled());
        at.setChecked(false);
        QTRY_VERIFY(!bt.isChecked());
        QWidget restored;
        QComboBox c(&restored);
        QCheckBox ct(&restored);
        c.setObjectName("screenScale");
        ct.setObjectName("screenScaleEnabled");
        for (int p : {100, 125, 150, 200})
            c.addItem(QString::number(p) + "%");
        Peer offline;
        ScreenScaleSettings saved(&offline, &restored, true);
        QCOMPARE(saved.percent(), 100);
        QCOMPARE(c.currentData().toInt(), 200);
        QVERIFY(!ct.isChecked());
        ct.setChecked(true);
        QCOMPARE(saved.percent(), 200);
    }
};
QTEST_MAIN(ScaleTest)
#include "screen_scale_test.moc"
