#pragma once
#include <QString>

// QR text, UTF-8:
// fukaros://pair?v=2&h=HOST&p=PORT&s=ONE_TIME_SECRET&f=FINGERPRINT&e=UNIX_EXPIRY
inline constexpr int kPairingPayloadVersion = 2;
inline constexpr qint64 kPairingQrTtlSeconds = 120;
inline constexpr quint16 kPairingQrPort = 27183;

struct PairingPayload {
    int version = kPairingPayloadVersion;
    QString host;
    quint16 port = 0;
    QString secret;
    QString fingerprint;
    qint64 expiry = 0;
};

struct PairingDecodeResult {
    bool ok = false;
    PairingPayload payload;
    QString chinese;
    QString english;
};

bool pairingOneTimeSecretValid(const QString& secret);
QString pairingGenerateOneTimeSecret();
QString pairingEncodePayload(const PairingPayload& payload);
PairingDecodeResult pairingDecodePayload(const QString& text, qint64 nowUnix);
