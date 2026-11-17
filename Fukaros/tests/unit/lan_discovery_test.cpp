#include <QtTest>
#include <QNetworkDatagram>
#include <QNetworkProxy>
#include "lan_discovery.h"
#include "network_discovery.h"

class LanDiscoveryTest : public QObject {
    Q_OBJECT
    const LanInterface loopback{"Test LAN",
                                QHostAddress("127.0.0.2"),
                                QHostAddress("255.0.0.0"),
                                QHostAddress("127.255.255.255"),
                                QHostAddress("127.0.0.3"),
                                0};

    quint16 freePort() {
        QUdpSocket socket;
        socket.setProxy(QNetworkProxy::NoProxy);
        if (!socket.bind(QHostAddress("127.0.0.9"), 0))
            return 0;
        return socket.localPort();
    }

  private slots:

    void singleResponderIsReported() {
        const auto port = freePort();
        QVERIFY(port);
        NetworkDiscoveryServer discovery;
        QVERIFY(discovery.start("127.0.0.3", port));
        LanDiscovery finder(nullptr, [this] { return QList<LanInterface>{loopback}; });
        QSignalSpy hosts(&finder, &LanDiscovery::hosts), failed(&finder, &LanDiscovery::failed);
        finder.start(3000, port);
        QTRY_COMPARE_WITH_TIMEOUT(hosts.count(), 1, 4000);
        QVERIFY(failed.isEmpty());
        // Windows keeps the address the reply was sent from; Linux loopback rewrites
        // it to 127.0.0.1, exactly as in usb_network_test.
#ifdef Q_OS_LINUX
        QCOMPARE(hosts.first()[0].toStringList(), QStringList{"127.0.0.1"});
#else
        QCOMPARE(hosts.first()[0].toStringList(), QStringList{"127.0.0.3"});
#endif
    }

    void everyResponderIsReported() {
        // Unlike UsbNetwork, two tablets must not be an error: the caller tries each.
        const auto port = freePort();
        QVERIFY(port);
        NetworkDiscoveryServer one, two;
        QVERIFY(one.start("127.0.0.3", port));
        QVERIFY(two.start("127.0.0.4", port));
        LanDiscovery finder(nullptr, [this] { return QList<LanInterface>{loopback}; });
        QSignalSpy hosts(&finder, &LanDiscovery::hosts), failed(&finder, &LanDiscovery::failed);
        finder.start(3000, port);
        QTRY_COMPARE_WITH_TIMEOUT(hosts.count(), 1, 4000);
        QVERIFY(failed.isEmpty());
        const auto found = hosts.first()[0].toStringList();
        QCOMPARE(found.size(), 2);
        QVERIFY(found.contains("127.0.0.3"));
        QVERIFY(found.contains("127.0.0.4"));
    }

    void noResponderFailsWithTheSearchMessage() {
        LanDiscovery finder(nullptr, [this] { return QList<LanInterface>{loopback}; });
        QSignalSpy hosts(&finder, &LanDiscovery::hosts), failed(&finder, &LanDiscovery::failed);
        finder.start(600);
        QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 2000);
        QVERIFY(hosts.isEmpty());
        QVERIFY(failed.first()[0].toString().contains("没有找到"));
    }

    void noAdapterSaysSo() {
        LanDiscovery finder(nullptr, [] { return QList<LanInterface>{}; });
        QSignalSpy status(&finder, &LanDiscovery::status), failed(&finder, &LanDiscovery::failed);
        finder.start(600);
        QTRY_VERIFY(!status.isEmpty());
        QVERIFY(status.first()[0].toString().contains("没有可用的局域网网卡"));
        QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 2000);
    }

    void cancellationDiscardsLateReplies() {
        const auto port = freePort();
        QUdpSocket responder;
        responder.setProxy(QNetworkProxy::NoProxy);
        QVERIFY(responder.bind(QHostAddress("127.0.0.3"), port));
        LanDiscovery finder(nullptr, [this] { return QList<LanInterface>{loopback}; });
        QSignalSpy hosts(&finder, &LanDiscovery::hosts), failed(&finder, &LanDiscovery::failed);
        finder.start(600, port);
        // Answer after the search has already been abandoned.
        QTRY_VERIFY(responder.hasPendingDatagrams());
        finder.cancel();
        while (responder.hasPendingDatagrams()) {
            const auto request = responder.receiveDatagram();
            responder.writeDatagram(request.makeReply(furinaDiscoveryReply(request.data())));
        }
        QTest::qWait(400);
        QVERIFY(hosts.isEmpty() && failed.isEmpty());
    }

    void enumerateRealInterfaces() {
        const auto interfaces = furinaLanInterfaces();
        for (const auto& interface : interfaces) {
            qInfo().noquote() << "LAN adapter:" << interface.name << interface.local.toString()
                              << interface.broadcast.toString() << interface.gateway.toString();
            QVERIFY2(!interface.local.isLoopback(), qPrintable(interface.name));
            QVERIFY2(!interface.local.isLinkLocal(), qPrintable(interface.name));
            QVERIFY2(interface.local != interface.broadcast, qPrintable(interface.name));
        }
    }
};
QTEST_GUILESS_MAIN(LanDiscoveryTest)
#include "lan_discovery_test.moc"