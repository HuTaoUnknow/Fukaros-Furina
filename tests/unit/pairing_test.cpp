#include <QtTest>
#include <QDateTime>
#include <QFile>
#include <QSettings>
#include <QTcpSocket>
#include <QUrl>
#include "furina_channel.h"
#include "pairing_code.h"
#include "pairing_identity.h"
#include "pairing_payload.h"
#include "pairing_qr.h"
#include "pairing_session.h"
#include "peer.h"
#include "secret_store.h"

class PairingTest : public QObject {
    Q_OBJECT
  private slots:
    void initTestCase() {
        QCoreApplication::setOrganizationName("FurinaTests");
        QCoreApplication::setApplicationName("PairingCodec");
    }

    void init() {
        QSettings settings;
        settings.clear();
        settings.sync();
        SecretStore store(settings);
        for (const auto& key : {QStringLiteral("manual-code"), QStringLiteral("used-secrets"),
                                QStringLiteral("desktop-public-key"), QStringLiteral("desktop-x25519-secret"),
                                QStringLiteral("channel-long-term-key")}) {
            store.remove(key);
            SecretStore::instance().remove(key);
        }
    }

    void manualCodeRules() {
        QVERIFY(!pairingCheckManualCode("K7M2P9QX4").ok);
        QVERIFY(!pairingCheckManualCode("k7m2p9qx4r").ok);
        QCOMPARE(pairingNormalizeManualCode("  k7m2p9qx4r8n "), QString("K7M2P9QX4R8N"));
        QVERIFY(pairingCheckManualCode("k7m2p9qx4r8n").ok);
        QVERIFY(!pairingCheckManualCode("ABCDEFGHJKLM").ok);
        QVERIFY(!pairingCheckManualCode("234567892345").ok);
        QVERIFY(!pairingCheckManualCode("IIIIIIIIIIII").ok);
        QVERIFY(!pairingCheckManualCode("OOOOOOOOOOOO").ok);
        QVERIFY(!pairingCheckManualCode("000000000000").ok);
        QVERIFY(!pairingCheckManualCode("111111111111").ok);
        QVERIFY(!pairingCheckManualCode("AAAAAAAAAAAA").ok);
        QVERIFY(!pairingCheckManualCode("222222222222").ok);
        QVERIFY(!pairingCheckManualCode("ABCD23456789").ok);
        QVERIFY(!pairingCheckManualCode("ZZZZZ5432ZZZ").ok);
        QVERIFY(!pairingCheckManualCode("K7M2-P9QX4R8").ok);
        QVERIFY(pairingCheckManualCode("A2B3C4D5E6F7").ok);
        QVERIFY(pairingCheckManualCode("H3R8K2M7Q4P9").ok);
        const auto generated = pairingGenerateManualCode();
        QVERIFY(pairingCheckManualCode(generated).ok);
        QCOMPARE(generated.size(), 12);
    }

    void payloadRoundTripExpiryTamperAndReuse() {
        const qint64 now = 1'700'000'000;
        PairingPayload payload;
        payload.host = "192.168.4.20";
        payload.port = 27183;
        payload.secret = pairingGenerateOneTimeSecret();
        payload.fingerprint = QString(64, QLatin1Char('a'));
        payload.expiry = now + kPairingQrTtlSeconds;
        QVERIFY(pairingOneTimeSecretValid(payload.secret));
        const auto text = pairingEncodePayload(payload);
        QVERIFY(text.startsWith("fukaros://pair?v=2&h=192.168.4.20&p=27183&s="));
        const auto decoded = pairingDecodePayload(text, now);
        QVERIFY(decoded.ok);
        QCOMPARE(decoded.payload.host, payload.host);
        QCOMPARE(decoded.payload.port, payload.port);
        QCOMPARE(decoded.payload.secret, payload.secret);
        QCOMPARE(decoded.payload.fingerprint, payload.fingerprint);
        QCOMPARE(decoded.payload.expiry, payload.expiry);
        QVERIFY(pairingDecodePayload(text, now + 119).ok);
        QVERIFY(!pairingDecodePayload(text, payload.expiry).ok);
        QCOMPARE(pairingDecodePayload(text, payload.expiry).english, QString("QR code has expired."));
        payload.expiry = now + kPairingQrTtlSeconds + 1;
        QVERIFY(!pairingDecodePayload(pairingEncodePayload(payload), now).ok);
        payload.expiry = now + kPairingQrTtlSeconds;

        auto tampered = text;
        tampered[tampered.size() - 1] = tampered.back() == QLatin1Char('0') ? QLatin1Char('1') : QLatin1Char('0');
        QVERIFY(!pairingDecodePayload(tampered, now).ok);
        QVERIFY(!pairingDecodePayload(text + "&x=1", now).ok);
        QVERIFY(!pairingDecodePayload(QString(text).replace("v=2", "v=1"), now).ok);
        QVERIFY(!pairingDecodePayload(QString(text).replace(payload.secret, payload.secret.left(21) + "+"), now).ok);
        QVERIFY(!pairingDecodePayload("http://pair?" + text.mid(QString("fukaros://pair?").size()), now).ok);

        QSettings settings;
        SecretStore store(settings);
        QVERIFY(pairingConsumeOneTimeSecret(store, payload.secret, payload.expiry, now));
        QVERIFY(!pairingConsumeOneTimeSecret(store, payload.secret, payload.expiry, now));
        const auto persisted = store.load("used-secrets");
        QVERIFY(!persisted.isEmpty());
        QVERIFY(!persisted.contains(payload.secret.toUtf8()));
    }

    void qrImageRoundTrip() {
        const auto secret = pairingGenerateOneTimeSecret();
        PairingPayload payload;
        payload.host = "10.0.0.8";
        payload.port = kPairingQrPort;
        payload.secret = secret;
        payload.fingerprint = pairingPublicKeyFingerprint(QByteArray(32, 'K'));
        payload.expiry = 50;
        const auto text = pairingEncodePayload(payload);
        const auto image = pairingQrImage(text, 8);
        QVERIFY(!image.isNull());
        const auto read = pairingReadQrImage(image);
        QCOMPARE(read, QStringList{text});
    }

    void fingerprintPinAndMigration() {
        QCOMPARE(pairingPublicKeyFingerprint(QByteArray(32, 'K')).size(), 64);
        QSettings settings;
        SecretStore store(settings);
        const auto key = pairingDesktopPublicKey(store);
        QCOMPARE(key.size(), 32);
        QCOMPARE(pairingDesktopPublicKey(store), key);
        const auto fingerprint = pairingPublicKeyFingerprint(key);
        QVERIFY(pairingPinPublicKey(store, "10.0.0.8", fingerprint));
        QVERIFY(pairingPinPublicKey(store, "10.0.0.8", fingerprint.toUpper()));
        QVERIFY(!pairingPinPublicKey(store, "10.0.0.8", QString(64, QLatin1Char('b'))));

        const QString legacy = "H3R8K2M7Q4P9";
        settings.setValue("token", legacy);
        settings.sync();
        QCOMPARE(QString::fromUtf8(pairingMigratePlaintext(store, settings, "token", "manual-code")), legacy);
        QVERIFY(!settings.contains("token"));
        QCOMPARE(QString::fromUtf8(store.load("manual-code")), legacy);
        settings.sync();
        QFile file(settings.fileName());
        if (file.open(QIODevice::ReadOnly))
            QVERIFY(!QString::fromUtf8(file.readAll()).contains(legacy));
        for (const auto& stored : settings.allKeys())
            QVERIFY(!settings.value(stored).toString().contains(legacy));
#ifdef Q_OS_WIN
        const auto blob = settings.value("secure/manual-code").toByteArray();
        QVERIFY(!blob.isEmpty());
        QVERIFY(!blob.contains(legacy.toUtf8()));
#endif
    }

    void desktopKeyIsTheChannelX25519PublicKey() {
        QSettings settings;
        SecretStore store(settings);
        const auto key = pairingDesktopPublicKey(store);
        const auto secret = store.load(QStringLiteral("desktop-x25519-secret"));
        QCOMPARE(key.size(), 32);
        QCOMPARE(secret.size(), 32);
        QCOMPARE(furinaX25519PublicKey(secret), key);
        QVERIFY(key != secret);
        QCOMPARE(pairingDesktopPublicKey(store), key);
        QVERIFY(store.load(QStringLiteral("desktop-public-key")).isEmpty());
    }

    void handshakeUsesTheQrSecretAndTheManualCode() {
        PairingCredential manual;
        manual.kind = PairingSecretKind::ManualCode;
        manual.secret = QStringLiteral("A2B3C4D5E6F7");
        Peer server;
        Peer client;
        QVERIFY(pairingListen(server, 0, manual, QStringLiteral("127.0.0.1")));
        QVERIFY(server.serverPort() != 0);
        QVERIFY(pairingConnect(client, QStringLiteral("127.0.0.1"), server.serverPort(), manual));
        QTRY_VERIFY_WITH_TIMEOUT(server.ready() && client.ready(), 5000);
        const auto established = server.establishedLongTermKey();
        QCOMPARE(established.size(), 32);
        QCOMPARE(client.establishedLongTermKey(), established);
        QTRY_COMPARE(SecretStore::instance().load(QStringLiteral("channel-long-term-key")), established);

        PairingCredential other;
        other.kind = PairingSecretKind::ManualCode;
        other.secret = QStringLiteral("H3R8K2M7Q4P9");
        Peer laterServer;
        Peer laterClient;
        QVERIFY(pairingListen(laterServer, 0, other, QStringLiteral("127.0.0.1")));
        QVERIFY(pairingConnect(laterClient, QStringLiteral("127.0.0.1"), laterServer.serverPort(), manual));
        QTRY_VERIFY_WITH_TIMEOUT(laterServer.ready() && laterClient.ready(), 5000);
        QCOMPARE(laterServer.establishedLongTermKey(), established);

        SecretStore::instance().remove(QStringLiteral("channel-long-term-key"));
        Peer mismatchServer;
        Peer mismatchClient;
        QVERIFY(pairingListen(mismatchServer, 0, manual, QStringLiteral("127.0.0.1")));
        QVERIFY(pairingConnect(mismatchClient, QStringLiteral("127.0.0.1"), mismatchServer.serverPort(), other));
        QTest::qWait(350);
        QVERIFY(!mismatchServer.ready());
        QVERIFY(!mismatchClient.ready());
        mismatchServer.close();
        mismatchClient.close();

        Peer legacyServer;
        QVERIFY(pairingListen(legacyServer, 0, manual, QStringLiteral("127.0.0.1")));
        QTcpSocket legacy;
        legacy.connectToHost(QStringLiteral("127.0.0.1"), legacyServer.serverPort());
        QTRY_COMPARE(legacy.state(), QAbstractSocket::ConnectedState);
        legacy.write("FUR1");
        legacy.flush();
        QTRY_COMPARE(legacy.state(), QAbstractSocket::UnconnectedState);
        QVERIFY(!legacyServer.ready());

        const auto junk = QByteArray(32, 'K');
        SecretStore::instance().store(QStringLiteral("channel-long-term-key"), junk);
        PairingCredential once;
        once.kind = PairingSecretKind::OneTimeQr;
        once.secret = pairingGenerateOneTimeSecret();
        Peer qrServer;
        Peer qrClient;
        QVERIFY(pairingListen(qrServer, 0, once, QStringLiteral("127.0.0.1")));
        QVERIFY(pairingConnect(qrClient, QStringLiteral("127.0.0.1"), qrServer.serverPort(), once));
        QTRY_VERIFY_WITH_TIMEOUT(qrServer.ready() && qrClient.ready(), 5000);
        QVERIFY(qrServer.establishedLongTermKey() != junk);
        QCOMPARE(qrServer.establishedLongTermKey(), qrClient.establishedLongTermKey());
        Peer replay;
        QVERIFY(!pairingListen(replay, 0, once, QStringLiteral("127.0.0.1")));
    }

    void credentialKinds() {
        PairingCredential manual;
        manual.kind = PairingSecretKind::ManualCode;
        manual.secret = "short";
        QVERIFY(!pairingCredentialAccepts(manual));
        manual.secret = "A2B3C4D5E6F7";
        QVERIFY(pairingCredentialAccepts(manual));
        PairingCredential once;
        once.kind = PairingSecretKind::OneTimeQr;
        once.secret = pairingGenerateOneTimeSecret();
        QVERIFY(pairingCredentialAccepts(once));
        once.secret[0] = QLatin1Char('+');
        QVERIFY(!pairingCredentialAccepts(once));
    }
};

QTEST_MAIN(PairingTest)
#include "pairing_test.moc"
