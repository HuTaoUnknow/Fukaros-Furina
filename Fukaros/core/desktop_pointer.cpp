#include "desktop_pointer.h"
#include "interface_support.h"
#include <QGuiApplication>
DesktopPointer::DesktopPointer(QObject *parent):QObject(parent){m_refresh.setInterval(250);connect(&m_refresh,&QTimer::timeout,this,&DesktopPointer::refresh);}
DesktopPointer::~DesktopPointer(){stop();}
void DesktopPointer::setMultiplier(double value){m_percent=qBound(25,qRound(value*100),400);refresh();}
void DesktopPointer::setDisplay(const QString &name){m_display=name;m_refresh.start();refresh();}
void DesktopPointer::stop(){m_refresh.stop();m_display.clear();refresh();}
#ifdef Q_OS_WIN
DesktopPointer *DesktopPointer::active=nullptr;
LRESULT CALLBACK DesktopPointer::hook(int code,WPARAM message,LPARAM data){
 auto *s=active;if(code<0||message!=WM_MOUSEMOVE||!s)return CallNextHookEx(nullptr,code,message,data);
 auto *event=reinterpret_cast<MSLLHOOKSTRUCT*>(data);const POINT next=event->pt;
 if(s->m_warping)return CallNextHookEx(nullptr,code,message,data);
 if(!s->m_valid||(event->flags&LLMHF_INJECTED)){s->m_last=next;s->m_valid=true;s->m_fraction={};return CallNextHookEx(nullptr,code,message,data);}
 long x,y;auto &r=s->m_bounds;const bool adjusted=fr_pointer_gain(&s->m_fraction,s->m_percent,r.left,r.top,r.right,r.bottom,s->m_last.x,s->m_last.y,next.x,next.y,&x,&y);
 s->m_last=next;
 if(adjusted){
  s->m_warping=true;const BOOL ok=SetCursorPos(x,y);s->m_warping=false;
  if(ok){GetCursorPos(&s->m_last);return 1;}
  s->m_fraction={};
 }
 return CallNextHookEx(nullptr,code,message,data);
}
#endif
void DesktopPointer::refresh(){
#ifdef Q_OS_WIN
 DEVMODEW mode{};mode.dmSize=sizeof(mode);
 const bool enabled=m_percent!=100&&!m_display.isEmpty()&&QGuiApplication::platformName()!="offscreen"&&EnumDisplaySettingsW(reinterpret_cast<const wchar_t*>(m_display.utf16()),ENUM_CURRENT_SETTINGS,&mode)&&mode.dmPelsWidth&&mode.dmPelsHeight;
 if(!enabled){if(m_hook)UnhookWindowsHookEx(m_hook);m_hook=nullptr;if(active==this)active=nullptr;m_valid=false;m_fraction={};return;}
 RECT next{mode.dmPosition.x,mode.dmPosition.y,mode.dmPosition.x+LONG(mode.dmPelsWidth),mode.dmPosition.y+LONG(mode.dmPelsHeight)};
 if(!EqualRect(&next,&m_bounds)){m_bounds=next;m_valid=false;m_fraction={};}
 if(!m_hook){
  if(active&&active!=this)return;active=this;GetCursorPos(&m_last);m_valid=true;
  m_hook=SetWindowsHookExW(WH_MOUSE_LL,hook,GetModuleHandleW(nullptr),0);
  if(!m_hook){active=nullptr;emit status(furinaStatus("副屏鼠标速率暂不可用，系统速率保持不变。","Extended-screen pointer adjustment is unavailable; system speed is unchanged."));}
 }
#endif
}
