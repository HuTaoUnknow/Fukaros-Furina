#pragma once
#include <QObject>
#include <QLocalServer>
#include <QLockFile>
#include <memory>

// The lock is acquired before constructing any UI or display controllers.
class SingleInstance final : public QObject {
    Q_OBJECT
  public:
    enum Result { Primary, Activated, Error };

    explicit SingleInstance(const QString& directory, QObject* parent = nullptr);
    Result start();

    QString errorString() const {
        return m_error;
    }

  signals:
    void activateRequested();

  private:
    QString m_directory, m_endpoint, m_error;
    std::unique_ptr<QLockFile> m_lock;
    QLocalServer m_server;
    bool activateOwner();
};
