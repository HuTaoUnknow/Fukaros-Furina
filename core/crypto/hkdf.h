#pragma once
#include <QByteArray>
#include <QCryptographicHash>
#include <QMessageAuthenticationCode>
#include <QtGlobal>

/* HKDF-SHA256 (RFC 5869). An empty salt is HashLen zero bytes. */
inline QByteArray furinaHmacSha256(const QByteArray& key, const QByteArray& message) {
    return QMessageAuthenticationCode::hash(message, key, QCryptographicHash::Sha256);
}

inline QByteArray furinaHkdfExtract(const QByteArray& salt, const QByteArray& ikm) {
    return furinaHmacSha256(salt.isEmpty() ? QByteArray(32, 0) : salt, ikm);
}

inline QByteArray furinaHkdfExpand(const QByteArray& prk, const QByteArray& info, int length) {
    QByteArray okm, block;
    for (int counter = 1; okm.size() < length; ++counter) {
        block = furinaHmacSha256(prk, block + info + QByteArray(1, char(counter)));
        okm += block;
    }
    okm.resize(length);
    return okm;
}

inline QByteArray furinaHkdf(const QByteArray& salt, const QByteArray& ikm, const QByteArray& info, int length) {
    return furinaHkdfExpand(furinaHkdfExtract(salt, ikm), info, length);
}
