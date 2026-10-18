#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "ui_settings.h"
#include "settings_support.h"
#include <QSettings>
#include <QRandomGenerator>
#include <QNetworkInterface>
#include <QGuiApplication>
#include <QScreen>
#include <QSignalBlocker>
#include <QResizeEvent>
#include <QSysInfo>
#include <QButtonGroup>
MainWindow::MainWindow(QWidget *parent):QMainWindow(parent),ui(new Ui::MainWindow),cfg(new Ui::SettingsDialog),m_settings(new QDialog(this)),m_peer(this),m_discovery(this),m_android(this){
 ui->setupUi(this);cfg->setupUi(m_settings);furinaConfigureSettingsScroll(cfg->settingsScroll);cfg->buttonBox->button(QDialogButtonBox::Save)->setText("保存");cfg->buttonBox->button(QDialogButtonBox::Cancel)->setText("取消");setStyleSheet(furinaStyle());furinaConfigureToolIcons(ui->settings,ui->maximize);setWindowIcon(QIcon(":/icons/app.png"));setWindowTitle("Furina · 平板端");resize(1100,680);
 m_view=new ScreenView(this);ui->viewLayout->addWidget(m_view);m_presentation=new Presentation(this,ui->sidebar,ui->bottomPanel,m_view);m_resolution=new ResolutionChoice(this);
m_direction=new DirectionChoice(this);m_consent=new SessionConsent(&m_peer,this);m_presentationSync=new PresentationSync(&m_peer,m_presentation,m_consent,this);connect(m_presentationSync,&PresentationSync::remoteChanged,this,[this](bool on){m_remoteMax=on;furinaSetMaximizeIcon(ui->maximize,on||m_presentation->active());});connect(m_presentationSync,&PresentationSync::status,this,[this](const QString &cn,const QString &en){log(cn,en);});m_consent->setReceiverOrientation([this]{return width()<height();});m_castUi=new CastUiState(this);m_castControl=new CastControl(this,m_castUi,&m_peer);m_layout=new MobileLayout(this);
 connect(m_consent,&SessionConsent::status,this,[this](const QString &cn,const QString &en){log(cn,en);});
 connect(m_consent,&SessionConsent::incoming,this,[this]{m_castUi->lockPending();m_pending=false;m_presentation->setActive(false);});
 connect(m_consent,&SessionConsent::ended,this,&MainWindow::endSession);
 connect(m_consent,&SessionConsent::approved,this,[this](bool sharing,bool extended,bool initiatedHere){
  m_android.keepSession(true);
  m_castUi->occupy(sharing,extended);log("投屏方式已锁定，清晰度与通用设置可调整；点击停止后可修改方式。","Casting options are locked; resolution and general settings can still change. Press Stop to change the casting mode.");m_pending=false;m_direction->setSharing(sharing);{const QSignalBlocker a(ui->duplicate),b(ui->extend);(extended?ui->extend:ui->duplicate)->setChecked(true);}
  ui->modePanel->setVisible(!sharing);m_sessionActive=true;
  if(!sharing&&extended){
   m_android.setExtensionOrientation(m_consent->portrait()?1:0);
   log(m_consent->portrait()?"竖屏扩展已锁定；点击停止后恢复旋转。":"横屏扩展已锁定；点击停止后恢复旋转。",m_consent->portrait()?"Portrait extension locked. Press Stop to restore rotation.":"Landscape extension locked. Press Stop to restore rotation.");
  }
  m_layout->refresh(m_presentation->active());updateVideoGeometry();
  if(initiatedHere)applyDirection();
 });
 fr_video_policy_init(&m_videoPolicy,0);
 connect(m_resolution,&ResolutionChoice::changed,this,[this](int edge){fr_video_policy_init(&m_videoPolicy,edge);m_android.setCaptureResolution(m_videoPolicy.edge);m_peer.sendProfile(edge);saveSettings();});
 connect(&m_peer,&Peer::profile,this,[this](int edge){const QSignalBlocker block(m_resolution);m_resolution->setValue(edge);fr_video_policy_init(&m_videoPolicy,edge);m_android.setCaptureResolution(m_videoPolicy.edge);saveSettings();});
 connect(&m_peer,&Peer::feedback,this,[this](double fps,double ms,unsigned queue,double shown){m_remoteWorkMs=ms;m_remoteFps=qMin(fps,shown);m_remoteQueue=queue;});
 connect(&m_android,&AndroidBridge::video,this,[this](const QByteArray &packet){if(m_sharing&&m_consent->active()&&m_consent->sharing()&&!m_peer.sendVideo(packet))m_android.requestKeyframe();});
 connect(&m_peer,&Peer::video,this,[this](const QByteArray &packet){if(!m_consent->active()||m_consent->sharing()||!ui->receive->isChecked())return;m_videoVisible=true;m_sessionActive=true;
#ifdef Q_OS_ANDROID
 m_android.receiveVideo(packet);updateVideoGeometry();
#else
 m_videoReceiver.submit(packet);
#endif
 });
 connect(&m_android,&AndroidBridge::needKey,this,[this]{m_peer.sendSession(FR_REQUEST_KEY);});
 connect(&m_android,&AndroidBridge::exitFullscreen,this,[this]{m_presentation->setActive(false);updateVideoGeometry();});
 connect(&m_android,&AndroidBridge::stopRequested,this,&MainWindow::stop);
 connect(&m_android,&AndroidBridge::backRequested,this,[this]{
  if(m_settings->isVisible()){m_settings->reject();return;}
  if(m_presentation->active()){m_presentation->setActive(false);updateVideoGeometry();return;}
  saveSettings();m_android.backgroundTask();
 });
 connect(&m_android,&AndroidBridge::videoStats,this,[this](bool sender,double fps,double ms,unsigned queue,double shown,int w,int h,unsigned sampleMs){
  if(sender)log(QString("平板编码：帧率 %1 帧/秒，图形处理耗时 %2 毫秒，分辨率 %3×%4 像素，目标帧率 60 帧/秒").arg(fps,0,'f',1).arg(ms,0,'f',1).arg(w).arg(h),QString("Tablet encode: %1 FPS, GPU processing %2 ms, %3x%4, target 60").arg(fps,0,'f',1).arg(ms,0,'f',1).arg(w).arg(h));
  else log(QString("平板解码：解码帧率 %1 帧/秒，显示帧率 %2 帧/秒，分辨率 %3×%4 像素，目标帧率 60 帧/秒").arg(fps,0,'f',1).arg(shown,0,'f',1).arg(w).arg(h),QString("Tablet decode: %1 FPS, display %2 FPS, %3x%4, target 60").arg(fps,0,'f',1).arg(shown,0,'f',1).arg(w).arg(h));
  if(sender){m_encodeWorkMs=ms;if(fr_video_policy_sample_elapsed(&m_videoPolicy,qMin(fps,m_remoteFps),qMax(ms+(ui->receive->isChecked()?m_decodeWorkMs:0),m_remoteWorkMs),qMax(m_peer.videoQueueMs(),m_remoteQueue),sampleMs)){m_android.setCaptureResolution(m_videoPolicy.edge);log(QString("自动清晰度：最长边 %1").arg(m_videoPolicy.edge),QString("Auto resolution: max edge %1").arg(m_videoPolicy.edge));}}else {m_decodeWorkMs=ms;m_peer.sendFeedback(fps,ms+(m_sharing?m_encodeWorkMs:0),queue,shown);}
 });
 connect(&m_videoReceiver,&DesktopVideoReceiver::frameReady,this,[this]{QImage image;if(m_videoReceiver.takeImage(&image)&&m_consent->active()&&!m_consent->sharing()&&ui->receive->isChecked())m_view->setImage(image);});
 connect(&m_videoReceiver,&DesktopVideoReceiver::recover,this,[this]{m_peer.sendSession(FR_REQUEST_KEY);});
 connect(&m_videoReceiver,&DesktopVideoReceiver::status,this,[this](const QString &text){log(text);});
#ifdef Q_OS_ANDROID
 // Render settings on the existing activity surface rather than a secondary native window.
 m_settings->setParent(ui->contentPanel,Qt::Widget);m_settings->hide();
 connect(m_settings,&QDialog::finished,this,[this]{ui->sidebar->setEnabled(true);ui->portraitToolbar->setEnabled(true);ui->portraitTabs->setEnabled(true);updateVideoGeometry();ui->viewPanel->setEnabled(true);ui->bottomPanel->setEnabled(true);});
#endif
 connect(qApp,&QGuiApplication::applicationStateChanged,this,[this](Qt::ApplicationState state){
  setUpdatesEnabled(state!=Qt::ApplicationSuspended);
  if(state==Qt::ApplicationActive){m_nativeRect={};updateVideoGeometry();if(m_consent->active()&&!m_consent->sharing()&&m_peer.ready())m_peer.sendSession(FR_REQUEST_KEY);}
 });
 QSettings s;if(s.value("token").toString().size()<6)s.setValue("token",QString::number(QRandomGenerator::system()->bounded(100000000,999999999)));
 loadSettings();
 new PointerSettings(&m_peer,this,false);
 connect(&m_peer,&Peer::cursor,this,[this](const QByteArray &data){if(m_consent->active()&&!m_consent->sharing())m_android.receiveCursor(data);});
 auto *modeGroup=new QButtonGroup(this);modeGroup->addButton(ui->duplicate);modeGroup->addButton(ui->extend);modeGroup->setExclusive(true);if(s.value("castMode",0).toInt()==1)ui->extend->setChecked(true);
 connect(m_castControl,&CastControl::startRequested,this,&MainWindow::start);connect(m_castControl,&CastControl::stopRequested,this,&MainWindow::stop);
 connect(ui->settings,&QPushButton::clicked,this,&MainWindow::openSettings);connect(ui->maximize,&QPushButton::clicked,this,&MainWindow::maximize);
 connect(m_presentation,&Presentation::changed,this,[this](bool on){furinaSetMaximizeIcon(ui->maximize,on||m_remoteMax);m_layout->refresh(on);updateVideoGeometry();});
 connect(m_direction,&DirectionChoice::changed,this,&MainWindow::directionChanged);
 auto modeChanged=[this](bool checked){if(m_castUi->locked()){m_castUi->preserveSelection();return;}if(checked&&(m_sessionActive||m_consent->pending()))stop();};connect(ui->duplicate,&QCheckBox::toggled,this,modeChanged);connect(ui->extend,&QCheckBox::toggled,this,modeChanged);
 connect(m_settings,&QDialog::accepted,this,[this]{if(m_castUi->locked()){loadSettings();log("投屏方式已锁定，请点击停止后修改设置。","Casting options are locked. Press Stop to change settings.");return;}stop();saveSettings();log("设置已保存，请按开始重新启动服务。","Settings saved. Press Start to restart the service.");});
 connect(m_settings,&QDialog::rejected,this,&MainWindow::loadSettings);
 connect(cfg->usbSettings,&QPushButton::clicked,&m_android,&AndroidBridge::usbSettings);
 connect(cfg->refreshAddresses,&QPushButton::clicked,this,&MainWindow::refreshAddresses);
 connect(cfg->accessibility,&QPushButton::clicked,&m_android,&AndroidBridge::accessibility);
 connect(&m_peer,&Peer::status,this,[this](const QString &message){log(message);});
 connect(&m_android,&AndroidBridge::status,this,[this](const QString &message){log(message);m_peer.sendStatus(message);});
 connect(&m_peer,&Peer::frame,this,[this](const QImage &image){if(m_consent->active()&&!m_consent->sharing()&&ui->receive->isChecked()){m_view->setImage(image);m_sessionActive=true;}});
 connect(&m_android,&AndroidBridge::jpeg,this,[this](const QByteArray &jpeg){if(m_sharing&&m_consent->active()&&m_consent->sharing())m_peer.sendJpeg(jpeg);});
 connect(&m_peer,&Peer::session,this,&MainWindow::session);
 connect(&m_peer,&Peer::gesture,this,[this](fr_gesture gesture){if(cfg->allowControl->isChecked())m_android.gesture(gesture);else m_peer.sendStatus(furinaStatus("平板未允许远程控制","Remote control is not allowed on the tablet"));});
 connect(&m_peer,&Peer::key,this,[this](quint32 key){if(cfg->allowControl->isChecked()&&(key==3||key==4||key==187))m_android.key(key);});
 connect(&m_peer,&Peer::connected,this,[this]{
  m_peer.sendStatus(furinaStatus("平板已连接，设备码："+cfg->serial->text(),"Tablet connected, device code: "+cfg->serial->text()));
  // The PC initiates a direction immediately. If it doesn't, honour a pending tablet Start.
  QTimer::singleShot(500,this,[this]{if(m_pending&&m_peer.ready()){m_pending=false;requestDirection();}});
 });
 connect(&m_peer,&Peer::disconnected,this,[this]{m_android.keepSession(false);m_castUi->connectionLost();if(m_castUi->occupied())log("连接已中断，请点击停止释放投屏方式。","Connection lost. Press Stop to unlock casting options.");stopCapture();m_videoVisible=false;m_android.resetVideo();m_videoReceiver.reset();m_pending=false;m_sessionActive=false;m_remoteMax=false;m_presentation->setActive(false);furinaSetMaximizeIcon(ui->maximize,false);m_view->setImage({});});
 connect(&m_android,&AndroidBridge::captureStopped,this,[this]{if(!m_sharing)return;stopCapture();m_sessionActive=false;m_peer.sendSession(FR_STOP_SHARE);});
 m_tick.setTimerType(Qt::PreciseTimer);m_tick.setInterval(16);connect(&m_tick,&QTimer::timeout,this,[this]{m_android.wantFrame=m_sharing&&m_peer.ready();updateVideoGeometry();
#ifndef Q_OS_ANDROID
  if(m_sharing&&m_peer.canSendFrame()){auto *screen=QGuiApplication::primaryScreen();if(screen)m_peer.sendFrame(screen->grabWindow(0).toImage());}
#endif
 });m_tick.start();directionChanged();
 log("等待投屏。按开始启动服务，再由电脑配对连接。","Waiting for a stream. Press Start to run the service, then pair from the PC.");
#ifndef Q_OS_ANDROID
 log("当前是桌面模拟，Android 录屏授权需在平板确认。","Desktop simulation. Android screen capture permission must be confirmed on the tablet.");
#endif
}
MainWindow::~MainWindow(){m_tick.stop();stop();saveSettings();delete cfg;delete ui;}
void MainWindow::log(const QString &message,const QString &english){const auto entry=furinaLog(message,english);if(!entry.isEmpty())ui->log->appendPlainText(entry);}
void MainWindow::loadSettings(){QSettings s;if(!m_castUi->locked()){{const QSignalBlocker block(m_resolution);m_resolution->setValue(s.value("resolution",0).toInt());}fr_video_policy_init(&m_videoPolicy,m_resolution->value());m_android.setCaptureResolution(m_videoPolicy.edge);}cfg->connectionTabs->setCurrentIndex(s.value("connectionTab",0).toInt()==1?1:0);cfg->host->setText(s.value("host","0.0.0.0").toString());cfg->serial->setText(s.value("serial",m_android.deviceCode()).toString());cfg->token->setText(s.value("token").toString());cfg->allowControl->setChecked(s.value("allowControl",false).toBool());}
void MainWindow::saveSettings(){QSettings s;s.setValue("resolution",m_resolution->value());s.setValue("connectionTab",cfg->connectionTabs->currentIndex());s.setValue("host",cfg->host->text());s.setValue("serial",cfg->serial->text());s.setValue("token",cfg->token->text());s.setValue("allowControl",cfg->allowControl->isChecked());s.setValue("castMode",ui->extend->isChecked()?1:0);}
void MainWindow::refreshAddresses(){
 QStringList usb,other;
 for(const auto &interface:QNetworkInterface::allInterfaces()){
  if(!(interface.flags()&QNetworkInterface::IsUp))continue;
  const auto name=interface.name();const bool wired=name.startsWith("rndis")||name.startsWith("usb")||name.startsWith("ncm");
  for(const auto &entry:interface.addressEntries())if(entry.ip().protocol()==QAbstractSocket::IPv4Protocol&&!entry.ip().isLoopback())(wired?usb:other)<<entry.ip().toString();
 }
 cfg->usbAddresses->setText(usb.isEmpty()?"USB IP：未就绪":"USB IP："+usb.join(", "));
 cfg->addresses->setText("本机 IP："+(other.isEmpty()?QString("未连接"):other.join(", ")));
}
void MainWindow::openSettings(){
 refreshAddresses();
#ifdef Q_OS_ANDROID
 const auto size=ui->contentPanel->size();m_settings->resize(qMin(560,size.width()-24),qMin(620,size.height()-24));m_settings->move((size.width()-m_settings->width())/2,(size.height()-m_settings->height())/2);ui->sidebar->setEnabled(false);ui->portraitToolbar->setEnabled(false);ui->portraitTabs->setEnabled(false);ui->viewPanel->setEnabled(false);ui->bottomPanel->setEnabled(false);m_settings->show();m_settings->raise();updateVideoGeometry();
#else
 const auto size=screen()->availableGeometry().size();m_settings->resize(qMin(560,size.width()-24),qMin(620,size.height()-24));m_settings->open();
#endif
}
void MainWindow::start(){
 if(m_castUi->locked())return;
 if(!ui->share->isChecked()&&!ui->receive->isChecked()){log("请先勾选分享或获取屏幕。","Select Share or Receive first.");return;}
 if(m_peer.ready()){m_pending=false;requestDirection();return;}
 if(cfg->token->text().size()<6){log("请填写至少 6 位的配对码。","Enter a pairing code with at least 6 characters.");openSettings();return;}
 if(!m_listening){
  const auto bind=cfg->connectionTabs->currentIndex()==0?QString("0.0.0.0"):cfg->host->text();
  m_listening=m_peer.listen(27183,cfg->token->text(),bind);if(!m_listening)return;
  if(!m_discovery.start(bind))log("自动发现不可用，请在电脑手填平板 USB IP。","Discovery is unavailable. Enter the tablet USB IP on the PC.");
  refreshAddresses();saveSettings();
 }
 m_pending=true;m_castUi->lockPending();log("开始等待对端同意。","Starting: waiting for the other device to approve.");log("服务已启动，等待电脑连接。","Service started. Waiting for the PC connection.");
}
void MainWindow::stop(){m_presentationSync->reset();m_android.keepSession(false);m_android.setExtensionOrientation(-1);m_discovery.stop();m_castUi->reset();m_consent->cancel();m_pending=false;m_sessionActive=false;m_peer.sendSession(FR_STOP_SHARE);m_peer.sendSession(FR_STOP_RECEIVE);m_peer.sendSession(FR_RESTORE);stopCapture();m_peer.close(true);m_listening=false;m_remoteMax=false;m_videoVisible=false;m_android.resetVideo();m_videoReceiver.reset();m_presentation->setActive(false);m_view->setImage({});furinaSetMaximizeIcon(ui->maximize,false);}
void MainWindow::startCapture(){if(!m_peer.ready()||m_sharing||!m_consent->active()||!m_consent->sharing())return;m_sharing=true;m_remoteFps=60;m_remoteQueue=0;m_remoteWorkMs=0;m_encodeWorkMs=0;fr_video_policy_init(&m_videoPolicy,m_resolution->value());m_android.setCaptureResolution(m_videoPolicy.edge);m_android.startCapture();log("正在请求平板录屏授权。","Requesting tablet screen capture permission.");}
void MainWindow::stopCapture(){m_sharing=false;m_android.wantFrame=false;m_android.stopCapture();}
void MainWindow::directionChanged(){
 if(m_castUi->locked()){m_castUi->preserveSelection();return;}
 ui->modePanel->setVisible(ui->receive->isChecked());
 m_videoVisible=false;m_android.resetVideo();m_videoReceiver.reset();m_view->setImage({});m_presentation->setActive(false);if(m_remoteMax){m_peer.sendSession(FR_RESTORE);m_remoteMax=false;furinaSetMaximizeIcon(ui->maximize,false);}
 if(m_sessionActive||m_pending||m_consent->pending())stop();m_sessionActive=false;
}
void MainWindow::endSession(){m_android.keepSession(false);m_android.setExtensionOrientation(-1);m_castUi->reset();stopCapture();m_videoVisible=false;m_android.resetVideo();m_videoReceiver.reset();m_view->setImage({});m_presentation->setActive(false);m_pending=false;m_sessionActive=false;m_remoteMax=false;furinaSetMaximizeIcon(ui->maximize,false);}
void MainWindow::requestDirection(){
 if(m_castUi->occupied())return;
 if(m_consent->request(m_direction->sharing(),ui->extend->isChecked())){m_pending=false;m_castUi->lockPending();}
}
void MainWindow::applyDirection(){
 if(!m_consent->active())return;
 m_pending=false;m_peer.sendProfile(m_resolution->value());m_sessionActive=ui->share->isChecked()||ui->receive->isChecked();
 m_peer.sendSession(ui->share->isChecked()?FR_START_RECEIVE:FR_STOP_RECEIVE);
 if(ui->share->isChecked())startCapture();else stopCapture();
 if(ui->receive->isChecked()){m_peer.sendSession(ui->extend->isChecked()?FR_EXTEND:FR_DUPLICATE);m_peer.sendSession(FR_START_SHARE);log("已请求电脑屏幕，等待画面。","Requested the PC screen. Waiting for frames.");}else m_peer.sendSession(FR_STOP_SHARE);

}
void MainWindow::session(quint32 action){
 if(action==FR_REQUEST_KEY){if(m_sharing)m_android.requestKeyframe();}
 else if(action==FR_START_SHARE){if(!m_consent->active()||!m_consent->sharing())return;m_pending=false;m_sessionActive=true;startCapture();}
 else if(action==FR_STOP_SHARE){stopCapture();}
 else if(action==FR_START_RECEIVE){if(!m_consent->active()||m_consent->sharing())return;m_pending=false;m_sessionActive=true;}
 else if(action==FR_STOP_RECEIVE){m_view->setImage({});m_videoVisible=false;m_android.resetVideo();m_videoReceiver.reset();}
 else if(action==FR_DUPLICATE||action==FR_EXTEND){if(m_castUi->occupied()&&ui->extend->isChecked()!=(action==FR_EXTEND)){log("投屏方式已锁定，忽略会话中的模式更改。","Casting options are locked. Ignoring an in-session mode change.");return;}if(!m_consent->active()||m_consent->sharing())return;const QSignalBlocker a(ui->duplicate),b(ui->extend);(action==FR_EXTEND?ui->extend:ui->duplicate)->setChecked(true);}

}
void MainWindow::maximize(){if(ui->share->isChecked()){if(!m_peer.ready()){log("请先连接电脑，再放大接收端画面。","Connect the PC before maximizing its received content.");return;}if(!m_consent->sharing())return;const bool requested=m_presentationSync->toggleRemote();log(requested?"已请求电脑全屏显示。":"已请求电脑退出全屏。",requested?"Requested full-screen viewing on the PC.":"Requested leaving full-screen viewing on the PC.");}else m_presentation->setActive(!m_presentation->active());}
void MainWindow::resizeEvent(QResizeEvent *event){QMainWindow::resizeEvent(event);if(m_layout)m_layout->refresh(m_presentation->active());}

void MainWindow::updateVideoGeometry(){
#ifdef Q_OS_ANDROID
 const auto pos=m_view->mapTo(this,QPoint());const qreal ratio=devicePixelRatioF();const QRect rect(qRound(pos.x()*ratio),qRound(pos.y()*ratio),qRound(m_view->width()*ratio),qRound(m_view->height()*ratio));
 const bool visible=m_videoVisible&&ui->receive->isChecked()&&!m_settings->isVisible()&&isVisible();const bool full=m_presentation->active();
 if(rect!=m_nativeRect||visible!=m_nativeVisible||full!=m_nativeFull){m_nativeRect=rect;m_nativeVisible=visible;m_nativeFull=full;m_android.updateVideoView(rect,visible,full);}
#endif
}
