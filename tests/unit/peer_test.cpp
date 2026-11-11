#include <QtTest>
#include <QElapsedTimer>
#include <QTcpSocket>
#include <QNetworkProxy>
#include <QNetworkInterface>
#include "furina_channel.h"
#include "peer.h"
#include "video_policy.h"

class RawChannel : public QObject {
  public:
    FurinaChannel channel;
    QTcpSocket socket;

    RawChannel(const QByteArray& password, QObject* parent = nullptr) : QObject(parent) {
        channel.reset(FurinaChannel::Role::Client, password);
        connect(&socket, &QTcpSocket::connected, this, [this] { socket.write(channel.start()); });
        connect(&socket, &QTcpSocket::readyRead, this, [this] { pump(); });
    }

    void connectTo(const QString& host, quint16 port) {
        socket.connectToHost(host, port);
    }

    bool pump() {
        QByteArray out;
        if (!channel.push(socket.readAll(), &out)) {
            socket.abort();
            return false;
        }
        if (!out.isEmpty())
            socket.write(out);
        return true;
    }

    bool send(quint8 type, const QByteArray& payload) {
        pump();
        QByteArray wire;
        if (!channel.seal(type, payload, &wire))
            return false;
        return socket.write(wire) == wire.size();
    }
};

class PeerTest : public QObject {
    Q_OBJECT
  private slots:

    void directTransportDisablesNagleAfterConnect() {
        Peer server, client;
        QVERIFY(server.listen(29893, "latency-check", "127.0.0.1"));
        client.connectTo("127.0.0.1", 29893, "latency-check");
        QTRY_VERIFY(server.ready() && client.ready());
        auto* socket = qobject_cast<QTcpSocket*>(client.mainTransport());
        QVERIFY(socket);
        int lowDelay = -1;
        QMetaObject::invokeMethod(socket, [&] {
            lowDelay = socket->socketOption(QAbstractSocket::LowDelayOption).toInt();
        }, Qt::BlockingQueuedConnection);
        QCOMPARE(lowDelay, 1);
    }

    void cursorConnectionIsIndependentOfVideo() {
        Peer server, client;
        QSignalSpy cursors(&client, &Peer::cursor), videos(&client, &Peer::video);
        QVERIFY(server.listen(29894, "cursor-channel", "127.0.0.1"));
        client.connectTo("127.0.0.1", 29894, "cursor-channel");
        QTRY_VERIFY(server.cursorReady() && client.cursorReady());
        auto* main = qobject_cast<QTcpSocket*>(client.mainTransport());
        QVERIFY(main);
        QMetaObject::invokeMethod(main, [main] { main->blockSignals(true); }, Qt::BlockingQueuedConnection);
        QByteArray video(FR_VIDEO_HEADER + 65536, 0);
        fr_video_info info{FR_VIDEO_KEY | FR_VIDEO_CURSOR_SEPARATE, 1, 1200, 1920, 1, 100};
        QVERIFY(fr_video_write(reinterpret_cast<uint8_t*>(video.data()), &info));
        QVERIFY(server.sendVideo(video));
        QTRY_VERIFY([&] {
            qint64 available = 0;
            QMetaObject::invokeMethod(main, [&] { available = main->bytesAvailable(); }, Qt::BlockingQueuedConnection);
            return available > 0;
        }());
        auto position = fr_cursor_position(123, 456, 1200, 1920, true);
        server.sendCursor(position);
        QTRY_COMPARE(cursors.size(), 1);
        QCOMPARE(cursors[0][0].toByteArray(), position);
        QCOMPARE(videos.size(), 0);
        QMetaObject::invokeMethod(main, [main] {
            main->blockSignals(false);
            QMetaObject::invokeMethod(main, "readyRead", Qt::DirectConnection);
        }, Qt::BlockingQueuedConnection);
        QTRY_COMPARE(videos.size(), 1);
        auto reverse = fr_cursor_position(-3, 14, 1920, 1080, true);
        QSignalSpy backwards(&server, &Peer::cursor);
        client.sendCursor(reverse);
        QTRY_COMPARE(backwards.size(), 1);
        QCOMPARE(backwards[0][0].toByteArray(), reverse);
        QByteArray invalid = position;
        invalid[23] = 2;
        server.sendCursor(invalid);
        QTest::qWait(30);
        QCOMPARE(cursors.size(), 1);
        client.close();
        QTRY_VERIFY(!server.cursorReady() && !client.cursorReady());
    }

    void cursorConnectionAuthenticatesAndCoalesces() {
        QObject owner;
        QTcpServer listener;
        QVERIFY(listener.listen(QHostAddress::LocalHost, 0));
        CursorTransport receiver(&owner), sender(&owner);
        int delivered = 0;
        QByteArray last;
        receiver.packet = [&](const QByteArray& packet) {
            ++delivered;
            last = packet;
        };
        connect(&listener, &QTcpServer::newConnection, &owner, [&] {
            while (listener.hasPendingConnections())
                receiver.accept(listener.nextPendingConnection());
        });
        const QByteArray key(32, 's');
        receiver.begin(key, true, {}, 0);
        QTcpSocket wrong;
        wrong.connectToHost(QHostAddress::LocalHost, listener.serverPort());
        QTRY_COMPARE(wrong.state(), QTcpSocket::ConnectedState);
        wrong.write(QByteArray(32, 'x'));
        QTRY_COMPARE(wrong.state(), QTcpSocket::UnconnectedState);
        QVERIFY(!receiver.isReady());
        sender.begin(key, false, QHostAddress::LocalHost, listener.serverPort());
        sender.send(fr_cursor_position(1, 1, 1200, 1920, true));
        auto newest = fr_cursor_position(2, 3, 1200, 1920, true);
        sender.send(newest);
        QTRY_VERIFY(sender.isReady() && receiver.isReady());
        for (auto* socket : owner.findChildren<QTcpSocket*>())
            if (socket->state() == QAbstractSocket::ConnectedState)
                QCOMPARE(socket->socketOption(QAbstractSocket::LowDelayOption).toInt(), 1);
        QTRY_COMPARE(delivered, 1);
        QCOMPARE(last, newest);
        sender.close();
        receiver.close();
        QTRY_VERIFY(owner.findChildren<QTcpSocket*>().isEmpty());
    }

    void gracefulStopDrainsCancellation_data() {
        QTest::addColumn<bool>("serverStops");
        QTest::newRow("client-stops") << false;
        QTest::newRow("server-stops") << true;
    }

    void gracefulStopDrainsCancellation() {
        QFETCH(bool, serverStops);
        Peer server, client;
        QVERIFY(server.listen(29880, "stop-sync-code"));
        client.connectTo("127.0.0.1", 29880, "stop-sync-code");
        QTRY_VERIFY(server.ready() && client.ready());
        auto *sender = serverStops ? &server : &client, *receiver = serverStops ? &client : &server;
        QSignalSpy video(receiver, &Peer::video), approval(receiver, &Peer::approval),
            closed(receiver, &Peer::disconnected);
        QStringList order;
        connect(receiver, &Peer::video, this, [&] { order << "video"; });
        connect(receiver, &Peer::approval, this, [&] { order << "cancel"; });
        connect(receiver, &Peer::disconnected, this, [&] { order << "disconnected"; });
        QByteArray packet(FR_VIDEO_HEADER + 192 * 1024, 0);
        fr_video_info info{FR_VIDEO_KEY, 1, 1920, 1080, 1, 123};
        QVERIFY(fr_video_write(reinterpret_cast<uint8_t*>(packet.data()), &info));
        QVERIFY(sender->sendVideo(packet));
        sender->sendApproval(3101, FR_APPROVAL_CANCEL, FR_APPROVAL_SHARE);
        sender->close(true);
        QVERIFY(!sender->ready());
        QTRY_COMPARE(closed.count(), 1);
        QCOMPARE(video.count(), 1);
        QCOMPARE(approval.count(), 1);
        QCOMPARE(approval[0][0].toUInt(), 3101u);
        QCOMPARE(approval[0][1].toUInt(), quint32(FR_APPROVAL_CANCEL));
        QCOMPARE(order, QStringList({"video", "cancel", "disconnected"}));
    }

    void simultaneousStopsFinishBothTransports() {
        Peer server, client;
        QVERIFY(server.listen(29881, "stop-sync-code"));
        client.connectTo("127.0.0.1", 29881, "stop-sync-code");
        QTRY_VERIFY(server.ready() && client.ready());
        server.sendApproval(3102, FR_APPROVAL_CANCEL, FR_APPROVAL_SHARE);
        client.sendApproval(3102, FR_APPROVAL_CANCEL, FR_APPROVAL_SHARE);
        server.close(true);
        client.close(true);
        QTRY_VERIFY(!server.hasOpenSockets() && !client.hasOpenSockets());
    }

    void anOldHeartbeatPongDoesNotFinishStop() {
        Peer server;
        RawChannel raw("stop-sync-code");
        QVERIFY(server.listen(29882, "stop-sync-code"));
        raw.connectTo("127.0.0.1", 29882);
        QTRY_VERIFY(server.ready() && raw.channel.transportReady());
        quint8 type = 0;
        QByteArray payload;
        bool sawPing = false;
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < 5000 && !sawPing) {
            raw.pump();
            while (raw.channel.pop(&type, &payload))
                if (type == FR_PING && payload.isEmpty())
                    sawPing = true;
            if (!sawPing)
                QTest::qWait(20);
        }
        QVERIFY(sawPing);
        server.sendApproval(3103, FR_APPROVAL_CANCEL, FR_APPROVAL_SHARE);
        server.close(true);
        bool sawBarrier = false;
        timer.restart();
        while (timer.elapsed() < 2000 && !sawBarrier) {
            raw.pump();
            while (raw.channel.pop(&type, &payload))
                if (type == FR_PING && payload.isEmpty())
                    sawBarrier = true;
            if (!sawBarrier)
                QTest::qWait(10);
        }
        QVERIFY(sawBarrier);
        QVERIFY(raw.send(FR_PONG, {}));
        QTest::qWait(40);
        QCOMPARE(raw.socket.state(), QAbstractSocket::ConnectedState);
        QVERIFY(raw.send(FR_PONG, {}));
        QTRY_COMPARE(raw.socket.state(), QAbstractSocket::UnconnectedState);
    }

    void videoProfileFeedback() {
        Peer server, client;
        QSignalSpy paired(&client, &Peer::connected), video(&server, &Peer::video),
            profile(&server, &Peer::profile), feedback(&client, &Peer::feedback);
        QVERIFY(server.listen(29879, "video-code"));
        client.connectTo("127.0.0.1", 29879, "video-code");
        QTRY_COMPARE(paired.count(), 1);
        client.sendProfile(0);
        QTRY_COMPARE(profile.count(), 1);
        QCOMPARE(profile[0][0].toInt(), 0);
        client.sendProfile(999);
        QTest::qWait(20);
        QCOMPARE(profile.count(), 1);
        QByteArray packet(FR_VIDEO_HEADER + 5, 0);
        fr_video_info info{FR_VIDEO_KEY, 1, 1920, 1200, 1, 12345};
        QVERIFY(fr_video_write(reinterpret_cast<uint8_t*>(packet.data()), &info));
        packet[32] = 1;
        QVERIFY(client.sendVideo(packet));
        QTRY_COMPARE(video.count(), 1);
        QCOMPARE(video[0][0].toByteArray(), packet);
        server.sendFeedback(60, 2.5, 16, 59.8);
        QTRY_COMPARE(feedback.count(), 1);
        QCOMPARE(feedback[0][0].toDouble(), 60.0);
        QCOMPARE(feedback[0][1].toDouble(), 2.5);
        QCOMPARE(feedback[0][3].toDouble(), 59.8);
    }

    void authenticatedSessionRequests() {
        Peer server, client;
        QSignalSpy paired(&client, &Peer::connected), received(&server, &Peer::session),
            reverse(&client, &Peer::session);
        QVERIFY(server.listen(29876, "session-code"));
        client.connectTo("127.0.0.1", 29876, "session-code");
        QTRY_COMPARE(paired.count(), 1);
        client.sendSession(FR_MAXIMIZE);
        QTRY_COMPARE(received.count(), 1);
        QCOMPARE(received[0][0].toUInt(), quint32(FR_MAXIMIZE));
        server.sendSession(FR_RESTORE);
        QTRY_COMPARE(reverse.count(), 1);
        QCOMPARE(reverse[0][0].toUInt(), quint32(FR_RESTORE));
        client.sendSession(99);
        QTest::qWait(50);
        QCOMPARE(received.count(), 1);
    }

    void unauthenticatedSessionIsRejected() {
        Peer server;
        QSignalSpy requests(&server, &Peer::session);
        QVERIFY(server.listen(29877, "session-code"));
        QTcpSocket raw;
        raw.connectToHost("127.0.0.1", 29877);
        QTRY_COMPARE(raw.state(), QAbstractSocket::ConnectedState);
        uint8_t h[12], action[4];
        fr_encode_header(h, FR_SESSION, 4);
        fr_put_u32(action, FR_START_SHARE);
        raw.write(reinterpret_cast<char*>(h), 12);
        raw.write(reinterpret_cast<char*>(action), 4);
        QTRY_COMPARE(raw.state(), QAbstractSocket::UnconnectedState);
        QCOMPARE(requests.count(), 0);
    }

    void invalidSessionPayloadIsRejected() {
        Peer server;
        QSignalSpy requests(&server, &Peer::session);
        QVERIFY(server.listen(29878, "session-code"));
        RawChannel raw("session-code");
        raw.connectTo("127.0.0.1", 29878);
        QTRY_VERIFY(server.ready() && raw.channel.transportReady());
        QVERIFY(raw.send(FR_SESSION, "xx"));
        QTRY_COMPARE(raw.socket.state(), QAbstractSocket::UnconnectedState);
        QCOMPARE(requests.count(), 0);
    }

    void deviceConnection() {
        const auto host = qEnvironmentVariable("FURINA_DEVICE_HOST"),
                   code = qEnvironmentVariable("FURINA_DEVICE_CODE");
        if (host.isEmpty() || code.isEmpty())
            QSKIP("Set FURINA_DEVICE_HOST and FURINA_DEVICE_CODE for a real device test");
        Peer client;
        QSignalSpy paired(&client, &Peer::connected);
        client.connectTo(host, 27183, code);
        QTRY_COMPARE(paired.count(), 1);
        QVERIFY(client.ready());
        QTest::qWait(200);
        client.close();
    }

    void directConnectionsIgnoreApplicationProxy() {
        struct RestoreProxy {
            QNetworkProxy old = QNetworkProxy::applicationProxy();

            ~RestoreProxy() {
                QNetworkProxy::setApplicationProxy(old);
            }
        } restore;

        QNetworkProxy::setApplicationProxy(QNetworkProxy(QNetworkProxy::HttpCachingProxy, "127.0.0.1", 9));
        QString address;
        for (const auto& a : QNetworkInterface::allAddresses())
            if (a.protocol() == QAbstractSocket::IPv4Protocol && !a.isLoopback()) {
                address = a.toString();
                break;
            }
        QVERIFY2(!address.isEmpty(), "Requires a local non-loopback IPv4 address");
        Peer server, client;
        QSignalSpy paired(&client, &Peer::connected), frames(&server, &Peer::frame);
        QVERIFY(server.listen(29875, "proxy-test-code"));
        client.connectTo(address, 29875, "proxy-test-code");
        QTRY_COMPARE(paired.count(), 1);
        QImage im(40, 30, QImage::Format_RGB32);
        im.fill(Qt::blue);
        client.sendFrame(im);
        QTRY_COMPARE(frames.count(), 1);
    }

    void connectionErrorDoesNotProduceLaterTimeout() {
        QTcpServer port;
        port.setProxy(QNetworkProxy::NoProxy);
        QVERIFY(port.listen(QHostAddress::LocalHost, 0));
        const auto unused = port.serverPort();
        port.close();
        Peer client;
        QSignalSpy statuses(&client, &Peer::status), closed(&client, &Peer::disconnected);
        client.connectTo("127.0.0.1", unused, "test-code");
        QTRY_VERIFY(statuses.count() > 0);
        QTRY_COMPARE(closed.count(), 1);
        const auto count = statuses.count();
        QTest::qWait(9500);
        QCOMPARE(statuses.count(), count);
    }

    void duplex() {
        Peer server, client;
        QSignalSpy connected(&client, &Peer::connected), frames(&server, &Peer::frame),
            keys(&server, &Peer::key), reverse(&client, &Peer::frame);
        QVERIFY(server.listen(29871, "12345678"));
        client.connectTo("127.0.0.1", 29871, "12345678");
        QTRY_COMPARE(connected.count(), 1);
        QVERIFY(server.ready());
        QImage im(80, 60, QImage::Format_RGB32);
        im.fill(Qt::red);
        client.sendFrame(im);
        QTRY_COMPARE(frames.count(), 1);
        QCOMPARE(qvariant_cast<QImage>(frames[0][0]).size(), im.size());
        server.sendFrame(im);
        QTRY_COMPARE(reverse.count(), 1);
        client.sendKey(4);
        QTRY_COMPARE(keys.count(), 1);
        QCOMPARE(keys[0][0].toUInt(), 4u);
        client.close();
        QTRY_VERIFY(!server.ready());
    }

    void wrongToken() {
        Peer s, c;
        QSignalSpy spy(&c, &Peer::connected);
        QVERIFY(s.listen(29872, "correct-code"));
        c.connectTo("127.0.0.1", 29872, "wrong-code");
        QTest::qWait(350);
        QCOMPARE(spy.count(), 0);
        QVERIFY(!s.ready());
    }

    void fragmentedAndCoalesced() {
        Peer s;
        QVERIFY(s.listen(29873, "test-code"));
        QTcpSocket raw;
        raw.connectToHost("127.0.0.1", 29873);
        QTRY_COMPARE(raw.state(), QAbstractSocket::ConnectedState);
        FurinaChannel channel;
        channel.reset(FurinaChannel::Role::Client, "test-code");
        const auto hello = channel.start();
        for (char byte : hello) {
            raw.write(&byte, 1);
            raw.flush();
            QTest::qWait(1);
        }
        QByteArray incoming;
        QElapsedTimer timer;
        timer.start();
        while (!channel.transportReady() && timer.elapsed() < 3000) {
            if (raw.waitForReadyRead(30))
                incoming += raw.readAll();
            while (!incoming.isEmpty() && !channel.transportReady()) {
                QByteArray out;
                QVERIFY(channel.push(incoming.left(1), &out));
                incoming.remove(0, 1);
                for (char byte : out) {
                    raw.write(&byte, 1);
                    raw.flush();
                }
            }
        }
        QTRY_VERIFY(s.ready() && channel.transportReady());
        QSignalSpy keys(&s, &Peer::key);
        uint8_t value[4];
        fr_put_u32(value, 3);
        QByteArray first, second;
        QVERIFY(channel.seal(FR_KEY, QByteArray(reinterpret_cast<char*>(value), 4), &first));
        QVERIFY(channel.seal(FR_KEY, QByteArray(reinterpret_cast<char*>(value), 4), &second));
        raw.write(first + second);
        QTRY_COMPARE(keys.count(), 2);
        first[0] = 'X';
        raw.write(first);
        QTRY_VERIFY(!s.ready());
    }

    void listenHonorsBindAddress() {
        Peer server;
        QVERIFY(server.listen(0, "bind-address-1", "127.0.0.1"));
        QCOMPARE(server.boundAddress(), QHostAddress("127.0.0.1"));
        QVERIFY(server.serverPort() != 0);
        Peer rejected;
        QVERIFY(!rejected.listen(0, "bind-address-1", "not-an-ip"));
        QCOMPARE(rejected.serverPort(), 0);
        QVERIFY(rejected.boundAddress().isNull());
    }

    void secondSessionIsDropped() {
        Peer server, first, second;
        QVERIFY(server.listen(29866, "one-session", "127.0.0.1"));
        first.connectTo("127.0.0.1", 29866, "one-session");
        QTRY_VERIFY(first.ready() && server.ready());
        QSignalSpy paired(&second, &Peer::connected);
        second.connectTo("127.0.0.1", 29866, "one-session");
        QTest::qWait(400);
        QCOMPARE(paired.count(), 0);
        QVERIFY(first.ready() && server.ready());
        QVERIFY(!second.ready());
    }

    void failedHandshakesLockOut() {
        Peer server;
        QVERIFY(server.listen(29869, "correct-code", "127.0.0.1"));
        QSignalSpy statuses(&server, &Peer::status);
        for (int attempt = 0; attempt < 5; ++attempt) {
            Peer client;
            QSignalSpy closed(&client, &Peer::disconnected);
            client.connectTo("127.0.0.1", 29869, "wrong-code-1");
            QTRY_COMPARE_WITH_TIMEOUT(closed.count(), 1, 3000);
            QVERIFY(!server.ready());
        }
        Peer good;
        QSignalSpy paired(&good, &Peer::connected);
        good.connectTo("127.0.0.1", 29869, "correct-code");
        QTest::qWait(300);
        QCOMPARE(paired.count(), 0);
        QVERIFY(!good.ready());
        bool locked = false;
        for (const auto& row : statuses) {
            const auto text = row.at(0).toString();
            QVERIFY(!text.contains("correct-code"));
            QVERIFY(!text.contains("wrong-code"));
            if (text.contains("锁定") || text.contains("locked"))
                locked = true;
        }
        QVERIFY(locked);
    }

    void reconnectResetsAead() {
        Peer server, client;
        QVERIFY(server.listen(29867, "reconnect-code", "127.0.0.1"));
        client.connectTo("127.0.0.1", 29867, "reconnect-code");
        QTRY_VERIFY(server.ready() && client.ready());
        QSignalSpy keys(&server, &Peer::key);
        client.sendKey(9);
        QTRY_COMPARE(keys.count(), 1);
        client.close();
        QTRY_VERIFY(!server.ready());
        keys.clear();
        client.connectTo("127.0.0.1", 29867, "reconnect-code");
        QTRY_VERIFY(server.ready() && client.ready());
        client.sendKey(11);
        QTRY_COMPARE(keys.count(), 1);
        QCOMPARE(keys.at(0).at(0).toUInt(), 11u);
    }

    void oneTimeSecretIsSingleUse() {
        const QString secret = QStringLiteral("ot1.AAAAAAAAAAAAAAAAAAAAAA");
        Peer server, client;
        QVERIFY(server.listen(29868, secret, "127.0.0.1"));
        client.connectTo("127.0.0.1", 29868, secret);
        QTRY_VERIFY(server.ready() && client.ready());
        QCOMPARE(server.establishedLongTermKey().size(), 32);
        client.close();
        QTRY_VERIFY(!server.ready());
        Peer again;
        QSignalSpy paired(&again, &Peer::connected), notes(&again, &Peer::status);
        again.connectTo("127.0.0.1", 29868, secret);
        QTRY_COMPARE(notes.count(), 1);
        QVERIFY(notes.at(0).at(0).toString().contains("一次性"));
        QVERIFY(!notes.at(0).at(0).toString().contains(secret));
        QTest::qWait(100);
        QCOMPARE(paired.count(), 0);
        QVERIFY(!again.ready());
    }

    void videoSendKeepsAtMostThreeFramesInFlight() {
        Peer server, client;
        QVERIFY(server.listen(29870, "video-cap", "127.0.0.1"));
        client.connectTo("127.0.0.1", 29870, "video-cap");
        QTRY_VERIFY(server.ready() && client.ready());
        auto* socket = qobject_cast<QTcpSocket*>(client.mainTransport());
        QVERIFY(socket);
        QMetaObject::invokeMethod(socket, [socket] {
            socket->setReadBufferSize(1);
            socket->blockSignals(true);
        }, Qt::BlockingQueuedConnection);
        QByteArray packet(256 * 1024, 0);
        fr_video_info info{FR_VIDEO_KEY, 1, 640, 480, 1, 1};
        QVERIFY(fr_video_write(reinterpret_cast<uint8_t*>(packet.data()), &info));
        int accepted = 0;
        for (int i = 0; i < 6; ++i)
            if (server.sendVideo(packet))
                ++accepted;
        QVERIFY(accepted >= 1);
        QVERIFY(accepted <= 3);
        QVERIFY(server.videoInFlight() <= 3);
        QMetaObject::invokeMethod(socket, [socket] { socket->blockSignals(false); }, Qt::BlockingQueuedConnection);
    }

    void unauthenticatedControl() {
        Peer s;
        QSignalSpy spy(&s, &Peer::key);
        QVERIFY(s.listen(29874, "test-code"));
        QTcpSocket raw;
        raw.connectToHost("127.0.0.1", 29874);
        QTRY_VERIFY(raw.state() == QAbstractSocket::ConnectedState);
        uint8_t h[12], k[4];
        fr_encode_header(h, FR_KEY, 4);
        fr_put_u32(k, 4);
        raw.write(reinterpret_cast<char*>(h), 12);
        raw.write(reinterpret_cast<char*>(k), 4);
        QTRY_VERIFY(raw.state() == QAbstractSocket::UnconnectedState);
        QCOMPARE(spy.count(), 0);
    }
};
QTEST_GUILESS_MAIN(PeerTest)
#include "peer_test.moc"
