#include "virtual_display_controller.h"
#include <QGuiApplication>
#include <QScreen>
#include <QFileInfo>
#include <QDir>
#ifdef Q_OS_WIN
#include <shellapi.h>
#endif
VirtualDisplayController::VirtualDisplayController(QObject *parent):QObject(parent){
 m_poll.setInterval(100);connect(&m_poll,&QTimer::timeout,this,&VirtualDisplayController::poll);
 m_releaseRetry.setInterval(250);connect(&m_releaseRetry,&QTimer::timeout,this,&VirtualDisplayController::finishRelease);
}
VirtualDisplayController::~VirtualDisplayController(){release();m_releaseRetry.stop();}
bool VirtualDisplayController::available() const{return fr_virtual_available()!=0;}
QString VirtualDisplayController::screenName() const{return m_qtName;}
QString VirtualDisplayController::deviceName() const{char name[128]{};return fr_virtual_name(&m_display,name,sizeof(name))?QString::fromUtf8(name):QString();}
bool VirtualDisplayController::position(QPoint *position) const{
 long x=0,y=0;if(!position||fr_virtual_get_position(&m_display,&x,&y))return false;*position=QPoint(x,y);return true;
}
bool VirtualDisplayController::setPosition(const QPoint &position){
 const long result=fr_virtual_set_position(&m_display,position.x(),position.y());
 if(result)emit status("恢复扩展屏位置失败，错误："+QString::number(result),"Restoring the extension position failed. Error: "+QString::number(result));
 return result==0;
}
void VirtualDisplayController::fail(const QString &cn,const QString &en){
 release();emit failed(cn,en);
}
void VirtualDisplayController::ensure(bool portrait){
 if(m_preparing)return;
 // A new session owns the target; an old cleanup timer must not detach it.
 m_releaseRetry.stop();
 if(m_display.acquired){const long result=fr_virtual_release(&m_display);if(result){fail("扩展屏清理尚未完成，请稍后重试。","Extension cleanup is still pending. Please retry shortly.");return;}}
 m_preparing=true;m_portrait=portrait;m_elapsed.restart();
 if(available()){acquire();return;}
#ifdef Q_OS_WIN
 if(fr_virtual_device_present()){
  fail("虚拟驱动尚未就绪。请完成 Windows 安装提示；若已完成，请重启后重试。","The virtual driver is not ready. Complete the Windows installation prompt; if completed, restart and retry.");return;
 }
 const QString helper=QDir(QCoreApplication::applicationDirPath()).filePath("virtual_display_helper.exe");
 QString package=QDir(QCoreApplication::applicationDirPath()).filePath("virtual-display");
 if(!QFileInfo::exists(package+"/MttVDD.inf"))package=QDir(QCoreApplication::applicationDirPath()).absoluteFilePath("../core/virtual-display/driver");
 if(!QFileInfo::exists(helper)||!QFileInfo::exists(package+"/MttVDD.inf")){
  fail("缺少虚拟显示驱动安装文件，请使用完整电脑端安装目录。","Virtual display installer is missing. Use the complete PC application directory.");return;
 }
 emit status("首次启用扩展：正在安装虚拟显示驱动，请允许系统安装提示。","First extension: installing the virtual display driver. Allow the Windows installation prompt.");
 const std::wstring exe=QDir::toNativeSeparators(helper).toStdWString();
 const std::wstring args=("install \""+QDir::toNativeSeparators(package)+"\"").toStdWString();
 SHELLEXECUTEINFOW info{};info.cbSize=sizeof(info);info.fMask=SEE_MASK_NOCLOSEPROCESS;info.lpVerb=L"runas";info.lpFile=exe.c_str();info.lpParameters=args.c_str();info.nShow=SW_HIDE;
 if(!ShellExecuteExW(&info)){fail("虚拟屏安装未完成，错误："+QString::number(GetLastError()),"Virtual display installation did not start. Error: "+QString::number(GetLastError()));return;}
 m_installer=info.hProcess;m_installing=true;m_poll.start();
#else
 fail("当前系统不支持 Windows 虚拟扩展屏。","Windows virtual extension is unavailable on this platform.");
#endif
}
void VirtualDisplayController::acquire(){
 const long result=fr_virtual_acquire_oriented(&m_display,m_portrait);
 if(result){fail("创建扩展桌面失败，错误："+QString::number(result),"Creating the extended desktop failed. Error: "+QString::number(result));return;}
 emit status(m_portrait?"正在启用竖屏扩展桌面：1200×1920，刷新率 60 赫兹。":"正在启用横屏扩展桌面：1920×1200，刷新率 60 赫兹。",m_portrait?"Enabling portrait extension: 1200x1920 / 60 Hz.":"Enabling landscape extension: 1920x1200 / 60 Hz.");
 m_elapsed.restart();m_poll.start();poll();
}
void VirtualDisplayController::poll(){
#ifdef Q_OS_WIN
 if(m_installing){
  DWORD code=STILL_ACTIVE;
  if(!GetExitCodeProcess(static_cast<HANDLE>(m_installer),&code)){fail("无法读取驱动安装状态。","Cannot read driver installation status.");return;}
  if(code==STILL_ACTIVE)return;
  CloseHandle(static_cast<HANDLE>(m_installer));m_installer=nullptr;m_installing=false;
  if(code||!available()){fail(code==3?"驱动已安装，请重启电脑后启用扩展。":"虚拟显示驱动安装失败或被取消，错误："+QString::number(code),code==3?"Driver installed. Restart Windows before extending.":"Virtual display installation failed or was cancelled. Error: "+QString::number(code));return;}
  acquire();return;
 }
#endif
 if(!m_preparing)return;
 for(QScreen *screen:QGuiApplication::screens()){
  bool match=screen->name()==deviceName();
#ifdef Q_OS_WIN
  match=match||(!QString::fromUtf8(m_display.monitor_name).isEmpty()&&screen->name()==QString::fromUtf8(m_display.monitor_name));
#endif
  const auto geometry=screen->geometry();const bool correctOrientation=(geometry.height()>geometry.width())==m_portrait;
  if(match&&correctOrientation&&screen!=QGuiApplication::primaryScreen()&&geometry.width()>0){m_qtName=screen->name();m_preparing=false;m_poll.stop();emit ready(screen->name());return;}
 }
 if(m_elapsed.elapsed()>10000)fail("Windows 未能启用虚拟扩展桌面。","Windows did not activate the virtual desktop.");
}
void VirtualDisplayController::release(){
 m_preparing=false;m_poll.stop();m_qtName.clear();
#ifdef Q_OS_WIN
 if(m_installer){CloseHandle(static_cast<HANDLE>(m_installer));m_installer=nullptr;}
#endif
 m_installing=false;
 m_releaseRetry.stop();m_releaseAttempts=0;finishRelease();
}
void VirtualDisplayController::finishRelease(){
 if(QGuiApplication::platformName()=="offscreen")return;
 const long result=fr_virtual_release(&m_display);
 if(!result){m_releaseRetry.stop();return;}
 if(++m_releaseAttempts<8){m_releaseRetry.start();return;}
 m_releaseRetry.stop();emit status("移除虚拟扩展屏失败，错误："+QString::number(result),"Could not detach the virtual extension. Error: "+QString::number(result));
}
