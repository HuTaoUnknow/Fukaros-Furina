#include "pairing_payload.h"
#include <QHash>
#include <QRandomGenerator>
#include <QUrl>

namespace {
PairingDecodeResult fail(const char* chinese, const char* english) {
    PairingDecodeResult result;
    result.chinese = QString::fromUtf8(chinese);
    result.english = QString::fromLatin1(english);
    return result;
}

const PairingDecodeResult kInvalid =
    fail("二维码无效。", "QR code is invalid.");

bool decimalInt(const QString& text, qint64* value) {
    if (text.isEmpty())
        return false;
    qint64 parsed = 0;
    for (const auto character : text) {
        if (character < QLatin1Char('0') || character > QLatin1Char('9'))
            return false;
        parsed = parsed * 10 + (character.unicode() - '0');
        if (parsed > 20000000000LL)
            return false;
    }
    *value = parsed;
    return true;
}
} // namespace

bool pairingOneTimeSecretValid(const QString& secret) {
    if (secret.size() != 22)
        return false;
    for (const auto character : secret) {
        const auto code = character.unicode();
        const bool allowed = (code >= 'A' && code <= 'Z') || (code >= 'a' && code <= 'z') ||
                             (code >= '0' && code <= '9') || code == '-' || code == '_';
        if (!allowed)
            return false;
    }
    const auto raw = QByteArray::fromBase64(secret.toLatin1(), QByteArray::Base64UrlEncoding |
                                                                   QByteArray::OmitTrailingEquals);
    if (raw.size() != 16)
        return false;
    return raw.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals) == secret.toLatin1();
}

QString pairingGenerateOneTimeSecret() {
    QByteArray raw(16, 0);
    QRandomGenerator::system()->generate(raw.begin(), raw.end());
    const auto secret =
        QString::fromLatin1(raw.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
    return pairingOneTimeSecretValid(secret) ? secret : QString();
}

QString pairingEncodePayload(const PairingPayload& payload) {
    if (payload.version != kPairingPayloadVersion || payload.host.isEmpty() || payload.port == 0 ||
        !pairingOneTimeSecretValid(payload.secret) || payload.fingerprint.size() != 64 || payload.expiry <= 0)
        return {};
    for (const auto character : payload.fingerprint) {
        const auto code = character.unicode();
        const bool hex = (code >= '0' && code <= '9') || (code >= 'a' && code <= 'f') ||
                         (code >= 'A' && code <= 'F');
        if (!hex)
            return {};
    }
    if (payload.host.size() > 253 || payload.host.contains(QLatin1Char('&')) ||
        payload.host.contains(QLatin1Char('?')) || payload.host.contains(QLatin1Char('#')) ||
        payload.host.contains(QLatin1Char(' ')) || payload.host.contains(QLatin1Char('/')))
        return {};
    return QStringLiteral("fukaros://pair?v=%1&h=%2&p=%3&s=%4&f=%5&e=%6")
        .arg(payload.version)
        .arg(QString::fromUtf8(QUrl::toPercentEncoding(payload.host)))
        .arg(payload.port)
        .arg(payload.secret)
        .arg(payload.fingerprint.toLower())
        .arg(payload.expiry);
}

PairingDecodeResult pairingDecodePayload(const QString& text, qint64 nowUnix) {
    const QString prefix = QStringLiteral("fukaros://pair?");
    if (!text.startsWith(prefix) || text.contains(QLatin1Char('#')))
        return kInvalid;
    const auto query = text.mid(prefix.size());
    if (query.isEmpty() || query.contains(QLatin1Char('?')))
        return kInvalid;
    QHash<QString, QString> fields;
    for (const auto& part : query.split(QLatin1Char('&'))) {
        const auto eq = part.indexOf(QLatin1Char('='));
        if (eq <= 0)
            return kInvalid;
        const auto key = part.left(eq);
        if (fields.contains(key))
            return kInvalid;
        fields.insert(key, part.mid(eq + 1));
    }
    const QStringList required{QStringLiteral("v"), QStringLiteral("h"), QStringLiteral("p"),
                               QStringLiteral("s"), QStringLiteral("f"), QStringLiteral("e")};
    if (fields.size() != required.size())
        return kInvalid;
    for (const auto& key : required) {
        if (!fields.contains(key))
            return kInvalid;
    }
    if (fields.value(QStringLiteral("v")) != QLatin1String("2"))
        return fail("二维码版本不受支持。", "QR code version is not supported.");

    const auto host = QUrl::fromPercentEncoding(fields.value(QStringLiteral("h")).toUtf8());
    if (host.isEmpty() || host.size() > 253 || host.contains(QLatin1Char('&')) ||
        host.contains(QLatin1Char('?')) || host.contains(QLatin1Char('#')) || host.contains(QLatin1Char(' ')) ||
        host.contains(QLatin1Char('/')))
        return fail("地址或端口无效。", "Host or port is invalid.");
    qint64 port = 0;
    if (!decimalInt(fields.value(QStringLiteral("p")), &port) || port < 1 || port > 65535)
        return fail("地址或端口无效。", "Host or port is invalid.");
    const auto secret = fields.value(QStringLiteral("s"));
    if (!pairingOneTimeSecretValid(secret))
        return fail("一次性密钥无效。", "One-time secret is invalid.");
    auto fingerprint = fields.value(QStringLiteral("f"));
    if (fingerprint.size() != 64)
        return fail("公钥指纹无效。", "Public key fingerprint is invalid.");
    for (const auto character : fingerprint) {
        const auto code = character.unicode();
        const bool hex = (code >= '0' && code <= '9') || (code >= 'a' && code <= 'f') ||
                         (code >= 'A' && code <= 'F');
        if (!hex)
            return fail("公钥指纹无效。", "Public key fingerprint is invalid.");
    }
    fingerprint = fingerprint.toLower();
    qint64 expiry = 0;
    if (!decimalInt(fields.value(QStringLiteral("e")), &expiry) || expiry <= 0)
        return kInvalid;
    if (expiry <= nowUnix)
        return fail("二维码已过期。", "QR code has expired.");
    if (expiry - nowUnix > kPairingQrTtlSeconds)
        return fail("二维码无效。", "QR code is invalid.");

    PairingDecodeResult result;
    result.ok = true;
    result.payload.version = kPairingPayloadVersion;
    result.payload.host = host;
    result.payload.port = static_cast<quint16>(port);
    result.payload.secret = secret;
    result.payload.fingerprint = fingerprint;
    result.payload.expiry = expiry;
    return result;
}
