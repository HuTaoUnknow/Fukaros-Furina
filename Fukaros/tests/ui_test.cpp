#include <QtTest>
#include <QDesktopServices>
#include <QUrl>
#include <QStackedWidget>
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
#include <QScrollArea>
#include <QScrollBar>
#include "settings_support.h"
#include <QAction>
#include <QSystemTrayIcon>
#include "desktop_tray.h"
#include <QRegularExpression>
#include "mainwindow.h"
Q_DECLARE_METATYPE(fr_gesture)
static bool actionSeen(const QSignalSpy &spy,quint32 action){for(const auto &row:spy)if(row[0].toUInt()==action)return true;return false;}
static int requestCount(const QSignalSpy &spy){int count=0;for(const auto &row:spy)if(row[1].toUInt()==FR_APPROVAL_REQUEST)++count;return count;}
class DisplaySettingsProbe:public QObject{
 Q_OBJECT
public:int opened=0;QUrl last;
 Q_INVOKABLE void open(const QUrl &url){++opened;last=url;}
};
class UiTest:public QObject{
 Q_OBJECT
private slots:
 void fullscreenStateRequiresReceiverConfirmation(){
  MainWindow w;w.show();auto *peer=w.findChild<Peer*>();auto *consent=w.findChild<SessionConsent*>();auto *maximize=w.findChild<QPushButton*>("maximize");
  Peer other;QSignalSpy actions(&other,&Peer::session);QVERIFY(other.listen(29910,"fullscreen-state-code","127.0.0.1"));peer->connectTo("127.0.0.1",29910,"fullscreen-state-code");QTRY_VERIFY(peer->ready()&&other.ready());
  other.sendApproval(4801,FR_APPROVAL_REQUEST,FR_APPROVAL_RECEIVE);QTRY_VERIFY(consent->pending());w.findChild<QPushButton*>("consentAccept")->click();QTRY_VERIFY(consent->sharing());
  QCOMPARE(maximize->toolTip(),QString("放大"));
  other.sendSession(FR_FULLSCREEN_CONFIRMED);QTRY_COMPARE(maximize->toolTip(),QString("还原"));QVERIFY(!w.isFullScreen());
  maximize->click();QTRY_VERIFY(actionSeen(actions,FR_RESTORE));QCOMPARE(maximize->toolTip(),QString("还原"));
  other.sendSession(FR_RESTORED_CONFIRMED);QTRY_COMPARE(maximize->toolTip(),QString("放大"));
  actions.clear();maximize->click();maximize->click();QTRY_COMPARE(actions.count(),2);QCOMPARE(actions[0][0].toUInt(),quint32(FR_MAXIMIZE));QCOMPARE(actions[1][0].toUInt(),quint32(FR_RESTORE));
  other.sendSession(FR_FULLSCREEN_CONFIRMED);other.sendSession(FR_RESTORED_CONFIRMED);QTRY_COMPARE(maximize->toolTip(),QString("放大"));QTest::qWait(30);QCOMPARE(actions.count(),2);
  other.sendApproval(4801,FR_APPROVAL_CANCEL,FR_APPROVAL_RECEIVE);QTRY_VERIFY(!consent->active());other.sendSession(FR_FULLSCREEN_CONFIRMED);QTest::qWait(30);QCOMPARE(maximize->toolTip(),QString("放大"));
  actions.clear();other.sendApproval(4802,FR_APPROVAL_REQUEST,FR_APPROVAL_SHARE);QTRY_VERIFY(consent->pending());w.findChild<QPushButton*>("consentAccept")->click();QTRY_VERIFY(consent->active()&&!consent->sharing());
  QTRY_VERIFY(actionSeen(actions,FR_RESTORED_CONFIRMED));actions.clear();maximize->click();QTRY_VERIFY(w.isFullScreen());QTRY_VERIFY(actionSeen(actions,FR_FULLSCREEN_CONFIRMED));
  w.findChild<ScreenView*>()->leaveFullScreen();QTRY_VERIFY(!w.isFullScreen());QTRY_VERIFY(actionSeen(actions,FR_RESTORED_CONFIRMED));
  actions.clear();other.sendSession(FR_RESTORE);QTRY_COMPARE(actions.count(),1);QCOMPARE(actions[0][0].toUInt(),quint32(FR_RESTORED_CONFIRMED));
  other.close();QTRY_VERIFY(!peer->ready());QCOMPARE(maximize->toolTip(),QString("放大"));
 }
 void chineseDebugParameters(){
  const QString english="width=1920 height=1080 fps=60 latency=3 ms target=60 FPS codec=h264 GPU=1.5 ms";
  const auto entry=furinaLog(english,english);const auto chinese=entry.section('\n',0,0);
  for(const auto *word:{"宽度=1920","高度=1080","帧率=60","延迟=3 毫秒","目标=60 帧/秒","编解码器=h264","图形处理器=1.5 毫秒"})QVERIFY2(chinese.contains(QString::fromUtf8(word)),qPrintable(chinese));
  QVERIFY(entry.section('\n',1).endsWith(english));
 }
 void initTestCase(){QCoreApplication::setOrganizationName("FurinaTests");QCoreApplication::setApplicationName("FukarosUi60");}
 void nativeExtensionPositionFlow(){
  if(QGuiApplication::platformName()!="windows"||!fr_virtual_available())QSKIP("Requires the installed Windows virtual display driver");
  QCoreApplication::setApplicationName("FukarosPositionSmoke049");QSettings settings;settings.remove("extensionPosition");settings.sync();
  struct Scope{~Scope(){QDesktopServices::unsetUrlHandler("ms-settings");QCoreApplication::setApplicationName("FukarosUi60");}}scope;
  DisplaySettingsProbe probe;QDesktopServices::setUrlHandler("ms-settings",&probe,"open");
  Peer server;QVERIFY(server.listen(27183,"extension-flow-test","127.0.0.2"));QSignalSpy approvals(&server,&Peer::approval),video(&server,&Peer::video);
  MainWindow window;window.show();window.findChild<QLineEdit*>("host")->setText("127.0.0.2");window.findChild<QLineEdit*>("token")->setText("extension-flow-test");window.findChild<QTabWidget*>("transport")->setCurrentIndex(1);window.findChild<QCheckBox*>("share")->click();window.findChild<QCheckBox*>("extend")->setChecked(true);
  auto *start=window.findChild<QPushButton*>("start");auto *dialog=window.findChild<QDialog*>("PositionDialog");
  for(int cycle=0;cycle<2;cycle++){
   approvals.clear();video.clear();start->click();QTRY_COMPARE(approvals.count(),1);const auto approval=approvals.first();server.sendApproval(approval[0].toUInt(),FR_APPROVAL_ACCEPT,approval[2].toUInt());
   QTRY_VERIFY_WITH_TIMEOUT(dialog->isVisible(),15000);QCOMPARE(video.count(),0);
   if(cycle==0){QCOMPARE(probe.opened,1);QCOMPARE(probe.last,QUrl("ms-settings:display"));fr_virtual_display borrowed={};borrowed.acquired=1;QCOMPARE(fr_virtual_set_position(&borrowed,-1920,0),0L);dialog->findChild<QPushButton*>("finishPosition")->click();}
   else{QCOMPARE(dialog->findChild<QStackedWidget*>("positionPages")->currentWidget()->objectName(),QString("choicePage"));dialog->findChild<QPushButton*>("inheritPosition")->click();QCOMPARE(probe.opened,1);}
   QTRY_VERIFY_WITH_TIMEOUT(video.count()>0,15000);fr_virtual_display borrowed={};borrowed.acquired=1;long x=0,y=0;QCOMPARE(fr_virtual_get_position(&borrowed,&x,&y),0L);QCOMPARE(x,-1920L);QCOMPARE(y,0L);
   start->click();QTRY_VERIFY(!server.ready());QTRY_VERIFY(!dialog->isVisible());
  }
  // Stopping while the placement choice is open cancels the connection and
  // cannot let a delayed placement callback start a new encoder.
  approvals.clear();video.clear();start->click();QTRY_COMPARE(approvals.count(),1);const auto approval=approvals.first();server.sendApproval(approval[0].toUInt(),FR_APPROVAL_ACCEPT,approval[2].toUInt());QTRY_VERIFY_WITH_TIMEOUT(dialog->isVisible(),15000);QCOMPARE(dialog->windowModality(),Qt::NonModal);start->click();QTRY_VERIFY(!server.ready());QVERIFY(!dialog->isVisible());QTest::qWait(300);QCOMPARE(video.count(),0);
 }

 void stepsAndRequiredFields(){
  MainWindow w;w.show();w.findChild<QPushButton*>("settings")->click();auto *dialog=w.findChild<QDialog*>("SettingsDialog");QTRY_VERIFY(dialog->isVisible());
  QVERIFY(!w.findChild<QLineEdit*>("token")->property("settingsRole").isValid());QCOMPARE(w.findChild<QLineEdit*>("host")->property("settingsRole").toString(),QString("required"));
  for(const auto *name:{"usbHost","serial","adbPath","adbEndpoint"})QCOMPARE(w.findChild<QLineEdit*>(name)->property("settingsRole").toString(),QString("optional"));
  auto *tabs=w.findChild<QTabWidget*>("transport");tabs->setCurrentIndex(0);auto *scroll=w.findChild<QScrollArea*>("settingsScroll");scroll->verticalScrollBar()->setValue(0);QTest::qWait(30);
  auto *page=w.findChild<QWidget*>("usbPage");
  for(const auto *name:{"usbHost","serial","adbPath"}){auto *field=w.findChild<QLineEdit*>(name);QVERIFY(page->rect().contains(QRect(field->mapTo(page,QPoint()),field->size())));}
  w.findChild<StepHelpButton*>("usbSteps")->click();auto *popup=dialog->findChild<QFrame*>("stepsPopup");QVERIFY(popup&&popup->isVisible());QVERIFY(dialog->rect().contains(popup->geometry()));dialog->grab().save("pc-usb-help.png");popup->findChild<QPushButton*>("closeSteps")->click();
  QVERIFY(!w.findChild<QLabel*>("usbHint")&&!w.findChild<QLabel*>("wifiHint")&&!w.findChild<QLabel*>("connectionHint"));dialog->reject();
 }

 void usbNetworkDoesNotStartAdb(){
  MainWindow w;w.findChild<QLineEdit*>("token")->setText("usb-ui-test-code");w.findChild<QLineEdit*>("adbPath")->setText("missing-adb.exe");
  w.findChild<QComboBox*>("usbMode")->setCurrentIndex(0);w.findChild<QLineEdit*>("usbHost")->clear();w.findChild<QTabWidget*>("transport")->setCurrentIndex(0);
  auto *button=w.findChild<QPushButton*>("start");auto *control=w.findChild<CastControl*>();button->click();QCOMPARE(control->state(),CastControl::Waiting);
  for(auto *process:w.findChildren<QProcess*>())QCOMPARE(process->state(),QProcess::NotRunning);
  button->click();QCOMPARE(control->state(),CastControl::Start);QVERIFY(w.findChild<QComboBox*>("usbMode")->isEnabled());
  w.findChild<QLineEdit*>("usbHost")->setText("invalid-ip");button->click();QCOMPARE(control->state(),CastControl::Start);
  QVERIFY(w.findChild<QPlainTextEdit*>("log")->toPlainText().contains("有线 平板 网络地址 无效"));
 }

 void stopSynchronization_data(){
  QTest::addColumn<bool>("remoteStops");QTest::addColumn<bool>("remoteShares");
  QTest::newRow("local-stop-receiving")<<false<<true;QTest::newRow("local-stop-sharing")<<false<<false;
  QTest::newRow("remote-stop-receiving")<<true<<true;QTest::newRow("remote-stop-sharing")<<true<<false;
 }
 void stopSynchronization(){
  QFETCH(bool,remoteStops);QFETCH(bool,remoteShares);MainWindow w;w.show();
  auto *peer=w.findChild<Peer*>();auto *consent=w.findChild<SessionConsent*>();auto *state=w.findChild<CastUiState*>();auto *control=w.findChild<CastControl*>();auto *button=w.findChild<QPushButton*>("start");auto *view=w.findChild<ScreenView*>();
  Peer other;QSignalSpy approvals(&other,&Peer::approval);QVERIFY(other.listen(29903,"stop-sync-code"));peer->connectTo("127.0.0.1",29903,"stop-sync-code");QTRY_VERIFY(peer->ready()&&other.ready());
  // Authentication alone is idle, and consent is still a waiting state.
  QCOMPARE(control->state(),CastControl::Start);const quint32 options=remoteShares?FR_APPROVAL_SHARE:FR_APPROVAL_RECEIVE;
  other.sendApproval(3201,FR_APPROVAL_REQUEST,options);QTRY_VERIFY(consent->pending());QCOMPARE(control->state(),CastControl::Waiting);
  w.findChild<QPushButton*>("consentAccept")->click();QTRY_VERIFY(state->occupied());QCOMPARE(control->state(),CastControl::Stop);QVERIFY(state->locked());
  if(remoteShares){QImage image(80,60,QImage::Format_RGB32);image.fill(Qt::blue);other.sendFrame(image);QTRY_VERIFY(view->hasImage());}else{QVERIFY(view->busy());}
  if(remoteStops){other.sendApproval(3201,FR_APPROVAL_CANCEL,options);other.close(true);}else{
   button->click();QTRY_VERIFY([&]{for(const auto &row:approvals)if(row[0].toUInt()==3201&&row[1].toUInt()==FR_APPROVAL_CANCEL&&row[2].toUInt()==options)return true;return false;}());
  }
  QTRY_COMPARE(control->state(),CastControl::Start);QTRY_VERIFY(!peer->ready()&&!other.ready());QVERIFY(!consent->active()&&!consent->pending());QVERIFY(!state->locked()&&!state->occupied());
  QVERIFY(!view->hasImage()&&!view->busy());QVERIFY(w.findChild<QCheckBox*>("share")->isEnabled()&&w.findChild<QCheckBox*>("receive")->isEnabled());QCOMPARE(button->property("castState").toString(),QString("start"));
  if(remoteStops)QVERIFY(w.findChild<QPlainTextEdit*>("log")->toPlainText().contains("stopped casting"));
 }
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

 void nativeWindowAndExclusiveDirection(){
  MainWindow w;w.show();QTest::qWait(30);auto *share=w.findChild<QCheckBox*>("share"),*receive=w.findChild<QCheckBox*>("receive");
  share->click();QVERIFY(share->isChecked()&&!receive->isChecked());share->click();QVERIFY(share->isChecked()&&!receive->isChecked());receive->click();QVERIFY(!share->isChecked()&&receive->isChecked());
  QVERIFY(!w.findChild<QWidget*>("windowControls"));QVERIFY(!w.findChild<QPushButton*>("windowMinimize"));QVERIFY(!w.findChild<QPushButton*>("windowMaximize"));QVERIFY(!w.findChild<QPushButton*>("windowClose"));QVERIFY(!w.windowIcon().isNull());
  QVERIFY(!w.findChild<QLabel*>("castHint"));QVERIFY(!w.findChild<QLabel*>("performanceHint"));
  auto *mode=w.findChild<QFrame*>("modeSeparator"),*resolution=w.findChild<QFrame*>("resolutionSeparator");QVERIFY(mode&&resolution);QVERIFY(resolution->isVisible());
  share->click();QVERIFY(mode->isVisible());w.grab().save("pc-sharing-layout.png");receive->click();QVERIFY(!mode->isVisible()&&resolution->isVisible());
  w.showMaximized();QTRY_VERIFY(w.isMaximized());w.showNormal();QTRY_VERIFY(!w.isMaximized());w.showMinimized();QTRY_VERIFY(w.isMinimized());w.showNormal();
  auto *presentation=w.findChild<Presentation*>();presentation->setActive(true);QTRY_VERIFY(w.isFullScreen());QVERIFY(w.findChild<QPushButton*>("exitFullScreen")->isVisible());presentation->setActive(false);QTRY_VERIFY(!w.isFullScreen());
 }

 void trayKeepsPendingConnection(){
  if(!QSystemTrayIcon::isSystemTrayAvailable())QSKIP("A Windows notification area is required");
  MainWindow w;w.show();auto *tray=w.findChild<DesktopTray*>();QVERIFY(tray&&tray->available());
  // USB forwarding can already own 127.0.0.1:27183 on the developer PC.
  Peer server;QVERIFY(server.listen(27183,"tray-test-code","127.0.0.2"));QSignalSpy approvals(&server,&Peer::approval);
  w.findChild<QLineEdit*>("host")->setText("127.0.0.2");w.findChild<QLineEdit*>("token")->setText("tray-test-code");w.findChild<QTabWidget*>("transport")->setCurrentIndex(1);w.findChild<QCheckBox*>("receive")->click();
  auto *start=w.findChild<QPushButton*>("start");auto *state=w.findChild<CastUiState*>();start->click();QTRY_VERIFY2(server.ready(),qPrintable(w.findChild<QPlainTextEdit*>("log")->toPlainText()));QTRY_VERIFY(state->locked());
  QVERIFY(!w.close());QVERIFY(!w.isVisible());QTest::qWait(100);QVERIFY(server.ready()&&state->locked());
  w.findChild<QAction*>("trayShow")->trigger();QTRY_VERIFY(w.isVisible());QVERIFY(server.ready()&&state->locked());
  QTRY_COMPARE(approvals.size(),1);const auto approval=approvals.first();server.sendApproval(approval[0].toUInt(),FR_APPROVAL_ACCEPT,approval[2].toUInt());auto *consent=w.findChild<SessionConsent*>();QTRY_VERIFY(consent->active());QVERIFY(state->occupied());
  QVERIFY(!w.close());QImage frame(320,180,QImage::Format_RGB32);frame.fill(Qt::darkBlue);server.sendFrame(frame);auto *view=w.findChild<ScreenView*>();QTRY_VERIFY(view->hasImage());QVERIFY(server.ready()&&consent->active()&&state->occupied());tray->restoreWindow();
  auto *presentation=w.findChild<Presentation*>();presentation->setActive(true);QTRY_VERIFY(w.isFullScreen());QVERIFY(!w.close());tray->restoreWindow();QTRY_VERIFY(w.isVisible()&&w.isFullScreen());presentation->setActive(false);
  start->click();QTRY_VERIFY(!server.ready());QVERIFY(!state->locked());
 }

 void singleControlCancelsWaiting(){
  MainWindow w;w.show();auto *button=w.findChild<QPushButton*>("start");auto *control=w.findChild<CastControl*>();QVERIFY(button&&control);QVERIFY(!w.findChild<QPushButton*>("stop"));QVERIFY(button->text().isEmpty()&&!button->icon().isNull());QCOMPARE(button->iconSize(),w.findChild<QPushButton*>("settings")->iconSize());QCOMPARE(control->state(),CastControl::Start);
  QTcpServer transport;QVERIFY(transport.listen(QHostAddress::AnyIPv4,27183));
  w.findChild<QLineEdit*>("host")->setText("127.0.0.2");w.findChild<QLineEdit*>("token")->setText("control-test-code");w.findChild<QTabWidget*>("transport")->setCurrentIndex(1);
  button->click();QCOMPARE(control->state(),CastControl::Waiting);QVERIFY(button->isEnabled());const auto first=button->icon().cacheKey();QTest::qWait(160);QCOMPARE(control->state(),CastControl::Waiting);QVERIFY(first!=button->icon().cacheKey());w.grab().save("control-waiting.png");
  button->click();QCOMPARE(control->state(),CastControl::Start);QVERIFY(w.findChild<QCheckBox*>("share")->isEnabled());
  QTRY_VERIFY(!w.findChild<Peer*>()->ready());
 }

 void connectionSettingsAreSeparated(){
  MainWindow w;w.show();w.findChild<QPushButton*>("settings")->click();auto *dialog=w.findChild<QDialog*>("SettingsDialog");QTRY_VERIFY(dialog->isVisible());
  auto *tabs=w.findChild<QTabWidget*>("transport");QVERIFY(tabs&&tabs->count()==2);QCOMPARE(tabs->tabText(0),QString("USB 有线"));QCOMPARE(tabs->tabText(1),QString("Wi-Fi 无线"));
  auto *usb=w.findChild<QWidget*>("usbPage"),*wifi=w.findChild<QWidget*>("wifiPage");QVERIFY(usb->isAncestorOf(w.findChild<QLineEdit*>("serial")));QVERIFY(usb->isAncestorOf(w.findChild<QLineEdit*>("adbPath")));QVERIFY(wifi->isAncestorOf(w.findChild<QLineEdit*>("host")));QVERIFY(wifi->isAncestorOf(w.findChild<QLineEdit*>("adbEndpoint")));
  w.findChild<QLineEdit*>("host")->setText("127.0.0.2");w.findChild<QLineEdit*>("serial")->setText("usb-example");tabs->setCurrentIndex(1);QCOMPARE(w.findChild<QLineEdit*>("serial")->text(),QString("usb-example"));tabs->setCurrentIndex(0);QCOMPARE(w.findChild<QLineEdit*>("host")->text(),QString("127.0.0.2"));tabs->setCurrentIndex(0);QTest::qWait(30);dialog->grab().save("pc-usb-settings.png");tabs->setCurrentIndex(1);QTest::qWait(30);dialog->grab().save("pc-wifi-settings.png");dialog->reject();
 }
 void invalidConnectionReleasesPendingLock(){
  MainWindow w;w.findChild<QLineEdit*>("host")->setText("invalid-ip");w.findChild<QLineEdit*>("token")->setText("test-code");w.findChild<QTabWidget*>("transport")->setCurrentIndex(1);w.findChild<QPushButton*>("start")->click();
  QVERIFY(!w.findChild<CastUiState*>()->locked());QVERIFY(w.findChild<QCheckBox*>("share")->isEnabled());QVERIFY(w.findChild<QPushButton*>("start")->isEnabled());
 }
 void remoteScreenAndInput(){
  qRegisterMetaType<fr_gesture>();Peer server;QVERIFY(server.listen(27183,"ui-test-code"));
  MainWindow w;w.show();auto *view=w.findChild<ScreenView*>();auto *mode=w.findChild<QCheckBox*>("extend"),*duplicate=w.findChild<QCheckBox*>("duplicate");QVERIFY(!w.findChild<QComboBox*>("castSource")&&!w.findChild<QWidget*>("displayGroup"));
  auto *start=w.findChild<QPushButton*>("start"),*stop=start;auto *share=w.findChild<QCheckBox*>("share"),*receive=w.findChild<QCheckBox*>("receive");auto *state=w.findChild<CastUiState*>();auto *control=w.findChild<CastControl*>();
  duplicate->setChecked(true);QVERIFY(receive->isChecked()&&!share->isChecked());QVERIFY(!w.findChild<QWidget*>("modePanel")->isVisible());
  QCOMPARE(start->width(),40);QCOMPARE(start->width(),start->height());QCOMPARE(share->mapTo(&w,QPoint()).y(),receive->mapTo(&w,QPoint()).y());auto *sidebar=w.findChild<QWidget*>("sidebar");QCOMPARE(sidebar->width(),48);QVERIFY(!view->hasImage()&&!view->busy());
  w.findChild<QPushButton*>("settings")->click();auto *dialog=w.findChild<QDialog*>("SettingsDialog");QTRY_VERIFY(dialog->isVisible());
  w.findChild<QLineEdit*>("host")->setText("127.0.0.2");w.findChild<QLineEdit*>("token")->setText("ui-test-code");w.findChild<QTabWidget*>("transport")->setCurrentIndex(1);dialog->accept();
  auto *client=w.findChild<Peer*>();auto *consent=w.findChild<SessionConsent*>();QSignalSpy connected(client,&Peer::connected),gestures(&server,&Peer::gesture),keys(&server,&Peer::key),sessions(&server,&Peer::session),approvals(&server,&Peer::approval),profiles(&server,&Peer::profile);
  QImage image(1024,600,QImage::Format_RGB32);image.fill(QColor(48,78,118));
  start->click();QTRY_COMPARE(connected.count(),1);QCOMPARE(control->state(),CastControl::Waiting);QTRY_COMPARE(approvals.count(),1);QVERIFY(state->locked()&&!state->occupied());QVERIFY(!consent->active());QVERIFY(!actionSeen(sessions,FR_START_SHARE));
  server.sendSession(FR_START_SHARE);server.sendFrame(image);QTest::qWait(40);QVERIFY(!view->hasImage());server.sendApproval(approvals[0][0].toUInt(),FR_APPROVAL_REJECT,approvals[0][2].toUInt());QTRY_VERIFY(!state->locked());QVERIFY(share->isEnabled());
  const auto grant=[&](){const auto count=requestCount(approvals);if(control->state()!=CastControl::Start){stop->click();QTRY_VERIFY(!server.ready());}start->click();QTRY_COMPARE(requestCount(approvals),count+1);const auto row=approvals.last();QCOMPARE(row[1].toUInt(),quint32(FR_APPROVAL_REQUEST));server.sendApproval(row[0].toUInt(),FR_APPROVAL_ACCEPT,row[2].toUInt());QTRY_VERIFY(consent->active());QVERIFY(state->occupied());};
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
  if(qEnvironmentVariableIsSet("FURINA_TEST_MULTI_SCREEN")){QCOMPARE(QGuiApplication::screens().size(),3);QTRY_VERIFY(w.findChild<QPlainTextEdit*>("log")->toPlainText().contains("扩展投屏已启动：extended-test"));}
  else{QCOMPARE(QGuiApplication::screens().size(),1);QTRY_VERIFY(w.findChild<QPlainTextEdit*>("log")->toPlainText().contains("扩展暂不可用"));}
  QVERIFY(start->isEnabled()&&control->state()==CastControl::Stop);QVERIFY(state->locked());stop->click();QTRY_VERIFY(!server.ready());duplicate->setChecked(true);receive->click();grant();server.sendFrame(image);QTRY_VERIFY(view->hasImage());
  server.close();QTRY_VERIFY(!client->ready());QVERIFY(state->occupied()&&(start->isEnabled()&&control->state()==CastControl::Stop)&&!share->isEnabled());stop->click();QVERIFY(start->isEnabled()&&share->isEnabled()&&!state->locked()&&!view->busy()&&!view->hasImage());
 }

};
QTEST_MAIN(UiTest)
#include "ui_test.moc"
