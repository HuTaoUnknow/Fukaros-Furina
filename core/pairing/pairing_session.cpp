#include "pairing_session.h"
#include "pairing_payload.h"

bool pairingCredentialAccepts(const PairingCredential& credential) {
    if (credential.kind == PairingSecretKind::OneTimeQr)
        return pairingOneTimeSecretValid(credential.secret);
    return pairingCheckManualCode(credential.secret).ok;
}

bool pairingListen(Peer& peer, quint16 port, const PairingCredential& credential, const QString& bindAddress) {
    if (!pairingCredentialAccepts(credential))
        return false;
    // CHANNEL-HANDOFF: feat/encrypted-channel replaces this call.
    // OneTimeQr.secret is the single-use base64url secret from the QR field `s`.
    // ManualCode.secret is the human pairing code. Neither may be sent, and the
    // one-time secret must not be installed as an HMAC key.
    return peer.listen(port, credential.secret, bindAddress);
}

bool pairingConnect(Peer& peer, const QString& host, quint16 port, const PairingCredential& credential,
                    const QString& localAddress) {
    if (!pairingCredentialAccepts(credential))
        return false;
    // CHANNEL-HANDOFF: feat/encrypted-channel replaces this call.
    // OneTimeQr.secret is the single-use base64url secret from the QR field `s`.
    // ManualCode.secret is the human pairing code. Neither may be sent, and the
    // one-time secret must not be installed as an HMAC key.
    peer.connectTo(host, port, credential.secret, localAddress);
    return true;
}
