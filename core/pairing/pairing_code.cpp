#include "pairing_code.h"
#include <QRandomGenerator>

namespace {
const QString kAlphabet = QStringLiteral("ABCDEFGHJKLMNPQRSTUVWXYZ23456789");

PairingCodeCheck fail(const char* chinese, const char* english) {
    PairingCodeCheck result;
    result.chinese = QString::fromUtf8(chinese);
    result.english = QString::fromLatin1(english);
    return result;
}

int alphabetIndex(QChar character) {
    return kAlphabet.indexOf(character);
}

bool simpleSequence(const QString& code) {
    if (code.size() < 4)
        return false;
    for (int i = 0; i + 3 < code.size(); ++i) {
        const int a = alphabetIndex(code.at(i));
        const int b = alphabetIndex(code.at(i + 1));
        const int c = alphabetIndex(code.at(i + 2));
        const int d = alphabetIndex(code.at(i + 3));
        if (a < 0 || b < 0 || c < 0 || d < 0)
            continue;
        const int step1 = b - a, step2 = c - b, step3 = d - c;
        if ((step1 == 1 && step2 == 1 && step3 == 1) || (step1 == -1 && step2 == -1 && step3 == -1))
            return true;
    }
    return false;
}
} // namespace

QString pairingNormalizeManualCode(const QString& text) {
    return text.trimmed().toUpper();
}

PairingCodeCheck pairingCheckManualCode(const QString& text) {
    const auto code = pairingNormalizeManualCode(text);
    if (code.size() < 12)
        return fail("配对码至少 12 位，并且同时包含字母和数字。",
                    "Pairing code must be at least 12 characters and include both a letter and a digit.");
    for (const auto character : code) {
        if (alphabetIndex(character) < 0)
            return fail("配对码只能使用字母 A–Z（不含 I、O）和数字 2–9。",
                        "Pairing code may use A-Z except I and O, and digits 2-9.");
    }
    bool repeated = true;
    for (int i = 1; i < code.size(); ++i) {
        if (code.at(i) != code.at(0)) {
            repeated = false;
            break;
        }
    }
    if (repeated)
        return fail("配对码不能是重复的单一字符，也不能包含简单序列（例如 ABCD、2345）。",
                    "Pairing code cannot be one repeated character or contain a simple sequence such as ABCD or 2345.");
    bool letter = false, digit = false;
    for (const auto character : code) {
        if (character.isDigit())
            digit = true;
        else
            letter = true;
    }
    if (!letter || !digit)
        return fail("配对码至少 12 位，并且同时包含字母和数字。",
                    "Pairing code must be at least 12 characters and include both a letter and a digit.");
    if (simpleSequence(code))
        return fail("配对码不能是重复的单一字符，也不能包含简单序列（例如 ABCD、2345）。",
                    "Pairing code cannot be one repeated character or contain a simple sequence such as ABCD or 2345.");
    PairingCodeCheck ok;
    ok.ok = true;
    return ok;
}

QString pairingGenerateManualCode() {
    for (int attempt = 0; attempt < 64; ++attempt) {
        QString code;
        code.reserve(12);
        const int letterAt = QRandomGenerator::system()->bounded(12);
        int digitAt = QRandomGenerator::system()->bounded(11);
        if (digitAt >= letterAt)
            ++digitAt;
        for (int i = 0; i < 12; ++i) {
            QChar character;
            if (i == letterAt) {
                character = kAlphabet.at(QRandomGenerator::system()->bounded(24));
            } else if (i == digitAt) {
                character = kAlphabet.at(24 + QRandomGenerator::system()->bounded(8));
            } else {
                character = kAlphabet.at(QRandomGenerator::system()->bounded(kAlphabet.size()));
            }
            code.append(character);
        }
        if (pairingCheckManualCode(code).ok)
            return code;
    }
    return {};
}
