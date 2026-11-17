#include "pairing_code.h"
#include <QRandomGenerator>

namespace {
const QString kAlphabet = QStringLiteral("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789");

PairingCodeCheck fail(const char* chinese, const char* english) {
    PairingCodeCheck result;
    result.chinese = QString::fromUtf8(chinese);
    result.english = QString::fromLatin1(english);
    return result;
}

int alphabetIndex(QChar character) {
    return kAlphabet.indexOf(character);
}
} // namespace

QString pairingNormalizeManualCode(const QString& text) {
    return text.trimmed().toUpper();
}

PairingCodeCheck pairingCheckManualCode(const QString& text) {
    const auto code = pairingNormalizeManualCode(text);
    // Peer's transport takes any token of six bytes or more. Keep that floor so a
    // short code cannot start a listener that the other side refuses.
    if (code.size() < kPairingManualCodeMinLength)
        return fail("配对码至少 6 位。", "Pairing code must be at least 6 characters.");
    for (const auto character : code) {
        if (alphabetIndex(character) < 0)
            return fail("配对码只能使用英文字母和数字。", "Pairing code may use letters and digits only.");
    }
    bool repeated = true;
    for (int i = 1; i < code.size(); ++i) {
        if (code.at(i) != code.at(0)) {
            repeated = false;
            break;
        }
    }
    if (repeated)
        return fail("配对码不能是同一个字符重复。", "Pairing code cannot repeat one character only.");
    PairingCodeCheck ok;
    ok.ok = true;
    return ok;
}

QString pairingGenerateManualCode() {
    for (int attempt = 0; attempt < 8; ++attempt) {
        QString code;
        code.reserve(kPairingGeneratedCodeLength);
        for (int i = 0; i < kPairingGeneratedCodeLength; ++i)
            code.append(kAlphabet.at(QRandomGenerator::system()->bounded(kAlphabet.size())));
        if (pairingCheckManualCode(code).ok)
            return code;
    }
    return {};
}
