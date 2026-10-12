#include "desktop_tray.h"
#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QMenu>
#include <QSystemTrayIcon>
#include <QWidget>

DesktopTray::DesktopTray(QWidget *window,const QIcon &icon):
 QObject(window),m_window(window),m_tray(new QSystemTrayIcon(icon,this)),m_menu(new QMenu(window)){
 m_tray->setObjectName("desktopTrayIcon");
 m_tray->setToolTip("Fukaros");
 auto *show=m_menu->addAction("打开 Fukaros");show->setObjectName("trayShow");
 m_menu->addSeparator();
 auto *quit=m_menu->addAction("退出 Fukaros");quit->setObjectName("trayQuit");
 m_tray->setContextMenu(m_menu);
 connect(show,&QAction::triggered,this,&DesktopTray::restoreWindow);
 connect(m_tray,&QSystemTrayIcon::activated,this,[this](QSystemTrayIcon::ActivationReason reason){
  if(reason==QSystemTrayIcon::Trigger||reason==QSystemTrayIcon::DoubleClick)restoreWindow();
 });
 connect(quit,&QAction::triggered,this,[this]{m_quitting=true;m_tray->hide();emit quitRequested();});
 if(QSystemTrayIcon::isSystemTrayAvailable()&&!icon.isNull())m_tray->show();
 window->installEventFilter(this);
}
bool DesktopTray::available() const{return !m_quitting&&m_tray->isVisible()&&QSystemTrayIcon::isSystemTrayAvailable();}
void DesktopTray::restoreWindow(){
 if(!m_window||m_quitting)return;
 m_window->setWindowState(m_window->windowState()&~Qt::WindowMinimized);
 m_window->show();m_window->raise();m_window->activateWindow();
}
bool DesktopTray::eventFilter(QObject *watched,QEvent *event){
 if(watched==m_window&&event->type()==QEvent::Close&&available()&&!qApp->isSavingSession()){
  static_cast<QCloseEvent*>(event)->ignore();m_window->hide();emit hiddenToTray();return true;
 }
 return QObject::eventFilter(watched,event);
}
