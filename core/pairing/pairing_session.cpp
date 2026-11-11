#include "pairing_session.h"
#include "pairing_payload.h"
#include "secret_store.h"
#include <QMutex>
#include <QMutexLocker>
#include <QSet>

namespace {
QMutex g_attachedMutex;
QSet<Peer*> g_attached;

QString channelSecret(const PairingCredential& credential) {
    // Peer treats "ot1." plus 22 base64url characters as a single-use SPAKE2 password.
    // The QR field `s` does not include that prefix.
    if (credential.kind == PairingSecretKind::OneTimeQr)
        return QStringLiteral("ot1.") + credential.secret;
    return credential.secret;
}

void rememberEstablishedKey(Peer& peer) {
    static QObject* sink = new QObject;
    QMutexLocker lock(&g_attachedMutex);
    if (g_attached.contains(&peer))
        return;
    g_attached.insert(&peer);
    QObject::connect(&peer, &Peer::longTermKeyEstablished, &peer, [](const QByteArray& key) {
        if (key.size() == 32)
            SecretStore::instance().store(QStringLiteral("channel-long-term-key"), key);
    });
    QObject::connect(&peer, &QObject::destroyed, sink, [p = &peer] {
        QMutexLocker inner(&g_attachedMutex);
        g_attached.remove(p);
    });
}

void applyCredential(Peer& peer, const PairingCredential& credential) {
    rememberEstablishedKey(peer);
    // A QR attempt must use field `s`. A stored long-term key would ignore that secret.
    if (credential.kind == PairingSecretKind::OneTimeQr) {
        peer.setLongTermKey({});
        return;
    }
    // Load a key saved by an earlier process. Do not clear a key this Peer already
    // negotiated: the store write is queued, and wiping it would desync the reconnect.
    const auto stored = SecretStore::instance().load(QStringLiteral("channel-long-term-key"));
    if (stored.size() == 32)
        peer.setLongTermKey(stored);
}
} // namespace

bool pairingCredentialAccepts(const PairingCredential& credential) {
    if (credential.kind == PairingSecretKind::OneTimeQr)
        return pairingOneTimeSecretValid(credential.secret);
    return pairingCheckManualCode(credential.secret).ok;
}

bool pairingListen(Peer& peer, quint16 port, const PairingCredential& credential, const QString& bindAddress) {
    if (!pairingCredentialAccepts(credential))
        return false;
    applyCredential(peer, credential);
    return peer.listen(port, channelSecret(credential), bindAddress);
}

bool pairingConnect(Peer& peer, const QString& host, quint16 port, const PairingCredential& credential,
                    const QString& localAddress) {
    if (!pairingCredentialAccepts(credential))
        return false;
    applyCredential(peer, credential);
    peer.connectTo(host, port, channelSecret(credential), localAddress);
    return true;
}
