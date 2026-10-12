#include "network_discovery.h"
#include <QNetworkDatagram>
#include <QNetworkProxy>

static bool validNonce(const QByteArray &nonce){
 if(nonce.size()!=32)return false;
 for(char c:nonce)if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')))return false;
 return true;
}
QByteArray furinaDiscoveryRequest(const QByteArray &nonce){return validNonce(nonce)?QByteArray("FURINA_DISCOVER_V1 ")+nonce:QByteArray();}
QByteArray furinaDiscoveryReply(const QByteArray &request){
 const QByteArray prefix("FURINA_DISCOVER_V1 ");
 if(!request.startsWith(prefix))return {};
 const auto nonce=request.mid(prefix.size());
 return validNonce(nonce)?QByteArray("FURINA_HERE_V1 ")+nonce+" 27183":QByteArray();
}
bool furinaDiscoveryMatches(const QByteArray &reply,const QByteArray &nonce){
 return validNonce(nonce)&&reply==QByteArray("FURINA_HERE_V1 ")+nonce+" 27183";
}
NetworkDiscoveryServer::NetworkDiscoveryServer(QObject *parent):QObject(parent){
 m_socket.setProxy(QNetworkProxy::NoProxy);
 connect(&m_socket,&QUdpSocket::readyRead,this,[this]{
  while(m_socket.hasPendingDatagrams()){
   auto request=m_socket.receiveDatagram(128);
   const auto reply=furinaDiscoveryReply(request.data());
   if(reply.isEmpty())continue;
   auto answer=request.makeReply(reply);
   // Reply through the interface that received the probe, including USB.
   answer.setInterfaceIndex(request.interfaceIndex());
   m_socket.writeDatagram(answer);
  }
 });
}
bool NetworkDiscoveryServer::start(const QString &bindAddress,quint16 port){
 stop();QHostAddress address(QHostAddress::AnyIPv4);
 if(!bindAddress.trimmed().isEmpty()&&(!address.setAddress(bindAddress.trimmed())||address.protocol()!=QAbstractSocket::IPv4Protocol))return false;
 return m_socket.bind(address,port,QUdpSocket::DontShareAddress);
}
void NetworkDiscoveryServer::stop(){m_socket.close();}
