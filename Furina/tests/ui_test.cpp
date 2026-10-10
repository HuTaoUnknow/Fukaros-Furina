#include <QtTest>
#include <QSettings>
#include <QLineEdit>
#include <QPushButton>
#include <QCheckBox>
#include <QPlainTextEdit>
#include <QRadioButton>
#include <QComboBox>
#include <QRegularExpression>
#include "mainwindow.h"
static bool actionSeen(const QSignalSpy &spy,quint32 action){for(const auto &row:spy)if(row[0].toUInt()==action)return true;return false;}
class UiTest:public QObject{
 Q_OBJECT
private slots:
 void initTestCase(){QCoreApplication::setOrganizationName("FurinaTests");QCoreApplication::setApplicationName("FurinaUi60");}
 void resolutionSelection(){
  QSettings settings;settings.remove("resolution");MainWindow w;w.show();
  auto *automatic=w.findChild<QPushButton*>("resolution0");auto *full=w.findChild<QPushButton*>("resolution1920");auto *hd=w.findChild<QPushButton*>("resolution1080");auto *low=w.findChild<QPushButton*>("resolution720");
  QVERIFY(automatic&&full&&hd&&low);QVERIFY(automatic->isChecked());QVERIFY(w.findChild<QWidget*>("resolutionPanel")->isVisible());
  full->click();QVERIFY(full->isChecked()&&!automatic->isChecked());settings.sync();QCOMPARE(settings.value("resolution").toInt(),1920);
  low->click();QVERIFY(low->isChecked()&&!full->isChecked());automatic->click();QVERIFY(automatic->isChecked()&&!low->isChecked());
 }

 void receiveAndControlConsent(){
  MainWindow w;w.show();auto *view=w.findChild<ScreenView*>();auto *start=w.findChild<QPushButton*>("start");auto *share=w.findChild<QCheckBox*>("share"),*receive=w.findChild<QCheckBox*>("receive");auto *consent=w.findChild<SessionConsent*>();
  w.findChild<QCheckBox*>("duplicate")->setChecked(true);QVERIFY(receive->isChecked()&&!share->isChecked());receive->click();QVERIFY(receive->isChecked());QVERIFY(!view->hasImage());w.grab().save("black-layout-waiting.png");
  w.findChild<QPushButton*>("settings")->click();auto *dialog=w.findChild<QDialog*>("SettingsDialog");QTRY_VERIFY(dialog->isVisible());w.findChild<QLineEdit*>("token")->setText("black-ui-code");w.findChild<QLineEdit*>("host")->setText("0.0.0.0");w.findChild<QCheckBox*>("allowControl")->setChecked(false);dialog->accept();
  start->click();QVERIFY(w.findChild<QPlainTextEdit*>("log")->toPlainText().contains("开始等待对端同意"));
  Peer client;QSignalSpy connected(&client,&Peer::connected),status(&client,&Peer::status),sessions(&client,&Peer::session),approvals(&client,&Peer::approval);client.connectTo("127.0.0.2",27183,"black-ui-code");QTRY_COMPARE(connected.count(),1);
  client.sendApproval(1001,FR_APPROVAL_REQUEST,FR_APPROVAL_SHARE);QTRY_VERIFY(w.findChild<QWidget*>("consentPanel")->isVisible());QVERIFY(!consent->active());
  QImage image(1024,600,QImage::Format_RGB32);image.fill(QColor(28,90,130));client.sendSession(FR_START_SHARE);client.sendFrame(image);QTest::qWait(30);QVERIFY(!view->hasImage());QVERIFY(!w.findChild<AndroidBridge*>()->wantFrame.load());
  w.findChild<QPushButton*>("consentReject")->click();QTRY_VERIFY(!consent->pending());QTRY_COMPARE(approvals.count(),1);QCOMPARE(approvals[0][1].toUInt(),quint32(FR_APPROVAL_REJECT));QVERIFY(!view->hasImage());
  QSignalSpy lateVideo(&client,&Peer::video);QByteArray late(FR_VIDEO_HEADER+1,0);fr_video_info info{FR_VIDEO_KEY,1,720,450,1,0};QVERIFY(fr_video_write(reinterpret_cast<uint8_t*>(late.data()),&info));w.findChild<AndroidBridge*>()->video(late);QTest::qWait(30);QCOMPARE(lateVideo.count(),0);
  client.sendApproval(1002,FR_APPROVAL_REQUEST,FR_APPROVAL_SHARE);QTRY_VERIFY(consent->pending());w.grab().save("black-layout-consent.png");w.findChild<QPushButton*>("consentAccept")->click();QTRY_COMPARE(approvals.count(),2);QCOMPARE(approvals[1][1].toUInt(),quint32(FR_APPROVAL_ACCEPT));QVERIFY(consent->active()&&!consent->sharing());QVERIFY(receive->isChecked()&&!share->isChecked());
  client.sendSession(FR_START_RECEIVE);client.sendFrame(image);QTRY_VERIFY(view->hasImage());
  fr_gesture gesture{32768,32768,32768,32768,50};client.sendGesture(gesture);QTRY_VERIFY([&]{for(const auto &row:status)if(row[0].toString().contains("未允许"))return true;return false;}());w.findChild<QCheckBox*>("allowControl")->setChecked(true);client.sendGesture(gesture);QTRY_VERIFY(w.findChild<QPlainTextEdit*>("log")->toPlainText().contains("模拟手势"));
  const auto log=w.findChild<QPlainTextEdit*>("log")->toPlainText();for(const auto &line:log.split('\n'))QVERIFY(QRegularExpression("^\\[\\d{2}:\\d{2}:\\d{2}\\] (中|EN) · .+").match(line).hasMatch());
  client.sendSession(FR_MAXIMIZE);QTRY_VERIFY(w.isFullScreen());client.sendSession(FR_RESTORE);QTRY_VERIFY(!w.isFullScreen());w.grab().save("black-layout-receive.png");
  const auto grantTablet=[&](){const auto count=approvals.count();start->click();client.connectTo("127.0.0.2",27183,"black-ui-code");QTRY_VERIFY(client.ready());QTRY_COMPARE(approvals.count(),count+1);const auto row=approvals.last();QCOMPARE(row[1].toUInt(),quint32(FR_APPROVAL_REQUEST));QVERIFY(!consent->active());client.sendApproval(row[0].toUInt(),FR_APPROVAL_ACCEPT,row[2].toUInt());QTRY_VERIFY(consent->active());};
  w.findChild<QCheckBox*>("extend")->setChecked(true);QTRY_VERIFY(!client.ready());grantTablet();QTRY_VERIFY(actionSeen(sessions,FR_EXTEND));QTRY_VERIFY(actionSeen(sessions,FR_START_SHARE));
  share->click();QVERIFY(share->isChecked()&&!receive->isChecked());QTRY_VERIFY(!client.ready());QVERIFY(!w.findChild<QWidget*>("modePanel")->isVisible());grantTablet();QTRY_VERIFY(w.findChild<AndroidBridge*>()->wantFrame.load());
  w.findChild<QPushButton*>("maximize")->click();QTRY_VERIFY(actionSeen(sessions,FR_MAXIMIZE));w.findChild<QPushButton*>("stop")->click();QTRY_VERIFY(!client.ready());QVERIFY(!w.findChild<AndroidBridge*>()->wantFrame.load());QVERIFY(!view->hasImage()&&!consent->active());
  receive->click();w.resize(760,430);QTest::qWait(40);w.grab().save("black-layout-phone-landscape.png");QCOMPARE(w.findChild<QWidget*>("sidebar")->width(),48);
 }
};
QTEST_MAIN(UiTest)
#include "ui_test.moc"
