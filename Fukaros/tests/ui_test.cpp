#include <QtTest>
#include <QSettings>
#include <QLineEdit>
#include <QPushButton>
#include <QPlainTextEdit>
#include <QComboBox>
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
 void remoteScreenAndInput(){
  qRegisterMetaType<fr_gesture>();Peer server;QVERIFY(server.listen(27183,"ui-test-code"));
  MainWindow w;w.show();auto *view=w.findChild<ScreenView*>();auto *mode=w.findChild<QCheckBox*>("extend"),*duplicate=w.findChild<QCheckBox*>("duplicate");auto *source=w.findChild<QComboBox*>("castSource");
  auto *start=w.findChild<QPushButton*>("start");auto *share=w.findChild<QCheckBox*>("share"),*receive=w.findChild<QCheckBox*>("receive");
  duplicate->setChecked(true);QVERIFY(receive->isChecked()&&!share->isChecked());QVERIFY(!w.findChild<QWidget*>("modePanel")->isVisible());
  QCOMPARE(start->width(),40);QCOMPARE(start->width(),start->height());QCOMPARE(share->mapTo(&w,QPoint()).y(),receive->mapTo(&w,QPoint()).y());
  auto *sidebar=w.findChild<QWidget*>("sidebar");QCOMPARE(sidebar->width(),48);QVERIFY(!view->hasImage());w.grab().save("white-layout-waiting.png");
  w.findChild<QPushButton*>("settings")->click();auto *dialog=w.findChild<QDialog*>("SettingsDialog");QTRY_VERIFY(dialog->isVisible());
  w.findChild<QLineEdit*>("host")->setText("127.0.0.2");w.findChild<QLineEdit*>("token")->setText("ui-test-code");w.findChild<QComboBox*>("transport")->setCurrentIndex(1);dialog->accept();
  auto *client=w.findChild<Peer*>();auto *consent=w.findChild<SessionConsent*>();QSignalSpy connected(client,&Peer::connected),gestures(&server,&Peer::gesture),keys(&server,&Peer::key),sessions(&server,&Peer::session),approvals(&server,&Peer::approval);
  QImage image(1024,600,QImage::Format_RGB32);image.fill(QColor(48,78,118));
  start->click();QTRY_COMPARE(connected.count(),1);QTRY_COMPARE(approvals.count(),1);QVERIFY(!consent->active());QVERIFY(!actionSeen(sessions,FR_START_SHARE));
  QVERIFY(w.findChild<QPlainTextEdit*>("log")->toPlainText().contains("开始等待对端同意"));
  server.sendSession(FR_START_SHARE);server.sendFrame(image);QTest::qWait(40);QVERIFY(!view->hasImage());
  server.sendApproval(approvals[0][0].toUInt(),FR_APPROVAL_REJECT,approvals[0][2].toUInt());QTRY_VERIFY(start->isEnabled());QVERIFY(!consent->active());
  const auto grant=[&](){const auto count=requestCount(approvals);start->click();QTRY_COMPARE(requestCount(approvals),count+1);const auto row=approvals.last();QCOMPARE(row[1].toUInt(),quint32(FR_APPROVAL_REQUEST));QVERIFY(!consent->active());server.sendApproval(row[0].toUInt(),FR_APPROVAL_ACCEPT,row[2].toUInt());QTRY_VERIFY(consent->active());};
  grant();QTRY_VERIFY(actionSeen(sessions,FR_START_SHARE));server.sendFrame(image);QTRY_VERIFY(view->hasImage());
  QTest::mouseClick(view,Qt::LeftButton,Qt::NoModifier,view->rect().center());QTRY_COMPARE(gestures.count(),1);auto g=qvariant_cast<fr_gesture>(gestures[0][0]);QVERIFY(qAbs(int(g.x1)-32768)<500);
  w.findChild<QPushButton*>("home")->click();QTRY_COMPARE(keys.count(),1);QCOMPARE(keys[0][0].toUInt(),3u);
  const auto log=w.findChild<QPlainTextEdit*>("log")->toPlainText();QVERIFY(log.contains("中 · 配对成功"));QVERIFY(log.contains("EN · Paired"));
  for(const auto &line:log.split('\n'))QVERIFY(QRegularExpression("^\\[\\d{2}:\\d{2}:\\d{2}\\] (中|EN) · .+").match(line).hasMatch());
  w.grab().save("white-layout-receive.png");w.findChild<QPushButton*>("maximize")->click();QTRY_VERIFY(w.isFullScreen());QVERIFY(!sidebar->isVisible());QVERIFY(w.findChild<QWidget*>("windowControls")->isVisible());w.findChild<QPushButton*>("exitFullScreen")->click();QTRY_VERIFY(!w.isFullScreen());
  share->click();QVERIFY(share->isChecked()&&!receive->isChecked());QTRY_VERIFY(!server.ready());QVERIFY(start->isEnabled());QVERIFY(!consent->active());QVERIFY(w.findChild<QWidget*>("modePanel")->isVisible());
  grant();QVERIFY(consent->sharing());w.findChild<QPushButton*>("maximize")->click();QTRY_VERIFY(actionSeen(sessions,FR_MAXIMIZE));
  mode->setChecked(true);QVERIFY(!duplicate->isChecked());QTRY_VERIFY(!server.ready());grant();
  if(qEnvironmentVariableIsSet("FURINA_TEST_MULTI_SCREEN")){
   QCOMPARE(QGuiApplication::screens().size(),3);QVERIFY(source->isEnabled());QCOMPARE(source->count(),1);QVERIFY(source->currentText().startsWith("extended-test"));
   QTRY_VERIFY(w.findChild<QPlainTextEdit*>("log")->toPlainText().contains("扩展投屏已启动：extended-test"));
  }else{QCOMPARE(QGuiApplication::screens().size(),1);QVERIFY(!source->isEnabled());QTRY_VERIFY(start->isEnabled());QVERIFY(w.findChild<QPlainTextEdit*>("log")->toPlainText().contains("扩展暂不可用"));}
  duplicate->setChecked(true);grant();QVERIFY(consent->sharing());w.grab().save("white-layout-share.png");receive->click();QVERIFY(receive->isChecked()&&!share->isChecked());QTRY_VERIFY(!server.ready());grant();server.sendFrame(image);QTRY_VERIFY(view->hasImage());
  w.findChild<QPushButton*>("stop")->click();QTRY_VERIFY(!server.ready());QVERIFY(start->isEnabled());QVERIFY(!view->hasImage()&&!consent->active());
 }
};
QTEST_MAIN(UiTest)
#include "ui_test.moc"
