#pragma once
#include "pairing_code.h"
#include "peer.h"
#include <QString>

// Callers pass either the human pairing code or the one-time QR secret.
// feat/encrypted-channel replaces the Peer calls inside pairingListen and
// pairingConnect. Do not pass the one-time secret as an HMAC key.
enum class PairingSecretKind { ManualCode, OneTimeQr };

struct PairingCredential {
    QString secret;
    PairingSecretKind kind = PairingSecretKind::ManualCode;
    QString pinnedFingerprint;
};

bool pairingCredentialAccepts(const PairingCredential& credential);

// CHANNEL-HANDOFF: the body calls Peer::listen(port, credential.secret, bindAddress).
bool pairingListen(Peer& peer, quint16 port, const PairingCredential& credential,
                   const QString& bindAddress = QString());

// CHANNEL-HANDOFF: the body calls Peer::connectTo(host, port, credential.secret, localAddress).
bool pairingConnect(Peer& peer, const QString& host, quint16 port, const PairingCredential& credential,
                    const QString& localAddress = QString());
