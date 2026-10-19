#pragma once
#include <QObject>
#include <QTimer>
class Peer;
class Presentation;
class SessionConsent;

// Fullscreen commands and receiver confirmations have distinct wire values.
class PresentationSync final : public QObject {
    Q_OBJECT
  public:
    PresentationSync(Peer* peer, Presentation* presentation, SessionConsent* consent, QObject* parent);
    bool toggleRemote();
    void reset();

    bool remoteFullscreen() const {
        return m_remote;
    }

  signals:
    void remoteChanged(bool fullscreen);
    void status(const QString& chinese, const QString& english);

  private:
    Peer* m_peer;
    Presentation* m_presentation;
    SessionConsent* m_consent;
    QTimer m_timeout;
    bool m_remote = false, m_pending = false, m_target = false;
    void confirmLocal();
    void session(quint32 action);
};
