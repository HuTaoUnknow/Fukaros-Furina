#include <QtTest>
#include <QVBoxLayout>
#include "session_consent.h"
struct ConsentWindow : QWidget {
 ConsentWindow(){auto *layout=new QVBoxLayout(this);auto *panel=new QWidget(this);panel->setObjectName("consentPanel");auto *inner=new QVBoxLayout(panel);auto *label=new QLabel(panel);label->setObjectName("consentMessage");inner->addWidget(label);for(auto name:{"consentAccept","consentReject"}){auto *button=new QPushButton(name,panel);button->setObjectName(name);inner->addWidget(button);}layout->addWidget(panel);show();}
 void allow(){findChild<QPushButton*>("consentAccept")->click();}
 void reject(){findChild<QPushButton*>("consentReject")->click();}
};
class ConsentTest : public QObject {
 Q_OBJECT
private slots:
 void wireValidation(){
  uint8_t header[12];fr_header decoded{};QVERIFY(fr_encode_header(header,FR_APPROVAL,12));QVERIFY(fr_decode_header(header,&decoded));QCOMPARE(decoded.type,uint8_t(FR_APPROVAL));
  for(quint32 verb=FR_APPROVAL_REQUEST;verb<=FR_APPROVAL_CANCEL;++verb){QVERIFY(fr_valid_approval(1,verb,FR_APPROVAL_SHARE));QVERIFY(fr_valid_approval(1,verb,FR_APPROVAL_RECEIVE|FR_APPROVAL_EXTEND));}
  QVERIFY(!fr_valid_approval(0,FR_APPROVAL_REQUEST,1));QVERIFY(!fr_valid_approval(1,0,1));QVERIFY(!fr_valid_approval(1,5,1));QVERIFY(!fr_valid_approval(1,1,0));QVERIFY(!fr_valid_approval(1,1,3));QVERIFY(!fr_valid_approval(1,1,9));
  QVERIFY(fr_valid_approval(1,FR_APPROVAL_REQUEST,FR_APPROVAL_RECEIVE|FR_APPROVAL_EXTEND|FR_APPROVAL_PORTRAIT));
 }
 void receiverChoosesOrientation(){
  Peer server,client;ConsentWindow receiver,sender;SessionConsent remote(&server,&receiver),local(&client,&sender);
  bool portrait=true;remote.setReceiverOrientation([&]{return portrait;});
  QVERIFY(server.listen(29883,"orientation-code"));client.connectTo("127.0.0.1",29883,"orientation-code");QTRY_VERIFY(server.ready()&&client.ready());
  QSignalSpy requests(&server,&Peer::approval);QVERIFY(local.request(true,true));QTRY_VERIFY(remote.pending());
  const auto id=requests.last()[0].toUInt();
  // A reply cannot use orientation negotiation to alter the authorized direction.
  server.sendApproval(id,FR_APPROVAL_ACCEPT,FR_APPROVAL_RECEIVE|FR_APPROVAL_EXTEND|FR_APPROVAL_PORTRAIT);
  QTest::qWait(20);QVERIFY(local.pending()&&!local.active());
  receiver.allow();QTRY_VERIFY(local.active());QVERIFY(local.portrait()&&remote.portrait());
  portrait=false;QVERIFY(remote.portrait());local.cancel();QTRY_VERIFY(!remote.active());
  QVERIFY(local.request(true,true));QTRY_VERIFY(remote.pending());receiver.allow();QTRY_VERIFY(local.active());QVERIFY(!local.portrait()&&!remote.portrait());
  local.cancel();QTRY_VERIFY(!remote.active());
  local.setReceiverOrientation([]{return true;});QVERIFY(local.request(false,true));QTRY_VERIFY(remote.pending());receiver.allow();QTRY_VERIFY(local.active());QVERIFY(local.portrait()&&remote.portrait());
  local.cancel();QTRY_VERIFY(!remote.active());
  QVERIFY(local.request(false,false));QTRY_VERIFY(remote.pending());receiver.allow();QTRY_VERIFY(local.active());QVERIFY(!local.portrait()&&!remote.portrait());
 }
 void approveRejectCancelAndLateReplies(){
  Peer server,client;ConsentWindow receiver,sender;SessionConsent remote(&server,&receiver),local(&client,&sender);
  QSignalSpy connected(&client,&Peer::connected),requests(&server,&Peer::approval),approvedLocal(&local,&SessionConsent::approved),approvedRemote(&remote,&SessionConsent::approved);
  QVERIFY(server.listen(29881,"consent-code"));client.connectTo("127.0.0.1",29881,"consent-code");QTRY_COMPARE(connected.count(),1);
  QVERIFY(local.request(true,true));QTRY_VERIFY(receiver.findChild<QWidget*>("consentPanel")->isVisible());QVERIFY(local.pending()&&!local.active()&&!remote.active());QCOMPARE(approvedLocal.count(),0);
  const auto oldId=requests.last()[0].toUInt();const auto options=requests.last()[2].toUInt();receiver.reject();QTRY_VERIFY(!local.pending());QVERIFY(!local.active()&&!remote.active());QCOMPARE(approvedLocal.count(),0);
  QVERIFY(local.request(false,false));QTRY_VERIFY(remote.pending());server.sendApproval(oldId,FR_APPROVAL_ACCEPT,options);QTest::qWait(30);QVERIFY(local.pending()&&!local.active());
  receiver.allow();QTRY_COMPARE(approvedLocal.count(),1);QCOMPARE(approvedRemote.count(),1);QVERIFY(local.active()&&remote.active());QVERIFY(!local.sharing()&&remote.sharing());QCOMPARE(approvedLocal[0][2].toBool(),true);QCOMPARE(approvedRemote[0][2].toBool(),false);
  local.cancel();QTRY_VERIFY(!remote.active());QVERIFY(!local.active());
  QVERIFY(local.request(true,true));QTRY_VERIFY(remote.pending());local.cancel();QTRY_VERIFY(!remote.pending());receiver.allow();QTest::qWait(20);QCOMPARE(approvedLocal.count(),1);
  QVERIFY(local.request(true,true));QTRY_VERIFY(remote.pending());receiver.allow();QTRY_COMPARE(approvedLocal.count(),2);QVERIFY(local.sharing()&&!remote.sharing());QCOMPARE(approvedLocal[1][1].toBool(),true);
  client.close();QTRY_VERIFY(!remote.active());QVERIFY(!local.active());
 }
 void timeoutAndSimultaneousRequests(){
  Peer server,client;ConsentWindow receiver,sender;SessionConsent remote(&server,&receiver),local(&client,&sender);QSignalSpy connected(&client,&Peer::connected),ended(&local,&SessionConsent::ended);
  QVERIFY(server.listen(29882,"consent-code"));client.connectTo("127.0.0.1",29882,"consent-code");QTRY_COMPARE(connected.count(),1);
  local.setTimeout(100);QVERIFY(local.request(true,false));QTRY_VERIFY(remote.pending());QTRY_COMPARE(ended.count(),1);QTRY_VERIFY(!remote.pending());receiver.allow();QVERIFY(!local.active()&&!remote.active());
  local.setTimeout(30000);QVERIFY(local.request(true,false));QVERIFY(remote.request(true,false));QTRY_VERIFY(!local.pending()&&!remote.pending());QVERIFY(!local.active()&&!remote.active());
 }
};
QTEST_MAIN(ConsentTest)
#include "session_consent_test.moc"
