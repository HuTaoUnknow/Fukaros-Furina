#include "interface_support.h"
#include <QDateTime>
#include <QEvent>
#include <QRegularExpression>
#include <QPainter>
#include <QApplication>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QIconEngine>
#include <QPainterPath>
#include <QSignalBlocker>
#include <cmath>
#include "video_policy.h"
#ifdef Q_OS_WIN
#include <windows.h>
#include <QLibrary>
namespace {
// Windows 11 may retain DWM's rounded/transparent frame on a Qt fullscreen
// window. Keep this local to presentation; normal windows retain their style.
struct FullscreenDwm {
 QLibrary library;using Set=HRESULT(WINAPI*)(HWND,DWORD,LPCVOID,DWORD);using Get=HRESULT(WINAPI*)(HWND,DWORD,PVOID,DWORD);
 Set set=nullptr;Get get=nullptr;
 FullscreenDwm(){wchar_t system[MAX_PATH]{};if(!GetSystemDirectoryW(system,MAX_PATH))return;library.setFileName(QString::fromWCharArray(system)+"/dwmapi.dll");set=reinterpret_cast<Set>(library.resolve("DwmSetWindowAttribute"));get=reinterpret_cast<Get>(library.resolve("DwmGetWindowAttribute"));}
};
FullscreenDwm &fullscreenDwm(){static FullscreenDwm api;return api;}
constexpr DWORD cornerAttribute=33,borderAttribute=34;
void presentationFrame(QWidget *window,quint32 border,quint32 corner){auto &api=fullscreenDwm();if(api.set){const auto handle=reinterpret_cast<HWND>(window->winId());api.set(handle,borderAttribute,&border,sizeof(border));api.set(handle,cornerAttribute,&corner,sizeof(corner));}}
}
#endif
DirectionChoice::DirectionChoice(QWidget *parent):QObject(parent),m_share(parent->findChild<QCheckBox*>("share")),m_receive(parent->findChild<QCheckBox*>("receive")),m_group(new QButtonGroup(this)){
 m_group->setExclusive(true);m_group->addButton(m_share,1);m_group->addButton(m_receive,0);
 if(!m_share->isChecked()&&!m_receive->isChecked())m_receive->setChecked(true);
 connect(m_group,&QButtonGroup::idToggled,this,[this](int,bool checked){if(checked)emit changed();});
}
bool DirectionChoice::sharing()const{return m_share->isChecked();}
void DirectionChoice::setSharing(bool sharing){const QSignalBlocker a(m_share),b(m_receive);(sharing?m_share:m_receive)->setChecked(true);}
ResolutionChoice::ResolutionChoice(QWidget *parent):QObject(parent),m_group(new QButtonGroup(this)){
 m_group->setExclusive(true);for(int edge:{1920,1080,720,0})m_group->addButton(parent->findChild<QPushButton*>("resolution"+QString::number(edge)),edge);
 connect(m_group,&QButtonGroup::idClicked,this,[this](int edge){if(m_value!=edge){m_value=edge;emit changed(edge);}});
}
void ResolutionChoice::setValue(int edge){if(!fr_resolution_valid(edge))edge=0;const bool change=m_value!=edge;m_value=edge;m_group->button(edge)->setChecked(true);if(change)emit changed(edge);}
namespace {
class ToolIconEngine final : public QIconEngine {
public:
 enum Kind { Gear,Maximize,Restore,WindowMinimize,WindowMaximize,WindowRestore,WindowClose };
 explicit ToolIconEngine(Kind kind):m_kind(kind){}
 QIconEngine *clone() const override{return new ToolIconEngine(m_kind);}
 QPixmap pixmap(const QSize &size,QIcon::Mode mode,QIcon::State state) override{
  QPixmap result(size);result.fill(Qt::transparent);QPainter painter(&result);paint(&painter,QRect(QPoint(),size),mode,state);return result;
 }
 void paint(QPainter *painter,const QRect &rect,QIcon::Mode mode,QIcon::State) override{
  painter->save();painter->setRenderHint(QPainter::Antialiasing);
  const qreal scale=qMin(rect.width(),rect.height())/24.0;
  painter->translate(rect.center().x()+0.5-12*scale,rect.center().y()+0.5-12*scale);painter->scale(scale,scale);
  const QColor color=mode==QIcon::Disabled?QColor("#768393"):QColor("#e4eaf0");
  if(m_kind==Gear){
   QPainterPath gear;gear.setFillRule(Qt::OddEvenFill);
   constexpr double pi=3.141592653589793;
   const double offsets[]={-22.5,-14,-11,11,14,22.5};
   for(int tooth=0;tooth<8;++tooth)for(int step=0;step<6;++step){
    const double angle=(tooth*45+offsets[step])*pi/180.0,radius=(step==2||step==3)?10:8;
    const QPointF point(12+radius*std::cos(angle),12+radius*std::sin(angle));
    if(tooth==0&&step==0)gear.moveTo(point);else gear.lineTo(point);
   }
   gear.closeSubpath();gear.addEllipse(QPointF(12,12),3.2,3.2);painter->setPen(Qt::NoPen);painter->setBrush(color);painter->drawPath(gear);
  }else if(m_kind>=WindowMinimize){
   painter->setPen(QPen(color,1.6,Qt::SolidLine,Qt::SquareCap));painter->setBrush(Qt::NoBrush);
   if(m_kind==WindowMinimize)painter->drawLine(QPointF(5,16),QPointF(19,16));
   else if(m_kind==WindowClose){painter->drawLine(QPointF(6,6),QPointF(18,18));painter->drawLine(QPointF(18,6),QPointF(6,18));}
   else if(m_kind==WindowMaximize)painter->drawRect(QRectF(5,5,14,14));
   else {painter->drawPolyline(QPolygonF{QPointF(9,8),QPointF(9,5),QPointF(19,5),QPointF(19,15),QPointF(16,15)});painter->drawRect(QRectF(5,9,10,10));}
  }else{
   painter->setPen(QPen(color,1.8,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));painter->setBrush(Qt::NoBrush);
   for(int corner=0;corner<4;++corner){
    painter->save();painter->translate(12,12);painter->rotate(corner*90);painter->translate(-12,-12);
    QPainterPath arrow;
    if(m_kind==Maximize){arrow.moveTo(4,9);arrow.lineTo(4,4);arrow.lineTo(9,4);arrow.moveTo(4,4);arrow.lineTo(9,9);}
    else{arrow.moveTo(4,9);arrow.lineTo(9,9);arrow.lineTo(9,4);arrow.moveTo(9,9);arrow.lineTo(4,4);}
    painter->drawPath(arrow);painter->restore();
   }
  }
  painter->restore();
 }
private: Kind m_kind;
};
}
void furinaSetMaximizeIcon(QPushButton *button,bool restore){
 button->setText({});button->setToolTip(restore?"还原":"放大");button->setAccessibleName(button->toolTip());
 button->setIcon(QIcon(new ToolIconEngine(restore?ToolIconEngine::Restore:ToolIconEngine::Maximize)));button->setIconSize(QSize(22,22));
}
void furinaConfigureToolIcons(QPushButton *settings,QPushButton *maximize){
 settings->setText({});settings->setToolTip("设置");settings->setAccessibleName("设置");settings->setIcon(QIcon(new ToolIconEngine(ToolIconEngine::Gear)));settings->setIconSize(QSize(22,22));furinaSetMaximizeIcon(maximize,false);
}
QString furinaStyle(){return QStringLiteral(R"(
 QWidget { background:#111820; color:#e4eaf0; font-family:"Microsoft YaHei","Noto Sans CJK SC",sans-serif; font-size:13px; }
 QFrame#sidebar { background:#1a232e; border-right:1px solid #34404e; }
 QWidget#bottomPanel { background:#17212c; }
 QPushButton { background:#253344; border:1px solid #435264; border-radius:6px; padding:7px 10px; min-height:26px; }
 QPushButton:hover { background:#33465d; }
 QPushButton:pressed { background:#406184; }
 QPushButton:disabled { color:#768393; border-color:#283542; }
 QPushButton#start,QPushButton#settings,QPushButton#maximize { min-height:36px; padding:6px 0px; }
 QPushButton#start,QPushButton#settings,QPushButton#maximize { min-width:38px; min-height:38px; max-width:38px; max-height:38px; padding:0px; border-radius:20px; font-size:12px; }
 QWidget#windowControls { background:#17212c; }
 QWidget#windowControls QPushButton { min-width:32px; max-width:32px; min-height:26px; max-height:26px; padding:0; border:0; border-radius:0; }
 QPushButton#windowClose:hover { background:#b83242; }
 QFrame#consentPanel { background:#253344; border:1px solid #57b9d8; border-radius:4px; }
 QWidget#resolutionPanel QPushButton { min-height:18px; padding:3px 5px; border-radius:4px; font-size:11px; }
 QWidget#resolutionPanel QPushButton:checked { background:#216e89; border-color:#57b9d8; }
 QLabel#performanceHint { color:#98afc6; font-size:11px; }
 QLineEdit,QComboBox { background:#0c121a; border:1px solid #394b5e; border-radius:5px; padding:7px; min-height:24px; }
 QComboBox::drop-down { border:0px; border-left:1px solid #435264; background:#253344; width:24px; }
 QFrame#modeSeparator,QFrame#resolutionSeparator { border:0px; border-top:1px solid #435264; min-height:1px; max-height:1px; }
 QFrame#settingsSeparator { border:0px; border-top:1px solid #5c758d; min-height:8px; max-height:8px; margin-top:6px; }
 QGroupBox { border:1px solid #394b5e; border-radius:5px; margin-top:12px; padding:12px 8px 8px; }
 QGroupBox::title { subcontrol-origin:margin; left:10px; padding:0 4px; }
 QTabWidget::pane { border:1px solid #394b5e; border-radius:5px; padding:8px; }
 QTabBar::tab { background:#253344; border:1px solid #394b5e; padding:9px 16px; }
 QTabBar::tab:selected { background:#216e89; }
 QTabBar::tab:disabled,QCheckBox:disabled,QLineEdit:disabled { color:#768393; }
 QLabel#sessionSettingsHint { color:#f0c779; }
 QRadioButton { padding:6px 0px; }
 QRadioButton::indicator { width:16px; height:16px; border:1px solid #8096ae; border-radius:8px; background:#0c121a; }
 QRadioButton::indicator:checked { background:#57b9d8; border:3px solid #244658; }
 QCheckBox { padding:5px 0px; }
 QCheckBox::indicator { width:16px; height:16px; border:1px solid #8096ae; border-radius:3px; background:#0c121a; }
 QCheckBox::indicator:checked { background:#57b9d8; border:1px solid #a5e8fa; }
 QPlainTextEdit { background:#0b1119; border:1px solid #344354; border-radius:5px; padding:5px; font-size:12px; }
 QLabel#debugTitle,QLabel#directionTitle { color:#98afc6; font-size:12px; }
 QLabel#castHint { color:#b5c6d7; font-size:12px; }
 QScrollArea { border:0px; }
 QScrollBar:vertical { width:8px; background:#17212c; }
 QScrollBar::handle:vertical { background:#415267; min-height:25px; }
 QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical { height:0px; }
 QScrollBar:horizontal { height:8px; background:#17212c; }
 QScrollBar::handle:horizontal { background:#415267; min-width:25px; }
 QScrollBar::add-line:horizontal,QScrollBar::sub-line:horizontal { width:0px; }

 QGroupBox[settingsRole="required"] { background:#14251f; border-color:#3d6453; }
 QGroupBox[settingsRole="optional"] { background:#27241c; border-color:#6c6240; }
 QGroupBox[settingsRole="required"] QLabel { background:transparent; color:#d9e6df; }
 QGroupBox[settingsRole="optional"] QLabel,QGroupBox[settingsRole="optional"] QCheckBox { background:transparent; color:#e4ddc7; }
 QLineEdit[settingsRole="required"] { background:#1a3027; border-color:#4f8068; color:#e4eee8; }
 QLineEdit[settingsRole="optional"],QGroupBox[settingsRole="optional"] QComboBox { background:#302b20; border-color:#6c6240; color:#eae4d3; }
 QLineEdit[settingsRole="required"]:focus { border:2px solid #6f9b81; }
 QLineEdit[settingsRole="optional"]:focus { border:2px solid #9b8b55; }
 QPushButton[stepHelp="true"] { background:transparent; border:0; min-width:32px; max-width:32px; min-height:16px; max-height:16px; padding:0; font-size:16px; }
 QPushButton[stepHelp="true"]:hover { background:transparent; color:#a5e8fa; }
 QPushButton[stepHelp="true"]:pressed { background:transparent; color:#57b9d8; }
 QFrame[stepPopup="true"] { background:#203044; border:1px solid #77bdd9; border-radius:8px; }
 QFrame[stepPopup="true"] QWidget { background:transparent; }
 QLabel#stepsTitle { font-size:15px; font-weight:bold; color:#f5faff; }
 QLabel#stepsBody { color:#eaf3ff; padding:2px; }
 QPushButton#closeSteps { min-width:32px; max-width:32px; min-height:32px; max-height:32px; padding:0; font-size:20px; }
 QFrame#usbSeparator,QFrame#wifiSeparator { border:0; border-top:1px solid #73837b; min-height:1px; max-height:1px; }
)");}
QString furinaStatus(const QString &cn,const QString &en){return cn+"\nEN: "+en;}
void FurinaComboBox::paintEvent(QPaintEvent *event){
 QComboBox::paintEvent(event);QPainter painter(this);painter.setRenderHint(QPainter::Antialiasing);painter.setPen(Qt::NoPen);painter.setBrush(isEnabled()?QColor("#d9e5ee"):QColor("#687a8b"));
 const qreal x=width()-13,y=height()/2.0;painter.drawPolygon(QPolygonF{QPointF(x-4,y-2),QPointF(x+4,y-2),QPointF(x,y+3)});
}
WindowControls::WindowControls(QMainWindow *window,Presentation *presentation,ScreenView *view,QWidget *controls):QObject(window),m_window(window),m_presentation(presentation),m_view(view),m_controls(controls){
 controls->setParent(view);auto *minimize=controls->findChild<QPushButton*>("windowMinimize");auto *maximize=controls->findChild<QPushButton*>("windowMaximize");auto *close=controls->findChild<QPushButton*>("windowClose");
 minimize->setIcon(QIcon(new ToolIconEngine(ToolIconEngine::WindowMinimize)));close->setIcon(QIcon(new ToolIconEngine(ToolIconEngine::WindowClose)));
 for(auto *button:{minimize,maximize,close}){button->setText({});button->setIconSize(QSize(18,18));}
 connect(minimize,&QPushButton::clicked,window,&QWidget::showMinimized);
 connect(maximize,&QPushButton::clicked,this,[this]{if(m_presentation->active()){m_presentation->setActive(false);m_window->showMaximized();}else if(m_window->isMaximized())m_window->showNormal();else m_window->showMaximized();});
 connect(close,&QPushButton::clicked,window,&QWidget::close);
 connect(presentation,&Presentation::changed,this,[this]{refresh();arrange();});
 view->installEventFilter(this);window->installEventFilter(this);controls->show();refresh();arrange();
}
void WindowControls::refresh(){const bool restore=m_window->isMaximized()||m_presentation->active();auto *button=m_controls->findChild<QPushButton*>("windowMaximize");button->setIcon(QIcon(new ToolIconEngine(restore?ToolIconEngine::WindowRestore:ToolIconEngine::WindowMaximize)));button->setToolTip(restore?"还原窗口":"最大化窗口");button->setAccessibleName(button->toolTip());}
void WindowControls::arrange(){m_controls->adjustSize();m_controls->move(qMax(0,m_view->width()-m_controls->width()-12),12);m_controls->raise();}
bool WindowControls::eventFilter(QObject *watched,QEvent *event){if(watched==m_view&&event->type()==QEvent::Resize)arrange();if(watched==m_window&&event->type()==QEvent::WindowStateChange){refresh();arrange();}return QObject::eventFilter(watched,event);}
void FurinaComboBox::showPopup(){
#ifdef Q_OS_ANDROID
 // Keep the popup in the current surface: secondary native widget windows can deadlock on Android.
 if(!m_popup){m_popup=new QListWidget(window());m_popup->setObjectName("comboOptions");m_popup->setStyleSheet("QListWidget{background:#253344;border:1px solid #7497b4;} QListWidget::item{padding:12px;min-height:20px;} QListWidget::item:selected{background:#216e89;}");
  connect(m_popup,&QListWidget::itemClicked,this,[this](QListWidgetItem *item){const auto index=m_popup->row(item);setCurrentIndex(index);hidePopup();emit activated(index);});
 }
 m_popup->clear();for(int i=0;i<count();++i)m_popup->addItem(itemText(i));
 const int popupHeight=qMin(qMax(44,count()*44+4),qMax(44,window()->height()-32));
 const int popupWidth=qMin(qMax(width(),160),window()->width()-24);QPoint pos=mapTo(window(),QPoint(0,height()));
 if(pos.y()+popupHeight>window()->height()-8)pos.setY(qMax(8,mapTo(window(),QPoint()).y()-popupHeight));
 pos.setX(qBound(8,pos.x(),qMax(8,window()->width()-popupWidth-8)));
 m_popup->setGeometry(pos.x(),pos.y(),popupWidth,popupHeight);m_popup->setCurrentRow(currentIndex());m_popup->show();m_popup->raise();m_popup->setFocus();qApp->installEventFilter(this);
#else
 QComboBox::showPopup();
#endif
}
void FurinaComboBox::hidePopup(){
#ifdef Q_OS_ANDROID
 if(m_popup)m_popup->hide();qApp->removeEventFilter(this);setFocus();
#else
 QComboBox::hidePopup();
#endif
}
bool FurinaComboBox::eventFilter(QObject *watched,QEvent *event){
 if(m_popup&&m_popup->isVisible()){
  if(event->type()==QEvent::MouseButtonPress){auto *mouse=static_cast<QMouseEvent*>(event);if(!m_popup->rect().contains(m_popup->mapFromGlobal(mouse->globalPosition().toPoint()))&&watched!=this)hidePopup();}
  else if(event->type()==QEvent::KeyPress){auto *key=static_cast<QKeyEvent*>(event);if(key->key()==Qt::Key_Back||key->key()==Qt::Key_Escape){hidePopup();return true;}}
 }
 return QComboBox::eventFilter(watched,event);
}
QString furinaLog(const QString &message,const QString &english){
 QString cn=message.trimmed(),en=english.trimmed();if(cn.isEmpty())return {};
 const auto separator=cn.indexOf("\nEN: ");if(separator>=0){en=cn.mid(separator+5);cn=cn.left(separator);}
 struct Pair{const char *cn;const char *en;};
 static const Pair translations[]={
  {"视频数据无效","Invalid video packet"},{"视频档位无效","Invalid video profile"},{"视频反馈无效","Invalid video feedback"},
  {"配对成功","Paired"},{"连接已断开","Disconnected"},{"连接超时","Connection timeout"},
  {"配对码至少需要 6 位","Pairing code requires at least 6 characters"},
  {"请输入平板配对码（至少 6 位）","Enter the tablet pairing code (at least 6 characters)"},
  {"配对码错误","Pairing code rejected"},{"需要先完成认证","Authentication required"},
  {"接收缓存超出限制","Receive buffer exceeded"},{"协议头无效","Invalid protocol header"},
  {"画面尺寸无效","Invalid frame dimensions"},{"JPEG 图像无效","Invalid JPEG"},
  {"触控手势无效","Invalid gesture"},{"按键无效","Invalid key"},{"状态消息无效","Invalid status"},
  {"心跳请求无效","Invalid ping"},{"心跳响应无效","Invalid pong"},{"收到未知消息","Unexpected message"},
  {"接收端处理过慢","Peer too slow"},{"投屏请求无效","Invalid session request"},{"监听 IP 地址无效","Invalid listening IP address"},
  {"ADB 操作超时","ADB timed out"},{"请先填写 USB 设备码","Select a device serial first"},
  {"ADB 队列已满","ADB queue is full"},{"ADB 命令失败","ADB command failed"},
  {"连接被拒绝，请检查对端服务是否启动","Connection refused"},{"找不到设备地址","Host not found"},
  {"录屏授权已取消","Screen capture permission was cancelled"},
  {"平板录屏已启动","Tablet screen capture started"},
  {"系统已停止屏幕共享","System stopped screen sharing"},
  {"请在平板设置中启用 Furina 无障碍服务","Enable Furina accessibility service in tablet settings"},
  {"上一手势尚未完成","Previous gesture has not completed"},{"系统取消了触控手势","System cancelled the gesture"},
  {"系统拒绝了触控手势","System rejected the gesture"},{"无障碍服务未开启","Accessibility service is disabled"},
  {"系统拒绝了导航操作","System rejected the navigation action"},
  {"桌面模拟：使用当前主屏幕代替 Android 录屏","Desktop simulation: using the primary monitor for capture"},
  {"无障碍触控仅在 Android 可用；桌面模拟只记录控制事件","Accessibility is available on Android; desktop simulation only logs input"}
 };
 if(en.isEmpty())for(const auto &p:translations){if(cn==QString::fromUtf8(p.cn)||cn==QString::fromUtf8(p.en)){cn=QString::fromUtf8(p.cn);en=QString::fromUtf8(p.en);break;}}
 if(en.isEmpty()&&cn.startsWith("Listening on port ")){en=cn;cn="正在监听端口 "+cn.mid(18);}
 if(en.isEmpty()){
  static const Pair prefixes[]={
   {"视频编码帧失败：","Video encoding failed: "},{"硬件编码错误：","Hardware encoder error: "},{"视频解码失败：","Video decode failed: "},{"GPU 录屏失败：","GPU capture failed: "},{"录屏启动失败：","Starting capture failed: "},{"分辨率切换失败：","Resolution change failed: "},{"Android H.264 硬件编码：","Android hardware H.264 encoding: "},{"Android H.264 硬件解码：","Android hardware H.264 decoding: "},{"视频数据无效","Invalid video packet"},{"视频档位无效","Invalid video profile"},{"视频反馈无效","Invalid video feedback"},
   {"录屏授权失败：","Screen capture permission failed: "},{"启动录屏失败：","Starting screen capture failed: "},
   {"录屏失败：","Screen capture failed: "},{"录屏帧处理失败：","Processing captured frame failed: "},
   {"模拟手势 ","Simulated gesture "},{"模拟按键 ","Simulated key "}
  };
  for(const auto &p:prefixes)if(cn.startsWith(QString::fromUtf8(p.cn))){en=QString::fromUtf8(p.en)+cn.mid(QString::fromUtf8(p.cn).size());break;}
 }
 en.replace("，目标 60 FPS",", target 60 FPS");
 if(en.isEmpty()){en="Device/system report: "+cn;cn="设备 / 系统原始信息："+cn;}
 cn.replace('\n'," | ");en.replace('\n'," | ");
 const auto stamp=QDateTime::currentDateTime().toString("[HH:mm:ss] ");
 return stamp+"中 · "+cn+'\n'+stamp+"EN · "+en;
}
Presentation::Presentation(QMainWindow *window,QWidget *sidebar,QWidget *controls,ScreenView *view):
 QObject(window),m_window(window),m_sidebar(sidebar),m_controls(controls),m_view(view){
 connect(view,&ScreenView::leaveFullScreen,this,[this]{setActive(false);});
}
void Presentation::setActive(bool on){
 if(m_active==on)return;m_active=on;
 if(on){
  m_geometry=m_window->geometry();m_wasMaximized=m_window->isMaximized();m_sidebar->hide();m_controls->hide();
#ifdef Q_OS_WIN
  m_windowFlags=m_window->windowFlags();auto &api=fullscreenDwm();const auto handle=reinterpret_cast<HWND>(m_window->winId());
  m_borderColor=0xffffffffu;m_cornerPreference=0;if(api.get){api.get(handle,borderAttribute,&m_borderColor,sizeof(m_borderColor));api.get(handle,cornerAttribute,&m_cornerPreference,sizeof(m_cornerPreference));}
  auto *screen=m_window->screen();m_window->setWindowFlags(m_windowFlags|Qt::FramelessWindowHint|Qt::NoDropShadowWindowHint);m_window->setScreen(screen);
#endif
  m_window->showFullScreen();
#ifdef Q_OS_WIN
  presentationFrame(m_window,0xfffffffeu,1); // DWMWA_COLOR_NONE / DWMWCP_DONOTROUND.
#endif
  m_view->setFocus();
 }else{
  m_sidebar->show();m_controls->show();
#ifdef Q_OS_WIN
  m_window->setWindowFlags(m_windowFlags);
#endif
  if(m_wasMaximized)m_window->showMaximized();else{m_window->showNormal();m_window->setGeometry(m_geometry);}
#ifdef Q_OS_WIN
  presentationFrame(m_window,m_borderColor,m_cornerPreference);
#endif
 }
 emit changed(on);
}
