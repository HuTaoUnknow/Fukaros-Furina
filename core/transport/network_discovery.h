#pragma once
#include <QObject>
#include <QUdpSocket>

// Discovery advertises availability only. Pairing and approval still happen
// over Peer's authenticated TCP transport; no pairing secret is sent by UDP.
QByteArray furinaDiscoveryRequest(const QByteArray& nonce);
QByteArray furinaDiscoveryReply(const QByteArray& request);
bool furinaDiscoveryMatches(const QByteArray& reply, const QByteArray& nonce);

class NetworkDiscoveryServer : public QObject {
    Q_OBJECT
  public:
    explicit NetworkDiscoveryServer(QObject* parent = nullptr);
    bool start(const QString& bindAddress = {}, quint16 port = 27184);
    void stop();

  private:
    QUdpSocket m_socket;
};
