#include <QtTest>
#include <QTcpSocket>
#include <QMessageAuthenticationCode>
#include <QNetworkProxy>
#include <QNetworkInterface>
#include "peer.h"
#include "video_policy.h"
class PeerTest:public QObject{
 Q_OBJECT
private slots:

 void gracefulStopDrainsCancellation_data(){
  QTest::addColumn<bool>("serverStops");
  QTest::newRow("client-stops")<<false;QTest::newRow("server-stops")<<true;
 }
 void gracefulStopDrainsCancellation(){
  QFETCH(bool,serverStops);Peer server,client;
  QVERIFY(server.listen(29880,"stop-sync-code"));client.connectTo("127.0.0.1",29880,"stop-sync-code");QTRY_VERIFY(server.ready()&&client.ready());
  auto *sender=serverStops?&server:&client,*receiver=serverStops?&client:&server;
  QSignalSpy video(receiver,&Peer::video),approval(receiver,&Peer::approval),closed(receiver,&Peer::disconnected);QStringList order;
  connect(receiver,&Peer::video,this,[&]{order<<"video";});connect(receiver,&Peer::approval,this,[&]{order<<"cancel";});connect(receiver,&Peer::disconnected,this,[&]{order<<"disconnected";});
  QByteArray packet(FR_VIDEO_HEADER+192*1024,0);fr_video_info info{FR_VIDEO_KEY,1,1920,1080,1,123};QVERIFY(fr_video_write(reinterpret_cast<uint8_t*>(packet.data()),&info));
  QVERIFY(sender->sendVideo(packet));sender->sendApproval(3101,FR_APPROVAL_CANCEL,FR_APPROVAL_SHARE);sender->close(true);QVERIFY(!sender->ready());
  QTRY_COMPARE(closed.count(),1);QCOMPARE(video.count(),1);QCOMPARE(approval.count(),1);QCOMPARE(approval[0][0].toUInt(),3101u);QCOMPARE(approval[0][1].toUInt(),quint32(FR_APPROVAL_CANCEL));
  QCOMPARE(order,QStringList({"video","cancel","disconnected"}));
 }
 void simultaneousStopsFinishBothTransports(){
  Peer server,client;QVERIFY(server.listen(29881,"stop-sync-code"));client.connectTo("127.0.0.1",29881,"stop-sync-code");QTRY_VERIFY(server.ready()&&client.ready());
  server.sendApproval(3102,FR_APPROVAL_CANCEL,FR_APPROVAL_SHARE);client.sendApproval(3102,FR_APPROVAL_CANCEL,FR_APPROVAL_SHARE);server.close(true);client.close(true);
  QTRY_VERIFY(server.findChildren<QTcpSocket*>().isEmpty()&&client.findChildren<QTcpSocket*>().isEmpty());
 }
 void anOldHeartbeatPongDoesNotFinishStop(){
  Peer server;QTcpSocket raw;QVERIFY(server.listen(29882,"stop-sync-code"));raw.connectToHost("127.0.0.1",29882);QTRY_VERIFY(raw.bytesAvailable()>=44);
  const auto challenge=raw.read(44).mid(FR_HEADER_SIZE);const auto mac=QMessageAuthenticationCode::hash(challenge,"stop-sync-code",QCryptographicHash::Sha256);
  uint8_t header[FR_HEADER_SIZE];fr_encode_header(header,FR_AUTH,mac.size());raw.write(reinterpret_cast<char*>(header),sizeof header);raw.write(mac);QTRY_VERIFY(server.ready());QTRY_VERIFY(raw.bytesAvailable()>=FR_HEADER_SIZE);raw.read(FR_HEADER_SIZE);
  QTRY_VERIFY_WITH_TIMEOUT(raw.bytesAvailable()>=FR_HEADER_SIZE,4000);fr_header ping{};const auto earlier=raw.read(FR_HEADER_SIZE);QVERIFY(fr_decode_header(reinterpret_cast<const uint8_t*>(earlier.constData()),&ping));QCOMPARE(ping.type,quint8(FR_PING));
  server.sendApproval(3103,FR_APPROVAL_CANCEL,FR_APPROVAL_SHARE);server.close(true);QTRY_VERIFY(raw.bytesAvailable()>=36);const auto stop=raw.read(36);fr_header barrier{};QVERIFY(fr_decode_header(reinterpret_cast<const uint8_t*>(stop.constData()+24),&barrier));QCOMPARE(barrier.type,quint8(FR_PING));
  fr_encode_header(header,FR_PONG,0);raw.write(reinterpret_cast<char*>(header),sizeof header);raw.flush();QTest::qWait(40);QCOMPARE(raw.state(),QAbstractSocket::ConnectedState);
  raw.write(reinterpret_cast<char*>(header),sizeof header);raw.flush();QTRY_COMPARE(raw.state(),QAbstractSocket::UnconnectedState);
 }
 void videoProfileFeedback(){
  Peer server,client;QSignalSpy paired(&client,&Peer::connected),video(&server,&Peer::video),profile(&server,&Peer::profile),feedback(&client,&Peer::feedback);
  QVERIFY(server.listen(29879,"video-code"));client.connectTo("127.0.0.1",29879,"video-code");QTRY_COMPARE(paired.count(),1);
  client.sendProfile(0);QTRY_COMPARE(profile.count(),1);QCOMPARE(profile[0][0].toInt(),0);client.sendProfile(999);QTest::qWait(20);QCOMPARE(profile.count(),1);
  QByteArray packet(FR_VIDEO_HEADER+5,0);fr_video_info info{FR_VIDEO_KEY,1,1920,1200,1,12345};QVERIFY(fr_video_write(reinterpret_cast<uint8_t*>(packet.data()),&info));packet[32]=1;
  QVERIFY(client.sendVideo(packet));QTRY_COMPARE(video.count(),1);QCOMPARE(video[0][0].toByteArray(),packet);
  server.sendFeedback(60,2.5,16,59.8);QTRY_COMPARE(feedback.count(),1);QCOMPARE(feedback[0][0].toDouble(),60.0);QCOMPARE(feedback[0][1].toDouble(),2.5);QCOMPARE(feedback[0][3].toDouble(),59.8);
 }
 void authenticatedSessionRequests(){
  Peer server,client;QSignalSpy paired(&client,&Peer::connected),received(&server,&Peer::session),reverse(&client,&Peer::session);
  QVERIFY(server.listen(29876,"session-code"));client.connectTo("127.0.0.1",29876,"session-code");QTRY_COMPARE(paired.count(),1);
  client.sendSession(FR_MAXIMIZE);QTRY_COMPARE(received.count(),1);QCOMPARE(received[0][0].toUInt(),quint32(FR_MAXIMIZE));
  server.sendSession(FR_RESTORE);QTRY_COMPARE(reverse.count(),1);QCOMPARE(reverse[0][0].toUInt(),quint32(FR_RESTORE));
  client.sendSession(99);QTest::qWait(50);QCOMPARE(received.count(),1);
 }
 void unauthenticatedSessionIsRejected(){
  Peer server;QSignalSpy requests(&server,&Peer::session);QVERIFY(server.listen(29877,"session-code"));QTcpSocket raw;
  raw.connectToHost("127.0.0.1",29877);QTRY_COMPARE(raw.state(),QAbstractSocket::ConnectedState);
  uint8_t h[12],action[4];fr_encode_header(h,FR_SESSION,4);fr_put_u32(action,FR_START_SHARE);
  raw.write(reinterpret_cast<char*>(h),12);raw.write(reinterpret_cast<char*>(action),4);
  QTRY_COMPARE(raw.state(),QAbstractSocket::UnconnectedState);QCOMPARE(requests.count(),0);
 }
 void invalidSessionPayloadIsRejected(){
  Peer server;QSignalSpy requests(&server,&Peer::session);QVERIFY(server.listen(29878,"session-code"));QTcpSocket raw;
  raw.connectToHost("127.0.0.1",29878);QTRY_VERIFY(raw.bytesAvailable()>=44);
  const auto challenge=raw.read(44).mid(12);const auto mac=QMessageAuthenticationCode::hash(challenge,"session-code",QCryptographicHash::Sha256);
  uint8_t h[12];fr_encode_header(h,FR_AUTH,32);raw.write(reinterpret_cast<char*>(h),12);raw.write(mac);QTRY_VERIFY(server.ready());
  fr_encode_header(h,FR_SESSION,2);raw.write(reinterpret_cast<char*>(h),12);raw.write("xx",2);
  QTRY_COMPARE(raw.state(),QAbstractSocket::UnconnectedState);QCOMPARE(requests.count(),0);
 }
 void deviceConnection(){
  const auto host=qEnvironmentVariable("FURINA_DEVICE_HOST"),code=qEnvironmentVariable("FURINA_DEVICE_CODE");
  if(host.isEmpty()||code.isEmpty())QSKIP("Set FURINA_DEVICE_HOST and FURINA_DEVICE_CODE for a real device test");
  Peer client;QSignalSpy paired(&client,&Peer::connected);client.connectTo(host,27183,code);QTRY_COMPARE(paired.count(),1);QVERIFY(client.ready());
  QTest::qWait(200);client.close();
 }
 void directConnectionsIgnoreApplicationProxy(){
  struct RestoreProxy {QNetworkProxy old=QNetworkProxy::applicationProxy();~RestoreProxy(){QNetworkProxy::setApplicationProxy(old);}} restore;
  QNetworkProxy::setApplicationProxy(QNetworkProxy(QNetworkProxy::HttpCachingProxy,"127.0.0.1",9));
  QString address;for(const auto &a:QNetworkInterface::allAddresses())if(a.protocol()==QAbstractSocket::IPv4Protocol&&!a.isLoopback()){address=a.toString();break;}
  QVERIFY2(!address.isEmpty(),"Requires a local non-loopback IPv4 address");
  Peer server,client;QSignalSpy paired(&client,&Peer::connected),frames(&server,&Peer::frame);
  QVERIFY(server.listen(29875,"proxy-test-code"));client.connectTo(address,29875,"proxy-test-code");QTRY_COMPARE(paired.count(),1);
  QImage im(40,30,QImage::Format_RGB32);im.fill(Qt::blue);client.sendFrame(im);QTRY_COMPARE(frames.count(),1);
 }
 void connectionErrorDoesNotProduceLaterTimeout(){
  QTcpServer port;port.setProxy(QNetworkProxy::NoProxy);QVERIFY(port.listen(QHostAddress::LocalHost,0));const auto unused=port.serverPort();port.close();
  Peer client;QSignalSpy statuses(&client,&Peer::status),closed(&client,&Peer::disconnected);client.connectTo("127.0.0.1",unused,"test-code");
  QTRY_VERIFY(statuses.count()>0);QTRY_COMPARE(closed.count(),1);const auto count=statuses.count();QTest::qWait(9500);QCOMPARE(statuses.count(),count);
 }
 void duplex(){
  Peer server,client;QSignalSpy connected(&client,&Peer::connected),frames(&server,&Peer::frame),keys(&server,&Peer::key),reverse(&client,&Peer::frame);
  QVERIFY(server.listen(29871,"12345678"));client.connectTo("127.0.0.1",29871,"12345678");QTRY_COMPARE(connected.count(),1);QVERIFY(server.ready());
  QImage im(80,60,QImage::Format_RGB32);im.fill(Qt::red);client.sendFrame(im);QTRY_COMPARE(frames.count(),1);QCOMPARE(qvariant_cast<QImage>(frames[0][0]).size(),im.size());
  server.sendFrame(im);QTRY_COMPARE(reverse.count(),1);client.sendKey(4);QTRY_COMPARE(keys.count(),1);QCOMPARE(keys[0][0].toUInt(),4u);
  client.close();QTRY_VERIFY(!server.ready());
 }
 void wrongToken(){Peer s,c;QSignalSpy spy(&c,&Peer::connected);QVERIFY(s.listen(29872,"correct-code"));c.connectTo("127.0.0.1",29872,"wrong-code");QTest::qWait(350);QCOMPARE(spy.count(),0);QVERIFY(!s.ready());}
 void fragmentedAndCoalesced(){
  Peer s;QVERIFY(s.listen(29873,"test-code"));QTcpSocket raw;raw.connectToHost("127.0.0.1",29873);QTRY_VERIFY(raw.state()==QAbstractSocket::ConnectedState);QTRY_VERIFY(raw.bytesAvailable()>=44);
  QByteArray challenge=raw.read(44).mid(12);auto mac=QMessageAuthenticationCode::hash(challenge,"test-code",QCryptographicHash::Sha256);uint8_t h[12];QVERIFY(fr_encode_header(h,FR_AUTH,32));QByteArray auth(reinterpret_cast<char*>(h),12);auth+=mac;
  for(char ch:auth){raw.write(&ch,1);raw.flush();QTest::qWait(1);}QTRY_VERIFY(s.ready());
  QSignalSpy keys(&s,&Peer::key);uint8_t k[4];fr_put_u32(k,3);fr_encode_header(h,FR_KEY,4);QByteArray pkt(reinterpret_cast<char*>(h),12);pkt+=QByteArray(reinterpret_cast<char*>(k),4);raw.write(pkt+pkt);QTRY_COMPARE(keys.count(),2);
  h[0]='X';raw.write(reinterpret_cast<char*>(h),12);QTRY_VERIFY(!s.ready());
 }
 void unauthenticatedControl(){Peer s;QSignalSpy spy(&s,&Peer::key);QVERIFY(s.listen(29874,"test-code"));QTcpSocket raw;raw.connectToHost("127.0.0.1",29874);QTRY_VERIFY(raw.state()==QAbstractSocket::ConnectedState);uint8_t h[12],k[4];fr_encode_header(h,FR_KEY,4);fr_put_u32(k,4);raw.write(reinterpret_cast<char*>(h),12);raw.write(reinterpret_cast<char*>(k),4);QTRY_VERIFY(raw.state()==QAbstractSocket::UnconnectedState);QCOMPARE(spy.count(),0);}
};
QTEST_GUILESS_MAIN(PeerTest)
#include "peer_test.moc"
