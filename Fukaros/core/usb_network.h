#pragma once
#include <QObject>
#include <QHostAddress>
#include <QTimer>
#include <QElapsedTimer>
#include <QUdpSocket>
#include <functional>

struct UsbNetworkInterface {
 QString name;
 QHostAddress local,netmask,broadcast,gateway;
 uint index=0;
};
QList<UsbNetworkInterface> furinaUsbNetworkInterfaces();

// Probe USB Ethernet interfaces only, and bind the later TCP connection to
// the same source address. Never silently fall back to a Wi-Fi adapter.
class UsbNetwork : public QObject {
 Q_OBJECT
public:
 using Interfaces=std::function<QList<UsbNetworkInterface>()>;
 explicit UsbNetwork(QObject *parent=nullptr,Interfaces interfaces=furinaUsbNetworkInterfaces);
 void start(const QString &host={},int timeoutMs=30000,quint16 discoveryPort=27184);
 void cancel();
signals:
 void found(const QString &host,const QString &localAddress,const QString &interfaceName);
 void failed(const QString &chinese,const QString &english);
 void status(const QString &chinese,const QString &english);
private:
 Interfaces m_interfaces;
 QTimer m_timer,m_settle;
 QElapsedTimer m_elapsed;
 QList<QUdpSocket*> m_sockets;
 struct Match {QHostAddress host;UsbNetworkInterface adapter;};
 QList<Match> m_matches;
 QByteArray m_nonce;
 QHostAddress m_host;
 int m_timeout=30000,m_lastCount=-1;
 quint16 m_port=27184;
 bool m_running=false;
 void probe();void finish();
};
