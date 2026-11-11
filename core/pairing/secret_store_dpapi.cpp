#include "secret_store.h"
#include <QUrl>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wincrypt.h>

namespace {
QString blobKey(const QString& key) {
    return QString::fromLatin1(QUrl::toPercentEncoding(key));
}

QByteArray protectCurrentUser(const QByteArray& plain) {
    DATA_BLOB in{};
    DATA_BLOB out{};
    in.cbData = static_cast<DWORD>(plain.size());
    in.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(plain.data()));
    // Current-user scope: no CRYPTPROTECT_LOCAL_MACHINE. Never prompt.
    if (!CryptProtectData(&in, L"Fukaros pairing", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN,
                          &out))
        return {};
    QByteArray sealed(reinterpret_cast<const char*>(out.pbData), static_cast<int>(out.cbData));
    LocalFree(out.pbData);
    if (sealed.isEmpty() || sealed == plain || sealed.contains(plain))
        return {};
    return sealed;
}

QByteArray unprotectCurrentUser(const QByteArray& sealed) {
    if (sealed.isEmpty())
        return {};
    DATA_BLOB in{};
    DATA_BLOB out{};
    in.cbData = static_cast<DWORD>(sealed.size());
    in.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(sealed.data()));
    if (!CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out))
        return {};
    QByteArray plain(reinterpret_cast<const char*>(out.pbData), static_cast<int>(out.cbData));
    LocalFree(out.pbData);
    return plain;
}

QString sealedPath(const QString& key) {
    return QStringLiteral("secure/") + blobKey(key);
}
} // namespace

bool pairingPlatformStore(QSettings& settings, const QString& key, const QByteArray& plain) {
    const auto sealed = protectCurrentUser(plain);
    if (sealed.isEmpty())
        return false;
    settings.setValue(sealedPath(key), sealed);
    return true;
}

QByteArray pairingPlatformLoad(const QSettings& settings, const QString& key) {
    return unprotectCurrentUser(settings.value(sealedPath(key)).toByteArray());
}

bool pairingPlatformRemove(QSettings& settings, const QString& key) {
    settings.remove(sealedPath(key));
    return true;
}
