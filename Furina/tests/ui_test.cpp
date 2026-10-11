#include <QtTest>
#include <QSettings>
#include <QLineEdit>
#include <QPushButton>
#include <QCheckBox>
#include <QPlainTextEdit>
#include <QRadioButton>
#include <QComboBox>
#include <QTabWidget>
#include <QDialogButtonBox>
#include <QRegularExpression>
#include <QFrame>
#include "mainwindow.h"
static int requestCount(const QSignalSpy &spy){int count=0;for(const auto &row:spy)if(row[1].toUInt()==FR_APPROVAL_REQUEST)++count;return count;}
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


 void singleControlCancelsWaiting(){
  MainWindow w;w.show();auto *button=w.findChild<QPushButton*>("start");auto *control=w.findChild<CastControl*>();QVERIFY(button&&control);QVERIFY(!w.findChild<QPushButton*>("stop"));QVERIFY(button->text().isEmpty()&&!button->icon().isNull());QCOMPARE(button->iconSize(),w.findChild<QPushButton*>("settings")->iconSize());QCOMPARE(control->state(),CastControl::Start);
  w.findChild<QLineEdit*>("host")->setText("0.0.0.0");w.findChild<QLineEdit*>("token")->setText("control-test-code");
  QVERIFY(!w.findChild<QLabel*>("castHint")&&!w.findChild<QLabel*>("performanceHint"));
  QVERIFY(w.findChild<QFrame*>("modeSeparator")->isVisible());QVERIFY(w.findChild<QFrame*>("resolutionSeparator")->isVisible());
  button->click();QCOMPARE(control->state(),CastControl::Waiting);QVERIFY(button->isEnabled());const auto first=button->icon().cacheKey();QTest::qWait(160);QCOMPARE(control->state(),CastControl::Waiting);QVERIFY(first!=button->icon().cacheKey());w.grab().save("control-waiting.png");
  button->click();QCOMPARE(control->state(),CastControl::Start);QVERIFY(w.findChild<QCheckBox*>("share")->isEnabled());
  QTcpServer released;QVERIFY(released.listen(QHostAddress::AnyIPv4,27183));
 }

 void connectionSettingsAreSeparated(){
  MainWindow w;w.findChild<QPushButton*>("settings")->click();auto *dialog=w.findChild<QDialog*>("SettingsDialog");QTRY_VERIFY(dialog->isVisible());auto *tabs=w.findChild<QTabWidget*>("connectionTabs");QVERIFY(tabs&&tabs->count()==2);
  QVERIFY(w.findChild<QWidget*>("usbPage")->isAncestorOf(w.findChild<QLineEdit*>("serial")));QVERIFY(w.findChild<QWidget*>("wifiPage")->isAncestorOf(w.findChild<QLineEdit*>("host")));tabs->setCurrentIndex(1);QVERIFY(w.findChild<QLineEdit*>("host")->isVisible());tabs->setCurrentIndex(0);dialog->grab().save("mobile-usb-settings.png");tabs->setCurrentIndex(1);dialog->grab().save("mobile-wifi-settings.png");dialog->reject();
 }
 void receiveAndControlConsent(){
  MainWindow w;w.show();auto *view=w.findChild<ScreenView*>();auto *start=w.findChild<QPushButton*>("start"),*stop=start;auto *share=w.findChild<QCheckBox*>("share"),*receive=w.findChild<QCheckBox*>("receive"),*extend=w.findChild<QCheckBox*>("extend"),*duplicate=w.findChild<QCheckBox*>("duplicate");auto *consent=w.findChild<SessionConsent*>();auto *state=w.findChild<CastUiState*>();auto *control=w.findChild<CastControl*>();
  duplicate->setChecked(true);QVERIFY(receive->isChecked()&&!share->isChecked());QVERIFY(!view->hasImage()&&!view->busy());
  w.findChild<QPushButton*>("settings")->click();auto *dialog=w.findChild<QDialog*>("SettingsDialog");QTRY_VERIFY(dialog->isVisible());w.findChild<QLineEdit*>("token")->setText("black-ui-code");w.findChild<QLineEdit*>("host")->setText("0.0.0.0");w.findChild<QCheckBox*>("allowControl")->setChecked(false);dialog->accept();
  start->click();QVERIFY(state->locked()&&!state->occupied());Peer client;QSignalSpy connected(&client,&Peer::connected),status(&client,&Peer::status),sessions(&client,&Peer::session),approvals(&client,&Peer::approval),profiles(&client,&Peer::profile);client.connectTo("127.0.0.2",27183,"black-ui-code");QTRY_COMPARE(connected.count(),1);QCOMPARE(control->state(),CastControl::Stop);
  client.sendApproval(1001,FR_APPROVAL_REQUEST,FR_APPROVAL_SHARE);QTRY_VERIFY(w.findChild<QWidget*>("consentPanel")->isVisible());QVERIFY(!consent->active());QImage image(1024,600,QImage::Format_RGB32);image.fill(QColor(28,90,130));client.sendSession(FR_START_SHARE);client.sendFrame(image);QTest::qWait(30);QVERIFY(!view->hasImage());w.findChild<QPushButton*>("consentReject")->click();QTRY_VERIFY(!consent->pending());QVERIFY(!state->locked());
  QSignalSpy lateVideo(&client,&Peer::video);QByteArray late(FR_VIDEO_HEADER+1,0);fr_video_info info{FR_VIDEO_KEY,1,720,450,1,0};QVERIFY(fr_video_write(reinterpret_cast<uint8_t*>(late.data()),&info));w.findChild<AndroidBridge*>()->video(late);QTest::qWait(30);QCOMPARE(lateVideo.count(),0);
  client.sendApproval(1002,FR_APPROVAL_REQUEST,FR_APPROVAL_SHARE);QTRY_VERIFY(consent->pending());w.findChild<QPushButton*>("consentAccept")->click();QTRY_VERIFY(consent->active()&&!consent->sharing());QVERIFY(state->occupied()&&!view->busy());client.sendSession(FR_START_RECEIVE);client.sendFrame(image);QTRY_VERIFY(view->hasImage());
  QVERIFY(!share->isEnabled()&&!receive->isEnabled()&&!extend->isEnabled()&&!duplicate->isEnabled());share->click();share->setChecked(true);extend->setChecked(true);QVERIFY(receive->isChecked()&&!share->isChecked());QVERIFY(duplicate->isChecked()&&!extend->isChecked());QVERIFY(client.ready());
  const auto profileCount=profiles.count();w.findChild<QPushButton*>("resolution720")->click();QTRY_VERIFY(profiles.count()>profileCount);QCOMPARE(profiles.last()[0].toInt(),720);QVERIFY(consent->active());
  w.findChild<QPushButton*>("settings")->click();QTRY_VERIFY(dialog->isVisible());QVERIFY(!w.findChild<QLineEdit*>("host")->isEnabled());QVERIFY(!w.findChild<QCheckBox*>("allowControl")->isEnabled());QVERIFY(!w.findChild<QDialogButtonBox*>("buttonBox")->button(QDialogButtonBox::Save)->isEnabled());dialog->grab().save("mobile-locked-settings.png");dialog->accept();QVERIFY(client.ready()&&state->occupied());
  fr_gesture gesture{32768,32768,32768,32768,50};client.sendGesture(gesture);QTRY_VERIFY([&]{for(const auto &row:status)if(row[0].toString().contains("未允许"))return true;return false;}());
  client.sendSession(FR_MAXIMIZE);QTRY_VERIFY(w.isFullScreen());client.sendSession(FR_RESTORE);QTRY_VERIFY(!w.isFullScreen());QVERIFY(state->locked());w.grab().save("mobile-locked-receive.png");
  const auto grantTablet=[&](){const auto count=requestCount(approvals);if(control->state()!=CastControl::Start){stop->click();QTRY_VERIFY(!client.ready());}start->click();if(!client.ready())client.connectTo("127.0.0.2",27183,"black-ui-code");QTRY_VERIFY(client.ready());QTRY_COMPARE(requestCount(approvals),count+1);const auto row=approvals.last();QCOMPARE(row[1].toUInt(),quint32(FR_APPROVAL_REQUEST));client.sendApproval(row[0].toUInt(),FR_APPROVAL_ACCEPT,row[2].toUInt());QTRY_VERIFY(consent->active());QVERIFY(state->occupied());};
  stop->click();QTRY_VERIFY(!client.ready());QVERIFY(!state->locked()&&extend->isEnabled());extend->setChecked(true);grantTablet();QTRY_VERIFY(actionSeen(sessions,FR_EXTEND));client.sendSession(FR_DUPLICATE);QTest::qWait(30);QVERIFY(extend->isChecked()&&!duplicate->isChecked());
  stop->click();QTRY_VERIFY(!client.ready());w.findChild<QPushButton*>("settings")->click();QTRY_VERIFY(dialog->isVisible());w.findChild<QCheckBox*>("allowControl")->setChecked(true);dialog->accept();share->click();QVERIFY(share->isChecked()&&!receive->isChecked());grantTablet();QTRY_VERIFY(w.findChild<AndroidBridge*>()->wantFrame.load());QVERIFY(view->busy()&&!view->hasImage());view->setImage(image);QVERIFY(!view->hasImage());client.sendGesture(gesture);QTRY_VERIFY(w.findChild<QPlainTextEdit*>("log")->toPlainText().contains("模拟手势"));w.grab().save("mobile-sharing-busy.png");
  // Looking at locked settings must preserve an auto probe, otherwise closing
  // the dialog silently lowers 1440 to 1080 before any five-second frame loss.
  auto *bridge=w.findChild<AndroidBridge*>();auto *debug=w.findChild<QPlainTextEdit*>("log");w.findChild<QPushButton*>("resolution0")->click();bridge->videoStats(true,60,2,0,0,1080,674,1000);bridge->videoStats(true,60,2,0,0,1080,674,4000);QVERIFY(debug->toPlainText().contains("自动清晰度：最长边 1440"));
  w.findChild<QPushButton*>("settings")->click();QTRY_VERIFY(dialog->isVisible());dialog->reject();debug->clear();bridge->videoStats(true,49,2,0,0,1440,900,4999);QVERIFY(!debug->toPlainText().contains("自动清晰度：最长边"));bridge->videoStats(true,49,2,0,0,1440,900,1);QVERIFY(debug->toPlainText().contains("自动清晰度：最长边 1080"));QVERIFY(!debug->toPlainText().contains("自动清晰度：最长边 720"));QVERIFY(consent->active()&&state->occupied());
  const auto active=approvals.last();client.sendApproval(active[0].toUInt(),FR_APPROVAL_CANCEL,active[2].toUInt());QTRY_VERIFY(!consent->active());QVERIFY(!state->locked()&&share->isEnabled()&&!view->busy());
  grantTablet();client.close();QTRY_VERIFY(!w.findChild<Peer*>()->ready());QVERIFY(state->occupied()&&(start->isEnabled()&&control->state()==CastControl::Stop)&&!share->isEnabled());
  client.connectTo("127.0.0.2",27183,"black-ui-code");QTRY_VERIFY(client.ready());const auto replies=approvals.count();client.sendApproval(2001,FR_APPROVAL_REQUEST,FR_APPROVAL_SHARE);QTRY_COMPARE(approvals.count(),replies+1);QCOMPARE(approvals.last()[1].toUInt(),quint32(FR_APPROVAL_REJECT));QVERIFY(!consent->pending()&&!w.findChild<QWidget*>("consentPanel")->isVisible());QVERIFY(state->occupied()&&share->isChecked()&&view->busy());
  stop->click();QVERIFY(!state->locked()&&start->isEnabled()&&!view->hasImage()&&!view->busy());
  receive->click();w.resize(760,430);QTest::qWait(40);w.grab().save("mobile-unlocked-landscape.png");QCOMPARE(w.findChild<QWidget*>("sidebar")->width(),48);
 }

};
QTEST_MAIN(UiTest)
#include "ui_test.moc"
