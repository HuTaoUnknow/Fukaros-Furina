#pragma once
#include "secret_store.h"
#include <QString>

// SHA-256 of the desktop long-term public key, lowercase hex, 64 characters.
QString pairingPublicKeyFingerprint(const QByteArray& publicKey);

// 32-byte desktop X25519 public key from furinaX25519PublicKey.
// The secret is stored as desktop-x25519-secret. The QR fingerprint hashes this public key.
QByteArray pairingDesktopPublicKey(SecretStore& store);

// Remember the fingerprint from a QR before any connection attempt.
// A different fingerprint for the same host is rejected.
bool pairingPinPublicKey(SecretStore& store, const QString& host, const QString& fingerprint);

// Single-use record for a one-time QR secret. Stores only a hash, plus the expiry.
bool pairingConsumeOneTimeSecret(SecretStore& store, const QString& secret, qint64 expiryUnix, qint64 nowUnix);
