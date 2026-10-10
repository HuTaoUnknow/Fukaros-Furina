#ifndef FURINA_SETTINGS_IDENTITY_H
#define FURINA_SETTINGS_IDENTITY_H
#include <QSettings>
#include <QCoreApplication>
// Keep the user's local pairing and connection preferences when the app is renamed.
// Credentials remain in QSettings and are never part of the source or packages.
inline void furinaMigrateSettings(const QString &legacyApplication) {
    QSettings current;
    QSettings legacy(QCoreApplication::organizationName(), legacyApplication);
    for (const auto &key : legacy.allKeys()) {
        if (!current.contains(key)) current.setValue(key, legacy.value(key));
    }
    current.sync();
}
#endif
