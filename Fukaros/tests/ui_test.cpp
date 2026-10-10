#include <QtTest>
#include <QSettings>
#include <QLineEdit>
#include <QPushButton>
#include <QPlainTextEdit>
#include <QComboBox>
#include <QTabWidget>
#include <QDialogButtonBox>
#include <QCheckBox>
#include <QLabel>
#include <QFrame>
#include <QRegularExpression>
#include "mainwindow.h"
Q_DECLARE_METATYPE(fr_gesture)
static bool actionSeen(const QSignalSpy &spy,quint32 action){for(const auto &row:spy)if(row[0].toUInt()==action)return true;return false;}
static int requestCount(const QSignalSpy &spy){int count=0;for(const auto &row:spy)if(row[1].toUInt()==FR_APPROVAL_REQUEST)++count;return count;}
class UiTest:public QObject{
 Q_OBJECT
private slots:
 void initTestCase(){QCoreApplication::setOrganizationName("FurinaTests");QCoreApplication::setApplicationName("FukarosUi60");}
 void resolutionSelection(){
  QSettings settings;settings.remove("resolution");MainWindow w;w.show();
  auto *automatic=w.findChild<QPushButton*>("resolution0");auto *full=w.findChild<QPushButton*>("resolution1920");auto *hd=w.findChild<QPushButton*>("resolution1080");auto *low=w.findChild<QPushButton*>("resolution720");
  QVERIFY(automatic&&full&&hd&&low);QVERIFY(automatic->isChecked());QVERIFY(w.findChild<QWidget*>("resolutionPanel")->isVisible());
  full->click();QVERIFY(full->isChecked()&&!automatic->isChecked());settings.sync();QCOMPARE(settings.value("resolution").toInt(),1920);
  low->click();QVERIFY(low->isChecked()&&!full->isChecked());automatic->click();QVERIFY(automatic->isChecked()&&!low->isChecked());
 }
 void fullscreenRestoresWindow(){
  MainWindow w;w.show();QTest::qWait(30);auto *presentation=w.findChild<Presentation*>();auto *view=w.findChild<ScreenView*>();QVERIFY(presentation&&view);
  const auto flags=w.windowFlags();const auto geometry=w.geometry();
  presentation->setActive(true);QTRY_VERIFY(w.isFullScreen());QVERIFY(!w.findChild<QWidget*>("sidebar")->isVisible());
  QCOMPARE(view->mapTo(w.centralWidget(),QPoint()),QPoint());QCOMPARE(view->size(),w.centralWidget()->size());
#ifdef Q_OS_WIN
  QVERIFY(w.windowFlags().testFlag(Qt::FramelessWindowHint));
#endif
  QTest::keyClick(view,Qt::Key_Escape);QTRY_VERIFY(!w.isFullScreen());QCOMPARE(w.windowFlags(),flags);QCOMPARE(w.geometry(),geometry);QVERIFY(w.findChild<QWidget*>("sidebar")->isVisible());
  w.showMaximized();QTRY_VERIFY(w.isMaximized());presentation->setActive(true);QTRY_VERIFY(w.isFullScreen());presentation->setActive(false);QTRY_VERIFY(w.isMaximized());QCOMPARE(w.windowFlags(),flags);
 }

 void windowButtonsAndExclusiveDirection(){
  MainWindow w;w.show();QTest::qWait(30);auto *share=w.findChild<QCheckBox*>("share"),*receive=w.findChild<QCheckBox*>("receive");
  share->click();QVERIFY(share->isChecked()&&!receive->isChecked());share->click();QVERIFY(share->isChecked()&&!receive->isChecked());receive->click();QVERIFY(!share->isChecked()&&receive->isChecked());
  auto *controls=w.findChild<QWidget*>("windowControls");auto *view=w.findChild<ScreenView*>();QVERIFY(controls->isVisible());QVERIFY(view->rect().contains(controls->geometry()));
  auto *maximize=w.findChild<QPushButton*>("windowMaximize");QVERIFY(!maximize->icon().isNull());maximize->click();QTRY_VERIFY(w.isMaximized());maximize->click();QTRY_VERIFY(!w.isMaximized());
  w.findChild<QPushButton*>("windowMinimize")->click();QTRY_VERIFY(w.isMinimized());w.showNormal();
  auto *presentation=w.findChild<Presentation*>();presentation->setActive(true);QTRY_VERIFY(w.isFullScreen());QVERIFY(controls->isVisible());QVERIFY(!controls->geometry().intersects(w.findChild<QPushButton*>("exitFullScreen")->geometry()));
  maximize->click();QTRY_VERIFY(!w.isFullScreen()&&w.isMaximized());w.findChild<QPushButton*>("windowClose")->click();QVERIFY(!w.isVisible());
 }

 void connectionSettingsAreSeparated(){
  MainWindow w;w.show();w.findChild<QPushButton*>("settings")->click();auto *dialog=w.findChild<QDialog*>("SettingsDialog");QTRY_VERIFY(dialog->isVisible());
  auto *tabs=w.findChild<QTabWidget*>("transport");QVERIFY(tabs&&tabs->count()==2);QCOMPARE(tabs->tabText(0),QString("USB 有线"));QCOMPARE(tabs->tabText(1),QString("Wi-Fi 无线"));
  auto *usb=w.findChild<QWidget*>("usbPage"),*wifi=w.findChild<QWidget*>("wifiPage");QVERIFY(usb->isAncestorOf(w.findChild<QLineEdit*>("serial")));QVERIFY(usb->isAncestorOf(w.findChild<QLineEdit*>("adbPath")));QVERIFY(wifi->isAncestorOf(w.findChild<QLineEdit*>("host")));QVERIFY(wifi->isAncestorOf(w.findChild<QLineEdit*>("adbEndpoint")));
  w.findChild<QLineEdit*>("host")->setText("127.0.0.2");w.findChild<QLineEdit*>("serial")->setText("usb-example");tabs->setCurrentIndex(1);QCOMPARE(w.findChild<QLineEdit*>("serial")->text(),QString("usb-example"));tabs->setCurrentIndex(0);QCOMPARE(w.findChild<QLineEdit*>("host")->text(),QString("127.0.0.2"));tabs->setCurrentIndex(0);dialog->grab().save("pc-usb-settings.png");tabs->setCurrentIndex(1);dialog->grab().save("pc-wifi-settings.png");dialog->reject();
 }
 void invalidConnectionReleasesPendingLock(){
  MainWindow w;w.findChild<QLineEdit*>("host")->setText("invalid-ip");w.findChild<QLineEdit*>("token")->setText("test-code");w.findChild<QTabWidget*>("transport")->setCurrentIndex(1);w.findChild<QPushButton*>("start")->click();
  QVERIFY(!w.findChild<CastUiState*>()->locked());QVERIFY(w.findChild<QCheckBox*>("share")->isEnabled());QVERIFY(w.findChild<QPushButton*>("start")->isEnabled());
 }
 void remoteScreenAndInput(){
  qRegisterMetaType<fr_gesture>();Peer server;QVERIFY(server.listen(27183,"ui-test-code"));
  MainWindow w;w.show();auto *view=w.findChild<ScreenView*>();auto *mode=w.findChild<QCheckBox*>("extend"),*duplicate=w.findChild<QCheckBox*>("duplicate");auto *source=w.findChild<QComboBox*>("castSource");
  auto *start=w.findChild<QPushButton*>("start"),*stop=w.findChild<QPushButton*>("stop");auto *share=w.findChild<QCheckBox*>("share"),*receive=w.findChild<QCheckBox*>("receive");auto *state=w.findChild<CastUiState*>();
  duplicate->setChecked(true);QVERIFY(receive->isChecked()&&!share->isChecked());QVERIFY(!w.findChild<QWidget*>("modePanel")->isVisible());
  QCOMPARE(start->width(),40);QCOMPARE(start->width(),start->height());QCOMPARE(share->mapTo(&w,QPoint()).y(),receive->mapTo(&w,QPoint()).y());auto *sidebar=w.findChild<QWidget*>("sidebar");QCOMPARE(sidebar->width(),48);QVERIFY(!view->hasImage()&&!view->busy());
  w.findChild<QPushButton*>("settings")->click();auto *dialog=w.findChild<QDialog*>("SettingsDialog");QTRY_VERIFY(dialog->isVisible());
  w.findChild<QLineEdit*>("host")->setText("127.0.0.2");w.findChild<QLineEdit*>("token")->setText("ui-test-code");w.findChild<QTabWidget*>("transport")->setCurrentIndex(1);dialog->accept();
  auto *client=w.findChild<Peer*>();auto *consent=w.findChild<SessionConsent*>();QSignalSpy connected(client,&Peer::connected),gestures(&server,&Peer::gesture),keys(&server,&Peer::key),sessions(&server,&Peer::session),approvals(&server,&Peer::approval),profiles(&server,&Peer::profile);
  QImage image(1024,600,QImage::Format_RGB32);image.fill(QColor(48,78,118));
  start->click();QTRY_COMPARE(connected.count(),1);QTRY_COMPARE(approvals.count(),1);QVERIFY(state->locked()&&!state->occupied());QVERIFY(!consent->active());QVERIFY(!actionSeen(sessions,FR_START_SHARE));
  server.sendSession(FR_START_SHARE);server.sendFrame(image);QTest::qWait(40);QVERIFY(!view->hasImage());server.sendApproval(approvals[0][0].toUInt(),FR_APPROVAL_REJECT,approvals[0][2].toUInt());QTRY_VERIFY(start->isEnabled());QVERIFY(!state->locked()&&share->isEnabled());
  const auto grant=[&](){const auto count=requestCount(approvals);start->click();QTRY_COMPARE(requestCount(approvals),count+1);const auto row=approvals.last();QCOMPARE(row[1].toUInt(),quint32(FR_APPROVAL_REQUEST));server.sendApproval(row[0].toUInt(),FR_APPROVAL_ACCEPT,row[2].toUInt());QTRY_VERIFY(consent->active());QVERIFY(state->occupied());};
  grant();QTRY_VERIFY(actionSeen(sessions,FR_START_SHARE));server.sendFrame(image);QTRY_VERIFY(view->hasImage());QVERIFY(!view->busy());
  QVERIFY(!share->isEnabled()&&!receive->isEnabled()&&!mode->isEnabled()&&!duplicate->isEnabled());
  share->click();share->setChecked(true);mode->setChecked(true);QVERIFY(receive->isChecked()&&!share->isChecked());QVERIFY(duplicate->isChecked()&&!mode->isChecked());QVERIFY(server.ready()&&consent->active());
  const auto profileCount=profiles.count();w.findChild<QPushButton*>("resolution720")->click();QTRY_VERIFY(profiles.count()>profileCount);QCOMPARE(profiles.last()[0].toInt(),720);QVERIFY(server.ready()&&state->occupied());
  w.findChild<QPushButton*>("settings")->click();QTRY_VERIFY(dialog->isVisible());QVERIFY(!w.findChild<QTabWidget*>("transport")->isEnabled());QVERIFY(!w.findChild<QLineEdit*>("token")->isEnabled());QVERIFY(!w.findChild<QDialogButtonBox*>("buttonBox")->button(QDialogButtonBox::Save)->isEnabled());dialog->grab().save("pc-locked-settings.png");dialog->accept();QVERIFY(server.ready()&&consent->active());
  QTest::mouseClick(view,Qt::LeftButton,Qt::NoModifier,view->rect().center());QTRY_COMPARE(gestures.count(),1);auto g=qvariant_cast<fr_gesture>(gestures[0][0]);QVERIFY(qAbs(int(g.x1)-32768)<500);w.findChild<QPushButton*>("home")->click();QTRY_COMPARE(keys.count(),1);
  const auto log=w.findChild<QPlainTextEdit*>("log")->toPlainText();for(const auto &line:log.split('\n'))QVERIFY(QRegularExpression("^\\[\\d{2}:\\d{2}:\\d{2}\\] (中|EN) · .+").match(line).hasMatch());
  w.grab().save("pc-locked-receive.png");w.findChild<QPushButton*>("maximize")->click();QTRY_VERIFY(w.isFullScreen());w.findChild<QPushButton*>("exitFullScreen")->click();QTRY_VERIFY(!w.isFullScreen());QVERIFY(state->locked());
  stop->click();QTRY_VERIFY(!server.ready());QVERIFY(!state->locked()&&share->isEnabled());share->click();QVERIFY(share->isChecked()&&!receive->isChecked());grant();QVERIFY(consent->sharing()&&view->busy());view->setImage(image);QVERIFY(!view->hasImage());
  server.sendSession(FR_EXTEND);QTest::qWait(30);QVERIFY(duplicate->isChecked()&&!mode->isChecked());QVERIFY(server.ready()&&state->occupied());w.grab().save("pc-sharing-busy.png");w.findChild<QPushButton*>("maximize")->click();QTRY_VERIFY(actionSeen(sessions,FR_MAXIMIZE));
  stop->click();QTRY_VERIFY(!server.ready());mode->setChecked(true);grant();
  if(qEnvironmentVariableIsSet("FURINA_TEST_MULTI_SCREEN")){QCOMPARE(QGuiApplication::screens().size(),3);QCOMPARE(source->count(),1);QVERIFY(source->currentText().startsWith("extended-test"));QTRY_VERIFY(w.findChild<QPlainTextEdit*>("log")->toPlainText().contains("扩展投屏已启动：extended-test"));}
  else{QCOMPARE(QGuiApplication::screens().size(),1);QTRY_VERIFY(w.findChild<QPlainTextEdit*>("log")->toPlainText().contains("扩展暂不可用"));}
  QVERIFY(!source->isEnabled()&&!start->isEnabled());QVERIFY(state->locked());stop->click();QTRY_VERIFY(!server.ready());duplicate->setChecked(true);receive->click();grant();server.sendFrame(image);QTRY_VERIFY(view->hasImage());
  server.close();QTRY_VERIFY(!client->ready());QVERIFY(state->occupied()&&!start->isEnabled()&&!share->isEnabled());stop->click();QVERIFY(start->isEnabled()&&share->isEnabled()&&!state->locked()&&!view->busy()&&!view->hasImage());
 }

};
QTEST_MAIN(UiTest)
#include "ui_test.moc"
