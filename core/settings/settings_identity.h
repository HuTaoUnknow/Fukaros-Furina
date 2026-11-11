#ifndef FURINA_SETTINGS_IDENTITY_H
#define FURINA_SETTINGS_IDENTITY_H
#include <QSettings>
#include <QCoreApplication>

// Keep the user's local connection preferences when the app is renamed.
// Pairing secrets and identity keys are not copied; they live in platform secure storage.
inline void furinaMigrateSettings(const QString& legacyApplication) {
    QSettings current;
    QSettings legacy(QCoreApplication::organizationName(), legacyApplication);
    for (const auto& key : legacy.allKeys()) {
        if (key == "token" || key.startsWith("secure/"))
            continue;
        if (!current.contains(key))
            current.setValue(key, legacy.value(key));
    }
    current.remove("token");
    current.sync();
}
#endif
