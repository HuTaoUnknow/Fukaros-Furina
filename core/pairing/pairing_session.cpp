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

// Remember every key a Peer negotiates. The server generates it; the client receives it.
// Both ends therefore keep the same 32-byte key for later session connections.
void rememberEstablishedKey(Peer& peer) {
    static QObject* sink = new QObject;
    QMutexLocker lock(&g_attachedMutex);
    if (g_attached.contains(&peer))
        return;
    g_attached.insert(&peer);
    QObject::connect(&peer, &Peer::longTermKeyEstablished, &peer, [](const QByteArray& key) {
        if (key.size() == 32)
            SecretStore::instance().store(QString(kChannelLongTermKey), key);
    });
    QObject::connect(&peer, &QObject::destroyed, sink, [p = &peer] {
        QMutexLocker inner(&g_attachedMutex);
        g_attached.remove(p);
    });
}

void applyCredential(Peer& peer) {
    rememberEstablishedKey(peer);
    // Explicit pairing always authenticates with the pairing code or the QR secret.
    // A stored long-term key must not replace it: that is how two ends holding different
    // old keys end up comparing different SPAKE2 passwords and reject every code.
    peer.setLongTermKey({});
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
    applyCredential(peer);
    return peer.listen(port, channelSecret(credential), bindAddress);
}

bool pairingConnect(Peer& peer, const QString& host, quint16 port, const PairingCredential& credential,
                    const QString& localAddress) {
    if (!pairingCredentialAccepts(credential))
        return false;
    applyCredential(peer);
    peer.connectTo(host, port, channelSecret(credential), localAddress);
    return true;
}

QByteArray pairingStoredKey(Peer& peer) {
    const auto preset = peer.longTermKey();
    if (preset.size() == 32)
        return preset;
    const auto stored = SecretStore::instance().load(QString(kChannelLongTermKey));
    return stored.size() == 32 ? stored : QByteArray();
}

bool pairingStoredKeyAvailable(Peer& peer) {
    return pairingStoredKey(peer).size() == 32;
}

bool sessionListen(Peer& peer, quint16 port, const QString& bindAddress) {
    const auto key = pairingStoredKey(peer);
    // Without a key the token would be a public constant and anyone on the LAN could connect.
    if (key.size() != 32)
        return false;
    applyCredential(peer);
    peer.setLongTermKey(key);
    return peer.listen(port, kSessionToken, bindAddress);
}

bool sessionConnect(Peer& peer, const QString& host, quint16 port, const QString& localAddress) {
    const auto key = pairingStoredKey(peer);
    // Without a key the token would be a public constant and anyone on the LAN could connect.
    if (key.size() != 32)
        return false;
    applyCredential(peer);
    peer.setLongTermKey(key);
    peer.connectTo(host, port, kSessionToken, localAddress);
    return true;
}
