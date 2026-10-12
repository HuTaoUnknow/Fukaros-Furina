#include <QtTest>
#include <QNetworkDatagram>
#include <QNetworkProxy>
#include "usb_network.h"
#include "network_discovery.h"
#include "peer.h"

class UsbNetworkTest:public QObject {
 Q_OBJECT
 const UsbNetworkInterface loopback{"Test USB",QHostAddress("127.0.0.2"),QHostAddress("255.0.0.0"),QHostAddress("127.255.255.255"),QHostAddress("127.0.0.3"),0};
 quint16 freePort(){QUdpSocket s;s.setProxy(QNetworkProxy::NoProxy);if(!s.bind(QHostAddress("127.0.0.3"),0))return 0;return s.localPort();}
private slots:
 void malformedAndStaleReplies(){
  const QByteArray nonce(32,'a');const auto request=furinaDiscoveryRequest(nonce);const auto reply=furinaDiscoveryReply(request);
  QVERIFY(furinaDiscoveryMatches(reply,nonce));QVERIFY(!furinaDiscoveryMatches(reply,QByteArray(32,'b')));
  QVERIFY(furinaDiscoveryReply(request+"trailing").isEmpty());QVERIFY(furinaDiscoveryReply("FURINA_DISCOVER_V1 "+QByteArray(32,'z')).isEmpty());
  QVERIFY(!furinaDiscoveryMatches(reply+"trailing",nonce));QVERIFY(furinaDiscoveryRequest("short").isEmpty());
 }
 void discoversAndPairsThroughBoundInterface(){
  const auto port=freePort();QVERIFY(port);NetworkDiscoveryServer discovery;QVERIFY(discovery.start("127.0.0.3",port));
  UsbNetwork finder(nullptr,[this]{return QList<UsbNetworkInterface>{loopback};});QSignalSpy found(&finder,&UsbNetwork::found),failed(&finder,&UsbNetwork::failed);
  finder.start({},3000,port);QTRY_COMPARE_WITH_TIMEOUT(found.count(),1,4000);QVERIFY(failed.isEmpty());
  QCOMPARE(found.first()[0].toString(),QString("127.0.0.3"));QCOMPARE(found.first()[1].toString(),QString("127.0.0.2"));
  Peer server,client;QVERIFY(server.listen(27195,"usb-network-test","127.0.0.3"));QSignalSpy ready(&client,&Peer::connected),status(&server,&Peer::status);
  client.connectTo(found.first()[0].toString(),27195,"usb-network-test",found.first()[1].toString());QTRY_VERIFY(client.ready()&&server.ready());QCOMPARE(ready.count(),1);
  auto *transport=client.findChild<QTcpSocket*>();QVERIFY(transport);QCOMPARE(transport->localAddress(),QHostAddress("127.0.0.2"));
  QSignalSpy received(&server,&Peer::session);client.sendSession(FR_REQUEST_KEY);QTRY_COMPARE(received.count(),1);
  client.close(true);QTRY_VERIFY(!server.ready());
 }
 void cancellationDiscardsLateReplies(){
  const auto port=freePort();QUdpSocket responder;responder.setProxy(QNetworkProxy::NoProxy);QVERIFY(responder.bind(QHostAddress("127.0.0.3"),port));
  UsbNetwork finder(nullptr,[this]{return QList<UsbNetworkInterface>{loopback};});QSignalSpy found(&finder,&UsbNetwork::found),failed(&finder,&UsbNetwork::failed);
  finder.start({},500,port);QTRY_VERIFY(responder.hasPendingDatagrams());const auto request=responder.receiveDatagram();finder.cancel();
  responder.writeDatagram(request.makeReply(furinaDiscoveryReply(request.data())));QTest::qWait(550);QVERIFY(found.isEmpty()&&failed.isEmpty());
 }
 void waitsForAdapterAndRejectsWifiAddress(){
  QList<UsbNetworkInterface> interfaces;UsbNetwork finder(nullptr,[&]{return interfaces;});QSignalSpy found(&finder,&UsbNetwork::found),failed(&finder,&UsbNetwork::failed);
  finder.start("192.168.44.1",1200);QVERIFY(found.isEmpty());
  // The WLAN subnet is never included in the provider's USB interface list.
  interfaces.append({"USB RNDIS",QHostAddress("192.168.44.2"),QHostAddress("255.255.255.0"),QHostAddress("192.168.44.255"),QHostAddress("192.168.44.1"),0});
  QTRY_COMPARE_WITH_TIMEOUT(found.count(),1,1500);QVERIFY(failed.isEmpty());
  finder.start("192.168.1.10",1);QTRY_COMPARE_WITH_TIMEOUT(failed.count(),1,1500);QCOMPARE(found.count(),1);
 }
 void invalidSourceCannotFallBack(){
  Peer client;QSignalSpy disconnected(&client,&Peer::disconnected),ready(&client,&Peer::connected);
  client.connectTo("127.0.0.3",27195,"usb-network-test","invalid-source");QCOMPARE(disconnected.count(),1);QVERIFY(ready.isEmpty()&&!client.ready());
 }
 void multipleDevicesRequireSelection(){
  const auto port=freePort();NetworkDiscoveryServer one,two;QVERIFY(one.start("127.0.0.3",port));QVERIFY(two.start("127.0.0.4",port));
  auto second=loopback;second.local=QHostAddress("127.0.0.5");second.gateway=QHostAddress("127.0.0.4");
  UsbNetwork finder(nullptr,[&]{return QList<UsbNetworkInterface>{loopback,second};});QSignalSpy found(&finder,&UsbNetwork::found),failed(&finder,&UsbNetwork::failed);
  finder.start({},3000,port);QTRY_COMPARE_WITH_TIMEOUT(failed.count(),1,4000);QVERIFY(found.isEmpty());
 }
 void enumerateRealInterfaces(){
  for(const auto &interface:furinaUsbNetworkInterfaces())qInfo().noquote()<<"USB adapter:"<<interface.name<<interface.local.toString()<<interface.gateway.toString();
 }
};
QTEST_GUILESS_MAIN(UsbNetworkTest)
#include "usb_network_test.moc"
