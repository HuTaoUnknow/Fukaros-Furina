#pragma once
#include "pairing_code.h"
#include "peer.h"
#include <QByteArray>
#include <QLatin1StringView>
#include <QString>

// Two kinds of FUR2 connection live here:
//
//  * Explicit pairing (pairingListen / pairingConnect) always authenticates with the
//    human pairing code or the one-time QR secret. The first success produces a fresh
//    32-byte long-term key on the server; both ends store it.
//  * Session connections (sessionListen / sessionConnect) authenticate with that stored
//    long-term key alone. They refuse to start without one, so the password can never be
//    a constant that anyone on the LAN knows.
//
// There is no plaintext or HMAC fallback.
enum class PairingSecretKind { ManualCode, OneTimeQr };

struct PairingCredential {
    QString secret;
    PairingSecretKind kind = PairingSecretKind::ManualCode;
    QString pinnedFingerprint;
};

inline constexpr QLatin1StringView kSessionToken("FUR2-LTK");
inline constexpr QLatin1StringView kChannelLongTermKey("channel-long-term-key");
inline constexpr QLatin1StringView kNoPairingKey("请先在设置中完成配对");
inline constexpr QLatin1StringView kNoPairingKeyEn("Pair in Settings first.");

bool pairingCredentialAccepts(const PairingCredential& credential);

// OneTimeQr.secret is QR field `s` (22-character base64url). The handshake token is "ot1." plus that field.
// ManualCode.secret is the human pairing code and is always the SPAKE2 password.
bool pairingListen(Peer& peer, quint16 port, const PairingCredential& credential,
                   const QString& bindAddress = QString());

bool pairingConnect(Peer& peer, const QString& host, quint16 port, const PairingCredential& credential,
                    const QString& localAddress = QString());

// The 32-byte key set on `peer` if there is one, otherwise the one saved by an earlier process.
// Empty when neither exists.
QByteArray pairingStoredKey(Peer& peer);
bool pairingStoredKeyAvailable(Peer& peer);

// Session connections. Both return false without touching the transport when no key is stored.
bool sessionListen(Peer& peer, quint16 port, const QString& bindAddress = QString());
bool sessionConnect(Peer& peer, const QString& host, quint16 port,
                    const QString& localAddress = QString());
