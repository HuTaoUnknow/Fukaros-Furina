#include "pairing_identity.h"
#include "furina_channel.h"
#include "monocypher.h"
#include <QCryptographicHash>
#include <QList>
#include <QUrl>

namespace {
QString hashSecret(const QString& secret) {
    return QString::fromLatin1(
        QCryptographicHash::hash(secret.toUtf8(), QCryptographicHash::Sha256).toHex());
}

QList<QPair<QString, qint64>> readUsed(SecretStore& store, qint64 nowUnix) {
    QList<QPair<QString, qint64>> rows;
    const auto blob = store.load(QStringLiteral("used-secrets"));
    for (const auto& line : QString::fromUtf8(blob).split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        const auto parts = line.split(QLatin1Char(' '));
        if (parts.size() != 2)
            continue;
        bool ok = false;
        const auto expiry = parts.at(1).toLongLong(&ok);
        if (!ok || expiry <= nowUnix || parts.at(0).size() != 64)
            continue;
        rows.push_back({parts.at(0), expiry});
    }
    return rows;
}

bool writeUsed(SecretStore& store, const QList<QPair<QString, qint64>>& rows) {
    QStringList lines;
    for (const auto& row : rows)
        lines << row.first + QLatin1Char(' ') + QString::number(row.second);
    const auto blob = lines.join(QLatin1Char('\n')).toUtf8();
    if (blob.isEmpty())
        return store.remove(QStringLiteral("used-secrets"));
    return store.store(QStringLiteral("used-secrets"), blob);
}
} // namespace

QString pairingPublicKeyFingerprint(const QByteArray& publicKey) {
    if (publicKey.isEmpty())
        return {};
    return QString::fromLatin1(QCryptographicHash::hash(publicKey, QCryptographicHash::Sha256).toHex());
}

QByteArray pairingDesktopPublicKey(SecretStore& store) {
    auto secret = store.load(QStringLiteral("desktop-x25519-secret"));
    if (secret.size() != 32) {
        secret = furinaX25519Secret();
        if (secret.size() != 32 || !store.store(QStringLiteral("desktop-x25519-secret"), secret))
            return {};
    }
    const auto publicKey = furinaX25519PublicKey(secret);
    secret.detach();
    crypto_wipe(secret.data(), size_t(secret.size()));
    secret.fill('\0');
    return publicKey;
}

bool pairingPinPublicKey(SecretStore& store, const QString& host, const QString& fingerprint) {
    const auto normalized = fingerprint.toLower();
    if (host.isEmpty() || normalized.size() != 64)
        return false;
    for (const auto character : normalized) {
        const auto code = character.unicode();
        const bool hex = (code >= '0' && code <= '9') || (code >= 'a' && code <= 'f');
        if (!hex)
            return false;
    }
    const auto key = QStringLiteral("pin-%1").arg(QString::fromLatin1(QUrl::toPercentEncoding(host)));
    const auto existing = store.load(key);
    if (!existing.isEmpty())
        return existing == normalized.toLatin1();
    return store.store(key, normalized.toLatin1());
}

bool pairingConsumeOneTimeSecret(SecretStore& store, const QString& secret, qint64 expiryUnix, qint64 nowUnix) {
    if (secret.isEmpty() || expiryUnix <= nowUnix)
        return false;
    auto rows = readUsed(store, nowUnix);
    const auto hash = hashSecret(secret);
    for (const auto& row : rows) {
        if (row.first == hash)
            return false;
    }
    rows.push_back({hash, expiryUnix});
    return writeUsed(store, rows);
}
