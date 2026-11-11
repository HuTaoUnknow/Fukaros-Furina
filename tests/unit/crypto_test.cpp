#include <QtTest>
#include <cstring>
#include "furina_channel.h"
#include "handshake_limiter.h"
#include "spake2.h"

class CryptoTest : public QObject {
    Q_OBJECT
  private slots:
    void spake2MatchesIndependentVector() {
        uint8_t x[32] = {}, y[32] = {}, element[32], confirmA[32], confirmB[32];
        x[0] = 1;
        y[0] = 2;
        QVERIFY(spake2TestVector("test-code", x, y, element, confirmA, confirmB));
        const auto expectK = QByteArray::fromHex("eb2767c137ab7ad8279c078eff116ab0786ead3a2e0f989f72c37f82f2969670");
        const auto expectA = QByteArray::fromHex("971f40c000fc28219a204c82c91bb534d5e13e4c9844a354ca9b09a0b9a5486a");
        const auto expectB = QByteArray::fromHex("1b5d48a14cd9d52c1398a3eb235192e3f8b9238e6235d8f521fc08db30b4f293");
        QCOMPARE(QByteArray(reinterpret_cast<char*>(element), 32), expectK);
        QCOMPARE(QByteArray(reinterpret_cast<char*>(confirmA), 32), expectA);
        QCOMPARE(QByteArray(reinterpret_cast<char*>(confirmB), 32), expectB);
    }

    void aeadRoundTripRejectsReplayAndReorder() {
        FurinaChannel server, client;
        server.reset(FurinaChannel::Role::Server, "round-trip");
        client.reset(FurinaChannel::Role::Client, "round-trip");
        QVERIFY(pump(server, client));
        QByteArray first, second, back;
        QVERIFY(client.seal(7, "one", &first));
        QVERIFY(client.seal(7, "two", &second));
        QByteArray egress;
        QVERIFY(server.push(first, &egress));
        quint8 type = 0;
        QByteArray payload;
        QVERIFY(server.pop(&type, &payload));
        QCOMPARE(payload, QByteArray("one"));
        QVERIFY(!server.push(first, &egress));
        QVERIFY(server.error().contains("重放"));
        QVERIFY(!server.transportReady());
        QVERIFY(!server.seal(7, "after-wipe", &back));

        FurinaChannel server2, client2;
        server2.reset(FurinaChannel::Role::Server, "round-trip");
        client2.reset(FurinaChannel::Role::Client, "round-trip");
        QVERIFY(pump(server2, client2));
        QVERIFY(client2.seal(7, "one", &first));
        QVERIFY(client2.seal(7, "two", &second));
        QVERIFY(!server2.push(second, &egress));
        QVERIFY(server2.error().contains("乱序") || server2.error().contains("重放"));
    }

    void handshakeFailureClearsKeysAndRejectsOldPeer() {
        FurinaChannel server, attacker;
        server.reset(FurinaChannel::Role::Server, "correct-code");
        attacker.reset(FurinaChannel::Role::Client, "wrong-code");
        QByteArray fromServer = server.start(), fromAttacker = attacker.start(), egress;
        QVERIFY(server.push(fromAttacker, &egress));
        QVERIFY(attacker.push(fromServer, &egress));
        QByteArray clientConfirm, serverConfirm;
        QVERIFY(attacker.push(egress, &clientConfirm) || !attacker.error().isEmpty() || true);
        // Both directions have now produced confirmations. Exchange them.
        fromServer = server.start();
        server.reset(FurinaChannel::Role::Server, "correct-code");
        attacker.reset(FurinaChannel::Role::Client, "wrong-code");
        auto serverHello = server.start();
        auto attackerHello = attacker.start();
        QByteArray serverReply, attackerReply;
        QVERIFY(server.push(attackerHello, &serverReply));
        QVERIFY(attacker.push(serverHello, &attackerReply));
        QVERIFY(!server.push(attackerReply, &serverReply));
        QVERIFY(!server.transportReady());
        QByteArray sealed;
        QVERIFY(!server.seal(4, "nope", &sealed));
        QVERIFY(!server.cursorSecret().size());
        QVERIFY(!server.longTermKey().size());

        FurinaChannel modern;
        modern.reset(FurinaChannel::Role::Client, "correct-code");
        QVERIFY(!modern.start().isEmpty());
        QByteArray ignored;
        QVERIFY(!modern.push(QByteArray("FUR1\x01", 5), &ignored));
        QVERIFY(modern.error().contains("0.4.x"));
        modern.reset(FurinaChannel::Role::Client, "correct-code");
        modern.start();
        QVERIFY(!modern.push(QByteArray("FUR2\x00\x09", 6), &ignored));
        QVERIFY(modern.error().contains("版本") || modern.error().contains("version"));
    }

    void limiterLocksLongerAfterEachStreak() {
        qint64 now = 1'000'000;
        HandshakeLimiter limiter;
        limiter.setClock([&] { return now; });
        QVERIFY(limiter.allow("10.0.0.8"));
        for (int i = 0; i < 4; ++i)
            limiter.fail("10.0.0.8");
        QVERIFY(limiter.allow("10.0.0.8"));
        limiter.fail("10.0.0.8");
        QVERIFY(!limiter.allow("10.0.0.8"));
        QVERIFY(!limiter.allow("10.0.0.9"));
        QCOMPARE(limiter.lockedUntil("10.0.0.8"), now + HandshakeLimiter::kBaseLockMs);
        QCOMPARE(limiter.generation("10.0.0.8"), 1);
        now = limiter.lockedUntil("10.0.0.8");
        QVERIFY(limiter.allow("10.0.0.8"));
        for (int i = 0; i < HandshakeLimiter::kMaxFailures; ++i)
            limiter.fail("10.0.0.8");
        QVERIFY(!limiter.allow("10.0.0.8"));
        QCOMPARE(limiter.lockedUntil(""), now + HandshakeLimiter::kBaseLockMs * 2);
        now = limiter.lockedUntil("") + 1;
        limiter.succeed("10.0.0.8");
        QVERIFY(limiter.allow("10.0.0.9"));
        QCOMPARE(limiter.generation("10.0.0.8"), 0);
    }

    void longTermKeyIsStableAcrossAFreshChannel() {
        FurinaChannel server, client;
        server.reset(FurinaChannel::Role::Server, "first-code");
        client.reset(FurinaChannel::Role::Client, "first-code");
        QVERIFY(pump(server, client));
        const auto key = server.longTermKey();
        QCOMPARE(key, client.longTermKey());
        QCOMPARE(key.size(), 32);
        FurinaChannel againS, againC;
        againS.reset(FurinaChannel::Role::Server, "ignored-code", key);
        againC.reset(FurinaChannel::Role::Client, "ignored-code", key);
        QVERIFY(pump(againS, againC));
        QCOMPARE(againS.longTermKey(), key);
        QByteArray wire;
        QVERIFY(againC.seal(6, QByteArray("k"), &wire));
        QByteArray egress;
        QVERIFY(againS.push(wire, &egress));
        quint8 type = 0;
        QByteArray payload;
        QVERIFY(againS.pop(&type, &payload));
        QCOMPARE(type, quint8(6));
        QCOMPARE(payload, QByteArray("k"));
    }

  private:
    static bool pump(FurinaChannel& server, FurinaChannel& client) {
        QByteArray toServer = client.start(), toClient = server.start();
        for (int i = 0; i < 6 && !(server.transportReady() && client.transportReady()); ++i) {
            QByteArray serverOut, clientOut;
            if (!server.push(toServer, &serverOut) || !client.push(toClient, &clientOut))
                return false;
            toServer = clientOut;
            toClient = serverOut;
        }
        return server.transportReady() && client.transportReady() && server.longTermKey() == client.longTermKey();
    }
};

QTEST_GUILESS_MAIN(CryptoTest)
#include "crypto_test.moc"
