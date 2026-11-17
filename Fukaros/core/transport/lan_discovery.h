#pragma once
#include <QObject>
#include <QHostAddress>
#include <QTimer>
#include <QElapsedTimer>
#include <QUdpSocket>
#include <functional>

struct LanInterface {
    QString name;
    QHostAddress local, netmask, broadcast, gateway;
    uint index = 0;
};

// Ethernet/Wi-Fi adapters only. USB tethering adapters are excluded on purpose:
// UsbNetwork owns that path and the two must not cross-talk.
QList<LanInterface> furinaLanInterfaces();

// Broadcasts the FURINA_DISCOVER_V1 probe on every LAN adapter and reports back
// every host that answered. Unlike UsbNetwork it returns all candidates, because
// the caller tries them one by one and a stale answer must not fail the search.
class LanDiscovery : public QObject {
    Q_OBJECT
  public:
    using Interfaces = std::function<QList<LanInterface>()>;
    explicit LanDiscovery(QObject* parent = nullptr, Interfaces interfaces = furinaLanInterfaces);
    void start(int timeoutMs = 2500, quint16 discoveryPort = 27184);
    void cancel();

  signals:
    void hosts(const QStringList& hosts);
    void failed(const QString& chinese, const QString& english);
    void status(const QString& chinese, const QString& english);

  private:
    Interfaces m_interfaces;
    QTimer m_timer, m_settle;
    QElapsedTimer m_elapsed;
    QList<QUdpSocket*> m_sockets;
    QStringList m_hosts;
    QByteArray m_nonce;
    int m_timeout = 2500, m_lastCount = -1;
    quint16 m_port = 27184;
    bool m_running = false;
    void probe();
    void finish();
    void closeSockets();
};