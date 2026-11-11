#pragma once
#include "pairing_code.h"
#include "peer.h"
#include <QString>

// Callers pass either the human pairing code or the one-time QR secret.
// pairingListen and pairingConnect feed that secret into the FUR2 SPAKE2 handshake.
// There is no plaintext or HMAC fallback.
enum class PairingSecretKind { ManualCode, OneTimeQr };

struct PairingCredential {
    QString secret;
    PairingSecretKind kind = PairingSecretKind::ManualCode;
    QString pinnedFingerprint;
};

bool pairingCredentialAccepts(const PairingCredential& credential);

// OneTimeQr.secret is QR field `s` (22-character base64url). The handshake token is "ot1." plus that field.
// ManualCode.secret is the human pairing code and is the SPAKE2 password until a long-term key is stored.
bool pairingListen(Peer& peer, quint16 port, const PairingCredential& credential,
                   const QString& bindAddress = QString());

bool pairingConnect(Peer& peer, const QString& host, quint16 port, const PairingCredential& credential,
                    const QString& localAddress = QString());
