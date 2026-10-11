#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "ui_settings.h"
#include <QGuiApplication>
#include <QSettings>
#include <QStandardPaths>
#include <QFileInfo>
#include <QHostAddress>
#include <QSignalBlocker>
#include <QDesktopServices>
#include <QUrl>
#include <QResizeEvent>
#include <QButtonGroup>
MainWindow::MainWindow(QWidget *parent):QMainWindow(parent),ui(new Ui::MainWindow),cfg(new Ui::SettingsDialog),m_settings(new QDialog(this)),m_peer(this),m_adb(this){
 ui->setupUi(this);cfg->setupUi(m_settings);cfg->buttonBox->button(QDialogButtonBox::Save)->setText("保存");cfg->buttonBox->button(QDialogButtonBox::Cancel)->setText("取消");setStyleSheet(furinaStyle());furinaConfigureToolIcons(ui->settings,ui->maximize);setWindowTitle("Fukaros · 电脑端");resize(1180,760);
 m_view=new ScreenView(this);ui->viewLayout->addWidget(m_view);m_view->setInteractive(true);
 m_presentation=new Presentation(this,ui->sidebar,ui->bottomPanel,m_view);
 m_resolution=new ResolutionChoice(this);m_direction=new DirectionChoice(this);m_consent=new SessionConsent(&m_peer,this);m_castUi=new CastUiState(this);m_castControl=new CastControl(this,m_castUi,&m_peer);
 connect(m_consent,&SessionConsent::status,this,[this](const QString &cn,const QString &en){log(cn,en);});
 connect(m_consent,&SessionConsent::incoming,this,[this]{m_castUi->lockPending();m_pending=false;m_presentation->setActive(false);});
 connect(m_consent,&SessionConsent::ended,this,&MainWindow::endSession);
 connect(m_consent,&SessionConsent::approved,this,[this](bool sharing,bool extended,bool initiatedHere){
  m_castUi->occupy(sharing,extended);log("投屏方式已锁定，仅清晰度可调整；点击停止后可修改方式。","Casting options are locked; resolution can still change. Press Stop to change the casting mode.");m_pending=false;m_direction->setSharing(sharing);{const QSignalBlocker a(ui->duplicate),b(ui->extend);(extended?ui->extend:ui->duplicate)->setChecked(true);}
  ui->modePanel->setVisible(sharing);m_sessionActive=true;
  m_view->setInteractive(!sharing);refreshScreens();
  if(initiatedHere)applyDirection();
 });
 m_displaySample.start();
 connect(m_resolution,&ResolutionChoice::changed,this,[this](int edge){m_videoSender.setResolution(edge);m_peer.sendProfile(edge);saveSettings();});
 connect(&m_peer,&Peer::profile,this,[this](int edge){const QSignalBlocker block(m_resolution);m_resolution->setValue(edge);m_videoSender.setResolution(edge);saveSettings();});
 connect(&m_peer,&Peer::feedback,this,[this](double fps,double ms,unsigned queue,double shown){m_videoSender.feedback(qMin(fps,shown),queue,ms);});
 connect(&m_videoSender,&DesktopVideoSender::packet,this,[this](const QByteArray &packet){if(m_sharing&&m_consent->active()&&m_consent->sharing())m_videoSender.accepted(m_peer.sendVideo(packet));});
 connect(&m_videoSender,&DesktopVideoSender::status,this,[this](const QString &text){log(text);});
 connect(&m_videoSender,&DesktopVideoSender::failed,this,[this]{if(!m_sharing)return;stopSharing();m_sessionActive=ui->receive->isChecked();m_peer.sendSession(FR_STOP_RECEIVE);log("屏幕采集未能恢复，请点击停止，检查显示器后重新开始。","Screen capture could not recover. Press Stop, check the monitor, then start again.");});
 m_layoutRefresh.setSingleShot(true);m_layoutRefresh.setInterval(250);connect(&m_layoutRefresh,&QTimer::timeout,this,&MainWindow::rebindCapture);
 connect(&m_videoSender,&DesktopVideoSender::stats,this,[this](double fps,double ms,int edge){m_encodeWorkMs=ms;ui->performanceHint->setText(QString("发送 %1 FPS · %2 · %3 ms").arg(fps,0,'f',1).arg(edge).arg(ms,0,'f',1));log(QString("电脑发送：%1 FPS，编码 %2 ms，最长边 %3，目标 60").arg(fps,0,'f',1).arg(ms,0,'f',1).arg(edge),QString("PC send: %1 FPS, encode %2 ms, edge %3, target 60").arg(fps,0,'f',1).arg(ms,0,'f',1).arg(edge));});
 connect(&m_peer,&Peer::video,this,[this](const QByteArray &packet){if(m_consent->active()&&!m_consent->sharing()&&ui->receive->isChecked())m_videoReceiver.submit(packet);});
 connect(&m_videoReceiver,&DesktopVideoReceiver::frameReady,this,[this]{QImage image;if(m_videoReceiver.takeImage(&image)&&m_consent->active()&&!m_consent->sharing()&&ui->receive->isChecked()){if(!m_view->hasImage()){m_displaySample.restart();m_view->takeDisplayedFrames();}m_view->setImage(image);}});
 connect(&m_videoReceiver,&DesktopVideoReceiver::recover,this,[this]{m_peer.sendSession(FR_REQUEST_KEY);});
 connect(&m_videoReceiver,&DesktopVideoReceiver::status,this,[this](const QString &text){log(text);});
 connect(&m_videoReceiver,&DesktopVideoReceiver::stats,this,[this](double fps,double ms,unsigned queue){const double shown=m_view->takeDisplayedFrames()*1000.0/qMax<qint64>(1,m_displaySample.restart());m_videoSender.setReceiverWork(m_sharing?ms:0);m_peer.sendFeedback(fps,ms+(m_sharing?m_encodeWorkMs:0),queue,shown);log(QString("电脑接收：解码 %1 FPS，显示 %2 FPS，处理 %3 ms").arg(fps,0,'f',1).arg(shown,0,'f',1).arg(ms,0,'f',1),QString("PC receive: decode %1 FPS, display %2 FPS, processing %3 ms").arg(fps,0,'f',1).arg(shown,0,'f',1).arg(ms,0,'f',1));});
 connect(&m_virtual,&VirtualDisplayController::status,this,[this](const QString &cn,const QString &en){log(cn,en);});
 connect(&m_virtual,&VirtualDisplayController::failed,this,[this](const QString &cn,const QString &en){log(cn,en);m_sessionActive=ui->receive->isChecked();m_peer.sendSession(FR_STOP_RECEIVE);refreshScreens();});
 connect(&m_virtual,&VirtualDisplayController::ready,this,[this](const QString &){
  if(!m_peer.ready()||!ui->share->isChecked()||castMode()!=FR_CAST_EXTEND){m_virtual.release();return;}
  refreshScreens();m_sessionActive=startSharing()||ui->receive->isChecked();
 });
 loadSettings();auto *modeGroup=new QButtonGroup(this);modeGroup->addButton(ui->duplicate);modeGroup->addButton(ui->extend);modeGroup->setExclusive(true);QSettings s;if(s.value("castMode",0).toInt()==1)ui->extend->setChecked(true);
 connect(m_castControl,&CastControl::startRequested,this,&MainWindow::start);
 connect(m_castControl,&CastControl::stopRequested,this,&MainWindow::stop);
 connect(ui->settings,&QPushButton::clicked,this,&MainWindow::openSettings);
 connect(ui->maximize,&QPushButton::clicked,this,&MainWindow::maximize);
 connect(m_presentation,&Presentation::changed,this,[this](bool on){furinaSetMaximizeIcon(ui->maximize,on||m_remoteMax);});
 connect(m_direction,&DirectionChoice::changed,this,&MainWindow::directionChanged);
 new WindowControls(this,m_presentation,m_view,ui->windowControls);
 connect(m_settings,&QDialog::accepted,this,[this]{if(m_castUi->locked()){loadSettings();log("投屏方式已锁定，请点击停止后修改设置。","Casting options are locked. Press Stop to change settings.");return;}stop();saveSettings();log("设置已保存，请按开始重新连接。","Settings saved. Press Start to reconnect.");});
 connect(m_settings,&QDialog::rejected,this,&MainWindow::loadSettings);
 connect(&m_peer,&Peer::status,this,[this](const QString &message){log(message);});
 connect(&m_adb,&Adb::status,this,[this](const QString &message){log(message);});
 connect(&m_peer,&Peer::frame,this,[this](const QImage &image){if(m_consent->active()&&!m_consent->sharing()&&ui->receive->isChecked())m_view->setImage(image);});
 connect(m_view,&ScreenView::gesture,this,&MainWindow::gesture);
 connect(&m_peer,&Peer::session,this,&MainWindow::session);
 connect(&m_peer,&Peer::connected,this,[this]{if(m_pending){m_pending=false;requestDirection();}});
 connect(&m_peer,&Peer::disconnected,this,[this]{m_castUi->connectionLost();if(m_castUi->occupied())log("连接已中断，请点击停止释放投屏方式。","Connection lost. Press Stop to unlock casting options.");stopSharing();m_videoReceiver.reset();m_pending=false;m_sessionActive=false;m_remoteMax=false;furinaSetMaximizeIcon(ui->maximize,false);m_view->setImage({});m_presentation->setActive(false);});
 connect(cfg->refresh,&QPushButton::clicked,this,[this]{configureAdb();m_adb.run({"devices","-l"},[this](bool ok,QByteArray out){if(!ok)return;log("USB 设备列表："+QString::fromUtf8(out),"USB device list: "+QString::fromUtf8(out));QStringList found;for(auto line:out.split('\n')){auto parts=QString::fromUtf8(line).simplified().split(' ');if(parts.size()>=2&&parts[1]=="device")found<<parts[0];}if(found.size()==1)cfg->serial->setText(found[0]);},false);});
 connect(cfg->adbPreview,&QPushButton::clicked,this,&MainWindow::preview);
 connect(cfg->adbConnect,&QPushButton::clicked,this,[this]{configureAdb();m_adb.run({"connect",cfg->adbEndpoint->text().trimmed()},[this](bool,QByteArray out){log("无线 ADB："+QString::fromUtf8(out),"Wireless ADB: "+QString::fromUtf8(out));},false);});
 connect(cfg->back,&QPushButton::clicked,this,[this]{key(4);});connect(cfg->home,&QPushButton::clicked,this,[this]{key(3);});connect(cfg->recent,&QPushButton::clicked,this,[this]{key(187);});
 auto modeChanged=[this](bool checked){if(m_castUi->locked()){m_castUi->preserveSelection();return;}if(!checked)return;if(m_sessionActive||m_consent->pending())stop();refreshScreens();};
 connect(ui->duplicate,&QCheckBox::toggled,this,modeChanged);connect(ui->extend,&QCheckBox::toggled,this,modeChanged);
 connect(cfg->castSource,&QComboBox::currentIndexChanged,this,[this]{if(m_castUi->locked())return;if(m_sessionActive||m_consent->pending()){m_consent->cancel();endSession();}});
 connect(cfg->refreshScreens,&QPushButton::clicked,this,&MainWindow::refreshScreens);
 connect(cfg->displaySettings,&QPushButton::clicked,this,[this]{if(!QDesktopServices::openUrl(QUrl("ms-settings:display")))log("请在系统设置中打开‘系统 → 显示’。","Open System > Display in Windows Settings.");});
 connect(qApp,&QGuiApplication::screenAdded,this,[this](QScreen *screen){observeScreen(screen);displayChanged();});
 connect(qApp,&QGuiApplication::screenRemoved,this,&MainWindow::displayChanged);
 connect(qApp,&QGuiApplication::primaryScreenChanged,this,&MainWindow::displayChanged);
 for(auto *screen:QGuiApplication::screens())observeScreen(screen);refreshScreens();directionChanged();
 m_capture.setTimerType(Qt::PreciseTimer);m_capture.setInterval(16);connect(&m_capture,&QTimer::timeout,this,[this]{
  if(!usableScreen(m_captureScreen)){stopSharing();m_sessionActive=false;log("投屏显示器不可用，共享已停止。","Capture display is unavailable. Sharing stopped.");refreshScreens();return;}
  if(m_peer.canSendFrame()){const auto image=m_captureScreen->grabWindow(0).toImage();m_peer.sendFrame(image);}
 });
 m_preview.setInterval(500);connect(&m_preview,&QTimer::timeout,this,[this]{if(m_screenshot.state()==QProcess::NotRunning)m_screenshot.start(m_adb.program,{"-s",m_adb.serial,"exec-out","screencap","-p"});});
 m_screenshotTimeout.setSingleShot(true);m_screenshotTimeout.setInterval(10000);
 connect(&m_screenshot,&QProcess::started,&m_screenshotTimeout,qOverload<>(&QTimer::start));
 connect(&m_screenshotTimeout,&QTimer::timeout,this,[this]{log("ADB 截图超时","ADB screenshot timed out");m_screenshot.kill();});
 connect(&m_screenshot,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[this](int code,QProcess::ExitStatus exit){
  m_screenshotTimeout.stop();const auto out=m_screenshot.readAllStandardOutput();const auto error=m_screenshot.readAllStandardError();if(!m_adbOnly)return;
  if(code||exit!=QProcess::NormalExit){log("ADB 截图失败："+QString::fromUtf8(error),"ADB screenshot failed: "+QString::fromUtf8(error));m_preview.stop();return;}
  const QImage image=QImage::fromData(out,"PNG");if(!image.isNull()){m_deviceSize=image.size();m_view->setImage(image);}
 });
 connect(&m_screenshot,&QProcess::errorOccurred,this,[this](QProcess::ProcessError){log("ADB 启动失败："+m_screenshot.errorString(),"ADB could not start: "+m_screenshot.errorString());m_preview.stop();m_screenshotTimeout.stop();});
 log("等待投屏。请在设置中填写连接信息，再选择方向并按开始。","Waiting for a stream. Configure connection settings, choose a direction, then press Start.");
}
MainWindow::~MainWindow(){stop();saveSettings();disconnect(&m_virtual,nullptr,this,nullptr);delete cfg;delete ui;}
void MainWindow::log(const QString &message,const QString &english){const auto entry=furinaLog(message,english);if(!entry.isEmpty())ui->log->appendPlainText(entry);}
void MainWindow::loadSettings(){
 QSettings s;QString adb=s.value("adb",QStandardPaths::findExecutable("adb")).toString();
 if(adb.isEmpty()){
  const QString bundled=QCoreApplication::applicationDirPath()+"/adb/adb.exe";
  if(QFileInfo::exists(bundled))adb=bundled;
 }
 cfg->adbPath->setText(adb);cfg->serial->setText(s.value("serial").toString());cfg->host->setText(s.value("host",QString()).toString());cfg->token->setText(s.value("token").toString());
 {const QSignalBlocker block(m_resolution);m_resolution->setValue(s.value("resolution",0).toInt());}m_videoSender.setResolution(m_resolution->value());
 cfg->transport->setCurrentIndex(s.value("transport",0).toInt()==1?1:0);cfg->adbEndpoint->setText(s.value("adbEndpoint").toString());
}
void MainWindow::saveSettings(){QSettings s;s.setValue("resolution",m_resolution->value());s.setValue("adb",cfg->adbPath->text());s.setValue("serial",cfg->serial->text());s.setValue("host",cfg->host->text());s.setValue("token",cfg->token->text());s.setValue("transport",cfg->transport->currentIndex());s.setValue("adbEndpoint",cfg->adbEndpoint->text());s.setValue("castMode",ui->extend->isChecked()?1:0);}
void MainWindow::openSettings(){const auto size=screen()->availableGeometry().size();m_settings->resize(qMin(560,size.width()-24),qMin(690,size.height()-24));m_settings->open();}
void MainWindow::configureAdb(){m_adb.program=cfg->adbPath->text().trimmed();m_adb.serial=cfg->serial->text().trimmed();}
void MainWindow::start(){
 if(m_castUi->locked())return;
 if(!ui->share->isChecked()&&!ui->receive->isChecked()){log("请先勾选分享或获取屏幕。","Select Share or Receive first.");return;}
 if(m_peer.ready()){requestDirection();return;}
 if(m_pending)return;
 if(cfg->token->text().size()<6){log("请在设置中填写至少 6 位的配对码。","Enter a pairing code with at least 6 characters in Settings.");openSettings();return;}
 stop();m_pending=true;m_castUi->lockPending();log("开始等待对端同意。","Starting: waiting for the other device to approve.");saveSettings();const int generation=m_generation;
 if(cfg->transport->currentIndex()==1){
  QHostAddress address;if(!address.setAddress(cfg->host->text().trimmed())||address.protocol()!=QAbstractSocket::IPv4Protocol){m_pending=false;m_castUi->connectionLost();log("平板 IP 无效，请检查设置。","Invalid tablet IP. Check Settings.");return;}
  log("正在通过 Wi-Fi 连接平板。","Connecting to the tablet over Wi-Fi.");m_peer.connectTo(cfg->host->text().trimmed(),27183,cfg->token->text());
 }else{
  configureAdb();if(m_adb.program.isEmpty()){m_pending=false;m_castUi->connectionLost();log("未找到 ADB，请在线下方设置路径。","ADB was not found. Set its path below the separator in Settings.");return;}
  if(!m_adb.serial.isEmpty()){connectUsb(generation);return;}
  m_adb.run({"devices","-l"},[this,generation](bool ok,QByteArray out){
   if(generation!=m_generation)return;QStringList found;for(const auto &line:out.split('\n')){auto parts=QString::fromUtf8(line).simplified().split(' ');if(parts.size()>=2&&parts[1]=="device")found<<parts[0];}
   if(ok&&found.size()==1){cfg->serial->setText(found[0]);configureAdb();connectUsb(generation);}else{m_pending=false;m_castUi->connectionLost();log("请授权 USB 调试，并在设置中选择设备码。","Authorize USB debugging and select a device serial in Settings.");}
  },false);
 }
}
void MainWindow::connectUsb(int generation){m_adb.run({"forward","tcp:27183","tcp:27183"},[this,generation](bool ok,QByteArray){if(generation!=m_generation)return;if(ok)m_peer.connectTo("127.0.0.1",27183,cfg->token->text());else{m_pending=false;m_castUi->connectionLost();}},true);}
void MainWindow::stopSharing(){if(m_sharing)m_peer.sendStatus(furinaStatus("电脑屏幕共享已停止","PC screen sharing stopped"));m_sharing=false;m_layoutRefresh.stop();m_capture.stop();m_videoSender.stop();m_captureScreen.clear();m_captureName.clear();m_virtual.release();}
void MainWindow::stop(){m_castUi->reset();
 m_consent->cancel();++m_generation;m_peer.sendSession(FR_STOP_SHARE);m_peer.sendSession(FR_STOP_RECEIVE);m_peer.sendSession(FR_RESTORE);stopSharing();m_pending=false;m_sessionActive=false;m_adbOnly=false;m_remoteMax=false;m_deviceSize={};
 m_preview.stop();m_screenshotTimeout.stop();m_screenshot.kill();m_screenshot.waitForFinished(1000);m_adb.cancel();m_peer.close();m_presentation->setActive(false);m_videoReceiver.reset();m_view->setImage({});furinaSetMaximizeIcon(ui->maximize,false);
}
void MainWindow::directionChanged(){
 if(m_castUi->locked()){m_castUi->preserveSelection();return;}
 ui->modePanel->setVisible(ui->share->isChecked());m_view->setInteractive(ui->receive->isChecked());
 if(m_sessionActive||m_pending||m_consent->pending())stop();stopSharing();m_preview.stop();m_adbOnly=false;m_view->setImage({});m_presentation->setActive(false);
 if(m_remoteMax){m_peer.sendSession(FR_RESTORE);m_remoteMax=false;furinaSetMaximizeIcon(ui->maximize,false);}
 m_sessionActive=false;
}
void MainWindow::endSession(){m_castUi->reset();stopSharing();m_videoReceiver.reset();m_view->setImage({});m_presentation->setActive(false);m_pending=false;m_sessionActive=false;m_remoteMax=false;furinaSetMaximizeIcon(ui->maximize,false);}
void MainWindow::requestDirection(){
 if(m_castUi->occupied())return;
 if(m_consent->request(m_direction->sharing(),ui->extend->isChecked())){m_pending=false;m_castUi->lockPending();}
}
void MainWindow::applyDirection(){
 if(!m_consent->active())return;
 m_sessionActive=false;m_peer.sendProfile(m_resolution->value());
 m_peer.sendSession(ui->share->isChecked()?FR_START_RECEIVE:FR_STOP_RECEIVE);
 if(ui->share->isChecked())m_sessionActive=startSharing();else stopSharing();
 m_peer.sendSession(ui->receive->isChecked()?FR_START_SHARE:FR_STOP_SHARE);
 if(ui->receive->isChecked()){m_sessionActive=true;log("已请求平板屏幕，请在平板确认录屏授权。","Requested tablet screen. Confirm screen capture permission on the tablet.");}

}
fr_cast_mode MainWindow::castMode() const{return ui->extend->isChecked()?FR_CAST_EXTEND:FR_CAST_DUPLICATE;}
bool MainWindow::usableScreen(QScreen *screen) const{
 if(!screen||!QGuiApplication::screens().contains(screen))return false;
 auto display=[](QScreen *s){const auto r=s->geometry();return fr_cast_display{r.x(),r.y(),r.width(),r.height(),s==QGuiApplication::primaryScreen()};};
 const auto d=display(screen);auto *primary=QGuiApplication::primaryScreen();const auto p=primary?display(primary):fr_cast_display{};
 return fr_cast_source_usable(castMode(),&d,primary?&p:nullptr);
}
QScreen *MainWindow::selectedScreen() const{const int index=cfg->castSource->currentIndex();if(index<0||index>=m_sources.size())return nullptr;auto *screen=m_sources[index].data();return usableScreen(screen)?screen:nullptr;}
void MainWindow::observeScreen(QScreen *screen){connect(screen,&QScreen::geometryChanged,this,&MainWindow::displayChanged,Qt::UniqueConnection);}
void MainWindow::displayChanged(){refreshScreens();if(m_sharing&&QGuiApplication::platformName()!="offscreen")m_layoutRefresh.start();}
void MainWindow::rebindCapture(){
 if(!m_sharing||!m_peer.ready())return;
 // Qt can replace QScreen objects during one Windows display-change transaction.
 // Keep the monitor identity and the peer session, rather than detaching the virtual screen.
 for(auto *screen:QGuiApplication::screens())if(screen->name()==m_captureName&&usableScreen(screen)){m_captureScreen=screen;break;}
 const QString device=castMode()==FR_CAST_EXTEND?m_virtual.deviceName():m_captureName;
 if(!device.isEmpty())m_videoSender.refreshDisplay(device);
 log("显示器布局已变更，正在重新绑定采集并恢复窗口画面。","Display layout changed. Rebinding capture and restoring live windows.");
}
void MainWindow::refreshScreens(){
 const int oldIndex=cfg->castSource->currentIndex();QPointer<QScreen> previous=oldIndex>=0&&oldIndex<m_sources.size()?m_sources[oldIndex]:QPointer<QScreen>{};
 const QSignalBlocker blocker(cfg->castSource);cfg->castSource->clear();m_sources.clear();
 for(auto *screen:QGuiApplication::screens())if(usableScreen(screen)){const auto size=screen->size()*screen->devicePixelRatio();cfg->castSource->addItem(QString("%1%2 · %3×%4").arg(screen->name(),screen==QGuiApplication::primaryScreen()?"（主屏）":"",QString::number(size.width()),QString::number(size.height())));m_sources.append(screen);}
 if(m_sources.isEmpty())cfg->castSource->addItem("没有可用的显示器");else{int index=m_sources.indexOf(previous);for(int n=0;n<m_sources.size();++n)if(m_sharing&&m_sources[n]&&m_sources[n]->name()==m_captureName){index=n;m_captureScreen=m_sources[n];}if(castMode()==FR_CAST_EXTEND&&!m_virtual.screenName().isEmpty()){for(int n=0;n<m_sources.size();++n)if(m_sources[n]&&m_sources[n]->name()==m_virtual.screenName())index=n;}if(index<0&&castMode()==FR_CAST_DUPLICATE)index=m_sources.indexOf(QGuiApplication::primaryScreen());cfg->castSource->setCurrentIndex(index<0?0:index);}
 cfg->castSource->setEnabled(!m_sources.isEmpty());
 if(m_sharing&&!m_captureScreen&&QGuiApplication::platformName()!="offscreen")m_layoutRefresh.start();
 ui->castHint->setText(castMode()==FR_CAST_DUPLICATE?"与所选电脑屏幕显示相同内容。":m_sources.isEmpty()?"按开始创建平板扩展桌面（首次需允许驱动安装）。":"平板可显示独立桌面，窗口可从主屏拖入。");
}
bool MainWindow::startSharing(){
 if(!m_peer.ready()||!m_consent->active()||!m_consent->sharing())return false;
 if(castMode()==FR_CAST_EXTEND&&QGuiApplication::platformName()!="offscreen"){
  // Prefer a dedicated tablet desktop, including when physical monitors exist.
  if(m_virtual.screenName().isEmpty()){
   m_virtual.ensure();return m_virtual.preparing()||m_sharing;
  }
  if(m_virtual.preparing())return true;
  refreshScreens();
 }
 auto *source=selectedScreen();
 if(!source){log("扩展暂不可用：请先启用第二块或虚拟显示器。","Extend is unavailable. Enable a second or virtual display first.");return false;}
 m_sharing=true;m_captureScreen=source;m_captureName=source->name();if(QGuiApplication::platformName()=="offscreen")m_capture.start();else{m_videoSender.setResolution(m_resolution->value());const QString device=castMode()==FR_CAST_EXTEND?m_virtual.deviceName():source->name();m_videoSender.start(device,source->geometry());}
 const auto cn=QString("电脑%1投屏已启动：%2").arg(castMode()==FR_CAST_EXTEND?"扩展":"复制",source->name());
 const auto en=QString("PC %1 sharing started: %2").arg(castMode()==FR_CAST_EXTEND?"extended desktop":"duplicate screen",source->name());
 log(cn,en);m_peer.sendSession(castMode()==FR_CAST_EXTEND?FR_EXTEND:FR_DUPLICATE);m_peer.sendStatus(furinaStatus(cn,en));return true;
}
void MainWindow::session(quint32 action){
 if(action==FR_REQUEST_KEY){if(m_sharing)m_videoSender.requestKeyframe();}
 else if(action==FR_START_SHARE){if(!m_consent->active()||!m_consent->sharing())return;m_pending=false;m_sessionActive=startSharing();}
 else if(action==FR_STOP_SHARE){stopSharing();}
 else if(action==FR_START_RECEIVE){if(!m_consent->active()||m_consent->sharing())return;m_pending=false;m_sessionActive=true;}
 else if(action==FR_STOP_RECEIVE){m_videoReceiver.reset();m_view->setImage({});}
 else if(action==FR_DUPLICATE||action==FR_EXTEND){if(m_castUi->occupied()&&ui->extend->isChecked()!=(action==FR_EXTEND)){log("投屏方式已锁定，忽略会话中的模式更改。","Casting options are locked. Ignoring an in-session mode change.");return;}if(!m_consent->active()||!m_consent->sharing())return;const QSignalBlocker a(ui->duplicate),b(ui->extend);(action==FR_EXTEND?ui->extend:ui->duplicate)->setChecked(true);refreshScreens();}
 else if(action==FR_MAXIMIZE||action==FR_RESTORE){if((m_consent->active()&&!m_consent->sharing())||action==FR_RESTORE)m_presentation->setActive(action==FR_MAXIMIZE);}
}
void MainWindow::maximize(){if(ui->share->isChecked()){if(!m_peer.ready()){log("请先连接平板，再放大接收端画面。","Connect the tablet before maximizing its received content.");return;}m_remoteMax=!m_remoteMax;m_peer.sendSession(m_remoteMax?FR_MAXIMIZE:FR_RESTORE);furinaSetMaximizeIcon(ui->maximize,m_remoteMax);log(m_remoteMax?"已请求平板全屏显示。":"已请求平板退出全屏。",m_remoteMax?"Requested full-screen viewing on the tablet.":"Requested leaving full-screen viewing on the tablet.");}else m_presentation->setActive(!m_presentation->active());}
void MainWindow::preview(){if(m_castUi->locked())return;stop();{const QSignalBlocker a(ui->share),b(ui->receive);ui->share->setChecked(false);ui->receive->setChecked(true);}ui->modePanel->hide();m_view->setInteractive(true);configureAdb();if(m_adb.serial.isEmpty()){log("请先刷新 USB 设备码。","Refresh the USB device serial first.");return;}m_adbOnly=true;m_castUi->lockPending();m_castUi->occupy(false,false);m_preview.start();log("ADB 预览启动，每 500ms 请求一帧。","ADB preview started, requesting a frame every 500 ms.");}
void MainWindow::gesture(fr_gesture g){if(!ui->receive->isChecked())return;if(!m_adbOnly){m_peer.sendGesture(g);return;}if(!m_deviceSize.isValid())return;auto x=[this](uint32_t n){return QString::number(qRound(n/65535.0*(m_deviceSize.width()-1)));};auto y=[this](uint32_t n){return QString::number(qRound(n/65535.0*(m_deviceSize.height()-1)));};const bool tap=qAbs(int(g.x1)-int(g.x2))<350&&qAbs(int(g.y1)-int(g.y2))<350&&g.duration_ms<450;if(tap)m_adb.run({"shell","input","tap",x(g.x1),y(g.y1)});else m_adb.run({"shell","input","swipe",x(g.x1),y(g.y1),x(g.x2),y(g.y2),QString::number(g.duration_ms)});}
void MainWindow::key(quint32 k){if(m_adbOnly)m_adb.run({"shell","input","keyevent",QString::number(k)});else m_peer.sendKey(k);}
void MainWindow::resizeEvent(QResizeEvent *event){QMainWindow::resizeEvent(event);if(!ui->bottomPanel)return;const bool landscape=width()>=height();ui->bottomLayout->setDirection(landscape?QBoxLayout::LeftToRight:QBoxLayout::TopToBottom);ui->bottomPanel->setMinimumHeight(landscape?220:310);ui->bottomPanel->setMaximumHeight(landscape?220:310);}
