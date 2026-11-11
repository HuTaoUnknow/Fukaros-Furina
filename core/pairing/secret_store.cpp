#include "secret_store.h"

SecretStore::SecretStore() = default;

SecretStore::SecretStore(const QSettings& settings)
    : m_explicit(true), m_organization(settings.organizationName()),
      m_application(settings.applicationName()) {}

bool SecretStore::store(const QString& key, const QByteArray& plain) {
    if (key.isEmpty() || plain.isEmpty())
        return false;
    bool ok = false;
    withSettings([&](QSettings& settings) {
        ok = pairingPlatformStore(settings, key, plain);
        if (ok)
            settings.sync();
    });
    return ok;
}

QByteArray SecretStore::load(const QString& key) const {
    if (key.isEmpty())
        return {};
    QByteArray value;
    withSettings([&](QSettings& settings) { value = pairingPlatformLoad(settings, key); });
    return value;
}

bool SecretStore::remove(const QString& key) {
    if (key.isEmpty())
        return false;
    bool ok = false;
    withSettings([&](QSettings& settings) {
        ok = pairingPlatformRemove(settings, key);
        if (ok)
            settings.sync();
    });
    return ok;
}

SecretStore& SecretStore::instance() {
    static SecretStore store;
    return store;
}

QByteArray pairingMigratePlaintext(SecretStore& store, QSettings& settings, const QString& legacyKey,
                                   const QString& secureKey) {
    const auto existing = store.load(secureKey);
    if (!existing.isEmpty()) {
        if (settings.contains(legacyKey)) {
            settings.remove(legacyKey);
            settings.sync();
        }
        return existing;
    }
    if (!settings.contains(legacyKey))
        return {};
    const auto legacy = settings.value(legacyKey).toString();
    if (legacy.isEmpty()) {
        settings.remove(legacyKey);
        settings.sync();
        return {};
    }
    const auto bytes = legacy.toUtf8();
    if (!store.store(secureKey, bytes))
        return {};
    settings.remove(legacyKey);
    settings.sync();
    return bytes;
}
