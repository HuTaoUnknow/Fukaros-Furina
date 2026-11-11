#include "secret_store.h"
#include <QHash>
#include <QMutex>
#include <QMutexLocker>

namespace {
QString scopeOf(const QSettings& settings) {
    return settings.organizationName() + QLatin1Char('\n') + settings.applicationName() + QLatin1Char('\n') +
           settings.fileName();
}

struct MemorySecrets {
    QMutex mutex;
    QHash<QString, QHash<QString, QByteArray>> values;
};

MemorySecrets& memorySecrets() {
    static MemorySecrets secrets;
    return secrets;
}
} // namespace

bool pairingPlatformStore(QSettings& settings, const QString& key, const QByteArray& plain) {
    auto& secrets = memorySecrets();
    QMutexLocker lock(&secrets.mutex);
    secrets.values[scopeOf(settings)].insert(key, plain);
    return true;
}

QByteArray pairingPlatformLoad(const QSettings& settings, const QString& key) {
    auto& secrets = memorySecrets();
    QMutexLocker lock(&secrets.mutex);
    return secrets.values.value(scopeOf(settings)).value(key);
}

bool pairingPlatformRemove(QSettings& settings, const QString& key) {
    auto& secrets = memorySecrets();
    QMutexLocker lock(&secrets.mutex);
    secrets.values[scopeOf(settings)].remove(key);
    return true;
}
