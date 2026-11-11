#pragma once
#include <QString>

// Manual pairing codes. The alphabet is A–Z without I and O, plus digits 2–9.
struct PairingCodeCheck {
    bool ok = false;
    QString chinese;
    QString english;
};

QString pairingNormalizeManualCode(const QString& text);
PairingCodeCheck pairingCheckManualCode(const QString& text);
QString pairingGenerateManualCode();
