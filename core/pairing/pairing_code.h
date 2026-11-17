#pragma once
#include <QString>

// Manual pairing codes. Any letters and digits are accepted; the code is only a
// shared password, so the length floor is what keeps it hard to guess.
inline constexpr int kPairingManualCodeMinLength = 6;
inline constexpr int kPairingGeneratedCodeLength = 8;

struct PairingCodeCheck {
    bool ok = false;
    QString chinese;
    QString english;
};

QString pairingNormalizeManualCode(const QString& text);
PairingCodeCheck pairingCheckManualCode(const QString& text);
QString pairingGenerateManualCode();
