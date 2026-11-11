#pragma once
#include <QByteArray>
#include <QSettings>
#include <QString>

// Platform secret store. Windows uses DPAPI, Android uses the Keystore wrapper,
// and other builds keep values in process memory so they are never written as plaintext.
// QSettings is not copied; an explicit store reopens the same organization and application.
class SecretStore {
  public:
    SecretStore();
    explicit SecretStore(const QSettings& settings);
    bool store(const QString& key, const QByteArray& plain);
    QByteArray load(const QString& key) const;
    bool remove(const QString& key);
    static SecretStore& instance();

  private:
    bool m_explicit = false;
    QString m_organization;
    QString m_application;
    template <typename Fn>
    void withSettings(Fn&& fn) const {
        if (!m_explicit) {
            QSettings settings;
            fn(settings);
            return;
        }
        QSettings settings(m_organization, m_application);
        fn(settings);
    }
};

bool pairingPlatformStore(QSettings& settings, const QString& key, const QByteArray& plain);
QByteArray pairingPlatformLoad(const QSettings& settings, const QString& key);
bool pairingPlatformRemove(QSettings& settings, const QString& key);

// Read a legacy plaintext QSettings value once, store it, then delete the plaintext key.
// If sealing fails, the plaintext entry is left in place.
QByteArray pairingMigratePlaintext(SecretStore& store, QSettings& settings, const QString& legacyKey,
                                   const QString& secureKey);
