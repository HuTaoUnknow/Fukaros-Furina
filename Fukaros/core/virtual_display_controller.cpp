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
}
VirtualDisplayController::~VirtualDisplayController(){release();}
bool VirtualDisplayController::available() const{return fr_virtual_available()!=0;}
QString VirtualDisplayController::screenName() const{return m_qtName;}
QString VirtualDisplayController::deviceName() const{char name[128]{};return fr_virtual_name(&m_display,name,sizeof(name))?QString::fromUtf8(name):QString();}
void VirtualDisplayController::fail(const QString &cn,const QString &en){
 release();emit failed(cn,en);
}
void VirtualDisplayController::ensure(){
 if(m_preparing)return;
 m_preparing=true;m_elapsed.restart();
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
 const long result=fr_virtual_acquire(&m_display);
 if(result){fail("创建扩展桌面失败，错误："+QString::number(result),"Creating the extended desktop failed. Error: "+QString::number(result));return;}
 emit status("正在启用平板扩展桌面：1920×1200 / 60 Hz。","Enabling the tablet desktop: 1920x1200 / 60 Hz.");
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
  if(match&&screen!=QGuiApplication::primaryScreen()&&screen->geometry().width()>0){m_qtName=screen->name();m_preparing=false;m_poll.stop();emit ready(screen->name());return;}
 }
 if(m_elapsed.elapsed()>10000)fail("Windows 未能启用虚拟扩展桌面。","Windows did not activate the virtual desktop.");
}
void VirtualDisplayController::release(){
 m_preparing=false;m_poll.stop();m_qtName.clear();
#ifdef Q_OS_WIN
 if(m_installer){CloseHandle(static_cast<HANDLE>(m_installer));m_installer=nullptr;}
#endif
 m_installing=false;
 const long result=fr_virtual_release(&m_display);
 if(result)emit status("收起虚拟扩展屏失败，错误："+QString::number(result),"Could not restore the virtual display. Error: "+QString::number(result));
}
