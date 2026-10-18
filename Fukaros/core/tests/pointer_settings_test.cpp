#include <QtTest>
#include <QSettings>
#include "pointer_settings.h"
class PointerTest:public QObject{
 Q_OBJECT
private slots:
 void initTestCase(){QCoreApplication::setOrganizationName("FurinaTests");QCoreApplication::setApplicationName("PointerSettings");QSettings().clear();}
 void synchronizeBothDirections(){
  QWidget desktop,mobile;QDoubleSpinBox a(&desktop),b(&mobile);for(auto *input:{&a,&b}){input->setObjectName("pointerMultiplier");input->setRange(.25,4);input->setDecimals(2);}
  QCheckBox mapA(&desktop),mapB(&mobile);mapA.setObjectName("independentCursor");mapB.setObjectName("independentCursor");
  Peer server,client;PointerSettings pc(&client,&desktop,true),tablet(&server,&mobile,false);
  QVERIFY(mapA.isChecked()&&mapB.isChecked());
  a.setValue(2);QTest::qWait(3);b.setValue(1.5);
  QVERIFY(server.listen(29893,"pointer-code","127.0.0.1"));client.connectTo("127.0.0.1",29893,"pointer-code");QTRY_VERIFY(client.ready());QTRY_COMPARE(a.value(),1.5);
  mapA.setChecked(false);QTRY_VERIFY(!mapB.isChecked());mapB.setChecked(true);QTRY_VERIFY(mapA.isChecked());
  server.sendCursorPreference(false,0);QTest::qWait(30);QVERIFY(mapA.isChecked()&&mapB.isChecked());
  a.setValue(2.25);QTRY_COMPARE(b.value(),2.25);
  b.setValue(.5);QTRY_COMPARE(a.value(),.5);
  QSignalSpy changes(&pc,&PointerSettings::changed);server.sendPointerSpeed(300,0);QTest::qWait(50);QCOMPARE(a.value(),.5);QCOMPARE(b.value(),.5);QCOMPARE(changes.size(),0);
  QSignalSpy packets(&client,&Peer::pointerSpeed);server.sendPointerSpeed(24,1);server.sendPointerSpeed(401,1);QTest::qWait(30);QCOMPARE(packets.size(),0);
  QCOMPARE(QSettings().value("pointerPercent").toInt(),50);
 }
};
QTEST_MAIN(PointerTest)
#include "pointer_settings_test.moc"
