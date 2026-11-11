#pragma once
#include <QByteArray>
#include <QString>

// Secrets are encrypted at rest. Windows uses DPAPI, Android uses the
// hardware-backed keystore, and other builds use a 0600 local key.
class SecureStore {
  public:
    static QByteArray load(const QString& name);
    static bool save(const QString& name, const QByteArray& secret);
    static void remove(const QString& name);
};
