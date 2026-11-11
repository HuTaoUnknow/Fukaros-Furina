#include "secure_store.h"
#include "monocypher.h"
#include <QFile>
#include <QRandomGenerator>
#include <QSettings>
#include <QStandardPaths>
#include <QDir>
#ifdef Q_OS_WIN
#include <windows.h>
#include <wincrypt.h>
#endif
#ifdef Q_OS_ANDROID
#include <QJniEnvironment>
#include <QJniObject>
#endif

namespace {
QString settingsKey(const QString& name) {
    return QString("secure/%1").arg(name);
}

void randomBytes(uint8_t* out, int n) {
    for (int i = 0; i < n;) {
        const quint32 value = QRandomGenerator::system()->generate();
        for (int shift = 0; shift < 32 && i < n; shift += 8)
            out[i++] = uint8_t(value >> shift);
    }
}

#ifdef Q_OS_WIN
QByteArray protect(const QByteArray& plain) {
    DATA_BLOB in{}, out{};
    in.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(plain.data()));
    in.cbData = DWORD(plain.size());
    if (!CryptProtectData(&in, L"Fukaros Furina", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out))
        return {};
    QByteArray blob(reinterpret_cast<char*>(out.pbData), int(out.cbData));
    LocalFree(out.pbData);
    return blob;
}

QByteArray unprotect(const QByteArray& blob) {
    DATA_BLOB in{}, out{};
    in.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(blob.data()));
    in.cbData = DWORD(blob.size());
    if (!CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out))
        return {};
    QByteArray plain(reinterpret_cast<char*>(out.pbData), int(out.cbData));
    LocalFree(out.pbData);
    return plain;
}
#elif defined(Q_OS_ANDROID)
QByteArray jniBytes(const char* method, const QByteArray& input) {
    QJniEnvironment env;
    auto array = env->NewByteArray(input.size());
    if (!array)
        return {};
    env->SetByteArrayRegion(array, 0, input.size(), reinterpret_cast<const jbyte*>(input.constData()));
    auto result = QJniObject::callStaticObjectMethod("com/furina/black/FurinaActivity", method, "([B)[B", array);
    env->DeleteLocalRef(array);
    if (!result.isValid())
        return {};
    auto bytes = result.object<jbyteArray>();
    const auto n = env->GetArrayLength(bytes);
    QByteArray plain(n, Qt::Uninitialized);
    env->GetByteArrayRegion(bytes, 0, n, reinterpret_cast<jbyte*>(plain.data()));
    return plain;
}

QByteArray protect(const QByteArray& plain) {
    return jniBytes("protectSecret", plain);
}

QByteArray unprotect(const QByteArray& blob) {
    return jniBytes("unprotectSecret", blob);
}
#else
QString keyPath() {
    const auto dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    return dir + "/pairing.key";
}

QByteArray loadKey() {
    QFile file(keyPath());
    if (file.exists() && file.open(QIODevice::ReadOnly)) {
        const auto key = file.readAll();
        if (key.size() == 32)
            return key;
    }
    QByteArray key(32, Qt::Uninitialized);
    randomBytes(reinterpret_cast<uint8_t*>(key.data()), key.size());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return {};
    if (file.write(key) != key.size())
        return {};
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    return key;
}

QByteArray protect(const QByteArray& plain) {
    const auto key = loadKey();
    if (key.size() != 32)
        return {};
    QByteArray blob(1 + 24 + 16 + plain.size(), Qt::Uninitialized);
    blob[0] = 1;
    randomBytes(reinterpret_cast<uint8_t*>(blob.data() + 1), 24);
    crypto_aead_lock(reinterpret_cast<uint8_t*>(blob.data() + 41),
                     reinterpret_cast<uint8_t*>(blob.data() + 25),
                     reinterpret_cast<const uint8_t*>(key.constData()),
                     reinterpret_cast<const uint8_t*>(blob.constData() + 1), nullptr, 0,
                     reinterpret_cast<const uint8_t*>(plain.constData()), size_t(plain.size()));
    return blob;
}

QByteArray unprotect(const QByteArray& blob) {
    const auto key = loadKey();
    if (key.size() != 32 || blob.size() < 41 || blob[0] != 1)
        return {};
    QByteArray plain(blob.size() - 41, Qt::Uninitialized);
    if (crypto_aead_unlock(reinterpret_cast<uint8_t*>(plain.data()),
                           reinterpret_cast<const uint8_t*>(blob.constData() + 25),
                           reinterpret_cast<const uint8_t*>(key.constData()),
                           reinterpret_cast<const uint8_t*>(blob.constData() + 1), nullptr, 0,
                           reinterpret_cast<const uint8_t*>(blob.constData() + 41),
                           size_t(blob.size() - 41)) != 0)
        return {};
    return plain;
}
#endif
} // namespace

QByteArray SecureStore::load(const QString& name) {
    const auto blob = QByteArray::fromBase64(QSettings().value(settingsKey(name)).toByteArray());
    if (blob.isEmpty())
        return {};
    return unprotect(blob);
}

bool SecureStore::save(const QString& name, const QByteArray& secret) {
    const auto blob = protect(secret);
    if (blob.isEmpty())
        return false;
    QSettings().setValue(settingsKey(name), blob.toBase64());
    QSettings().remove("token");
    return true;
}

void SecureStore::remove(const QString& name) {
    QSettings().remove(settingsKey(name));
}
