#include <QtTest>
#include <QTcpSocket>
#include <QNetworkProxy>
#include <QNetworkInterface>
#include <QDateTime>
#include "peer.h"
#include "video_policy.h"
#include "furina_security.h"

namespace {
const char* kPairingCode = "ABCD-EFGH-JKLM-NPQR-ST23";

struct RawSession {
    fr_handshake hs{};
    uint64_t sendCount = 0;
    uint64_t recvCount = 0;
    QByteArray pending;
};

bool rawHandshake(QTcpSocket& raw, RawSession& session, bool fragment = false) {
    uint8_t psk[32], secret[32], eph[32], hello[FR_HS_CLIENT_HELLO], confirm[FR_HS_CLIENT_CONFIRM];
    fr_qr_info info{};
    int error = 0;
    if (fr_credential_from_text(kPairingCode, psk, &info, &error) != 1)
        return false;
    for (int i = 0; i < 32; ++i) {
        secret[i] = uint8_t(50 + i);
        eph[i] = uint8_t(90 + i * 3);
    }
    fr_identity id{};
    fr_identity_from_secret(&id, secret);
    if (!fr_hs_client_start(&session.hs, &id, eph, psk, 1, nullptr, hello))
        return false;
    const auto writeBytes = [&](const uint8_t* data, int size) {
        if (!fragment)
            return raw.write(reinterpret_cast<const char*>(data), size) == size;
        for (int i = 0; i < size; ++i) {
            if (raw.write(reinterpret_cast<const char*>(data + i), 1) != 1)
                return false;
            raw.flush();
        }
        return true;
    };
    if (!writeBytes(hello, FR_HS_CLIENT_HELLO))
        return false;
    const auto start = QDateTime::currentMSecsSinceEpoch();
    while (raw.bytesAvailable() < FR_HS_SERVER_HELLO && QDateTime::currentMSecsSinceEpoch() - start < 3000)
        QTest::qWait(10);
    if (raw.bytesAvailable() < FR_HS_SERVER_HELLO)
        return false;
    const auto reply = raw.read(FR_HS_SERVER_HELLO);
    if (!fr_hs_client_finish(&session.hs, reinterpret_cast<const uint8_t*>(reply.constData()), confirm))
        return false;
    return writeBytes(confirm, FR_HS_CLIENT_CONFIRM);
}

bool writeFrame(QTcpSocket& raw, RawSession& session, quint8 type, const QByteArray& payload) {
    uint8_t header[FR_HEADER_SIZE];
    if (!fr_encode_header(header, type, payload.size()))
        return false;
    QByteArray plain;
    plain.append(reinterpret_cast<const char*>(header), FR_HEADER_SIZE);
    plain.append(payload);
    QByteArray record(plain.size() + 20, Qt::Uninitialized);
    size_t n = 0;
    if (fr_record_seal(session.hs.send_key, session.hs.send_salt, &session.sendCount,
                       reinterpret_cast<const uint8_t*>(plain.constData()), size_t(plain.size()),
                       reinterpret_cast<uint8_t*>(record.data()), size_t(record.size()), &n) != 1)
        return false;
    return raw.write(record.constData(), qsizetype(n)) == qsizetype(n);
}

bool readFrame(QTcpSocket& raw, RawSession& session, quint8* type, QByteArray* payload, int timeoutMs = 4000) {
    const auto start = QDateTime::currentMSecsSinceEpoch();
    while (QDateTime::currentMSecsSinceEpoch() - start < timeoutMs) {
        session.pending += raw.readAll();
        if (session.pending.size() >= 4) {
            const auto declared = fr_get_u32(reinterpret_cast<const uint8_t*>(session.pending.constData()));
            if (declared > FR_MAX_PAYLOAD + FR_HEADER_SIZE)
                return false;
            QByteArray plain(int(declared), Qt::Uninitialized);
            size_t consumed = 0, outn = 0;
            const int rc =
                fr_record_open(session.hs.recv_key, session.hs.recv_salt, &session.recvCount,
                               reinterpret_cast<const uint8_t*>(session.pending.constData()),
                               size_t(session.pending.size()), &consumed, reinterpret_cast<uint8_t*>(plain.data()),
                               size_t(plain.size()), &outn);
            if (rc < 0)
                return false;
            if (rc == 1) {
                session.pending.remove(0, int(consumed));
                if (outn < FR_HEADER_SIZE)
                    return false;
                fr_header header{};
                if (!fr_decode_header(reinterpret_cast<const uint8_t*>(plain.constData()), &header) ||
                    outn != FR_HEADER_SIZE + header.length)
                    return false;
                *type = header.type;
                *payload = plain.mid(FR_HEADER_SIZE, int(header.length));
                return true;
            }
        }
        QTest::qWait(10);
    }
    return false;
}
} // namespace

class PeerTest : public QObject {
    Q_OBJECT
  private slots:

    void directTransportDisablesNagleAfterConnect() {
        Peer server, client;
        QVERIFY(server.listen(29893, "ABCD-EFGH-JKLM-NPQR-ST23", "127.0.0.1"));
        client.connectTo("127.0.0.1", 29893, "ABCD-EFGH-JKLM-NPQR-ST23");
        QTRY_VERIFY(server.ready() && client.ready());
        auto* socket = client.findChild<QTcpSocket*>("furinaMain");
        QVERIFY(socket);
        QCOMPARE(socket->socketOption(QAbstractSocket::LowDelayOption).toInt(), 1);
    }

    void cursorConnectionIsIndependentOfVideo() {
        Peer server, client;
        QSignalSpy cursors(&client, &Peer::cursor), videos(&client, &Peer::video);
        QVERIFY(server.listen(29894, "ABCD-EFGH-JKLM-NPQR-ST23", "127.0.0.1"));
        client.connectTo("127.0.0.1", 29894, "ABCD-EFGH-JKLM-NPQR-ST23");
        QTRY_VERIFY(server.cursorReady() && client.cursorReady());
        auto* main = client.findChild<QTcpSocket*>("furinaMain");
        QVERIFY(main);
        main->blockSignals(true);
        QByteArray video(FR_VIDEO_HEADER + 65536, 0);
        fr_video_info info{FR_VIDEO_KEY | FR_VIDEO_CURSOR_SEPARATE, 1, 1200, 1920, 1, 100};
        QVERIFY(fr_video_write(reinterpret_cast<uint8_t*>(video.data()), &info));
        QVERIFY(server.sendVideo(video));
        auto position = fr_cursor_position(123, 456, 1200, 1920, true);
        server.sendCursor(position);
        QTRY_COMPARE(cursors.size(), 1);
        QCOMPARE(cursors[0][0].toByteArray(), position);
        QCOMPARE(videos.size(), 0);
        main->blockSignals(false);
        QVERIFY(QMetaObject::invokeMethod(main, "readyRead", Qt::DirectConnection));
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
        QVERIFY(server.listen(29880, "ABCD-EFGH-JKLM-NPQR-ST23"));
        client.connectTo("127.0.0.1", 29880, "ABCD-EFGH-JKLM-NPQR-ST23");
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
        QVERIFY(server.listen(29881, "ABCD-EFGH-JKLM-NPQR-ST23"));
        client.connectTo("127.0.0.1", 29881, "ABCD-EFGH-JKLM-NPQR-ST23");
        QTRY_VERIFY(server.ready() && client.ready());
        server.sendApproval(3102, FR_APPROVAL_CANCEL, FR_APPROVAL_SHARE);
        client.sendApproval(3102, FR_APPROVAL_CANCEL, FR_APPROVAL_SHARE);
        server.close(true);
        client.close(true);
        QTRY_VERIFY(server.findChildren<QTcpSocket*>().isEmpty() &&
                    client.findChildren<QTcpSocket*>().isEmpty());
    }

    void anOldHeartbeatPongDoesNotFinishStop() {
        Peer server;
        QTcpSocket raw;
        QVERIFY(server.listen(29882, kPairingCode));
        raw.connectToHost("127.0.0.1", 29882);
        QTRY_COMPARE(raw.state(), QAbstractSocket::ConnectedState);
        RawSession session;
        QVERIFY(rawHandshake(raw, session));
        QTRY_VERIFY(server.ready());
        bool sawScale = false;
        quint8 type = 0;
        QByteArray payload;
        while (type != FR_PING) {
            QVERIFY(readFrame(raw, session, &type, &payload, 5000));
            if (type == FR_STATUS && payload.contains("screen-scale"))
                sawScale = true;
        }
        QVERIFY(sawScale);
        server.sendApproval(3103, FR_APPROVAL_CANCEL, FR_APPROVAL_SHARE);
        server.close(true);
        bool sawCancel = false, sawBarrier = false;
        while (!sawBarrier) {
            QVERIFY(readFrame(raw, session, &type, &payload));
            if (type == FR_APPROVAL)
                sawCancel = true;
            if (type == FR_PING)
                sawBarrier = true;
        }
        QVERIFY(sawCancel);
        QVERIFY(writeFrame(raw, session, FR_PONG, {}));
        raw.flush();
        QTest::qWait(40);
        QCOMPARE(raw.state(), QAbstractSocket::ConnectedState);
        QVERIFY(writeFrame(raw, session, FR_PONG, {}));
        raw.flush();
        QTRY_COMPARE(raw.state(), QAbstractSocket::UnconnectedState);
    }

    void videoProfileFeedback() {
        Peer server, client;
        QSignalSpy paired(&client, &Peer::connected), video(&server, &Peer::video),
            profile(&server, &Peer::profile), feedback(&client, &Peer::feedback);
        QVERIFY(server.listen(29879, "ABCD-EFGH-JKLM-NPQR-ST23"));
        client.connectTo("127.0.0.1", 29879, "ABCD-EFGH-JKLM-NPQR-ST23");
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
        QVERIFY(server.listen(29876, "ABCD-EFGH-JKLM-NPQR-ST23"));
        client.connectTo("127.0.0.1", 29876, "ABCD-EFGH-JKLM-NPQR-ST23");
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
        QVERIFY(server.listen(29877, "ABCD-EFGH-JKLM-NPQR-ST23"));
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
        QVERIFY(server.listen(29878, kPairingCode));
        QTcpSocket raw;
        raw.connectToHost("127.0.0.1", 29878);
        QTRY_COMPARE(raw.state(), QAbstractSocket::ConnectedState);
        RawSession session;
        QVERIFY(rawHandshake(raw, session));
        QTRY_VERIFY(server.ready());
        QVERIFY(writeFrame(raw, session, FR_SESSION, QByteArray("xx", 2)));
        QTRY_COMPARE(raw.state(), QAbstractSocket::UnconnectedState);
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
        QVERIFY(server.listen(29875, "ABCD-EFGH-JKLM-NPQR-ST23"));
        client.connectTo(address, 29875, "ABCD-EFGH-JKLM-NPQR-ST23");
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
        client.connectTo("127.0.0.1", unused, "ABCD-EFGH-JKLM-NPQR-ST23");
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
        QVERIFY(server.listen(29871, "ABCD-EFGH-JKLM-NPQR-ST23"));
        client.connectTo("127.0.0.1", 29871, "ABCD-EFGH-JKLM-NPQR-ST23");
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
        QVERIFY(s.listen(29872, "ABCD-EFGH-JKLM-NPQR-ST23"));
        c.connectTo("127.0.0.1", 29872, "WXYZ-6789-ABCD-EFGH-JKL2");
        QTest::qWait(350);
        QCOMPARE(spy.count(), 0);
        QVERIFY(!s.ready());
    }

    void fragmentedAndCoalesced() {
        Peer s;
        QVERIFY(s.listen(29873, kPairingCode));
        QTcpSocket raw;
        raw.connectToHost("127.0.0.1", 29873);
        QTRY_VERIFY(raw.state() == QAbstractSocket::ConnectedState);
        RawSession session;
        QVERIFY(rawHandshake(raw, session, true));
        QTRY_VERIFY(s.ready());
        QSignalSpy keys(&s, &Peer::key);
        uint8_t keyBytes[4];
        fr_put_u32(keyBytes, 3);
        QVERIFY(writeFrame(raw, session, FR_KEY, QByteArray(reinterpret_cast<char*>(keyBytes), 4)));
        QVERIFY(writeFrame(raw, session, FR_KEY, QByteArray(reinterpret_cast<char*>(keyBytes), 4)));
        QTRY_COMPARE(keys.count(), 2);
        QByteArray corrupt(32, 'X');
        raw.write(corrupt);
        QTRY_VERIFY(!s.ready());
    }

    void unauthenticatedControl() {
        Peer s;
        QSignalSpy spy(&s, &Peer::key);
        QVERIFY(s.listen(29874, "ABCD-EFGH-JKLM-NPQR-ST23"));
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
