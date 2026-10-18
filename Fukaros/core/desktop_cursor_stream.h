#pragma once
#include <QObject>
#include <QTimer>
#include <QElapsedTimer>
#include <functional>
#include "cursor_wire.h"
#include "cursor_shape.h"
#include <vector>
#ifdef Q_OS_WIN
#include <windows.h>
#endif
// Position sampling is independent of capture/encoding and uses the final
// Windows cursor position (after secondary-display speed adjustment).
class DesktopCursorStream {
 QTimer m_tick;QElapsedTimer m_sample,m_layoutSample;QString m_display;QByteArray m_shape,m_last;
 std::function<void(const QByteArray&)> m_send;bool m_enabled=false;
#ifdef Q_OS_WIN
 HCURSOR m_handle=nullptr;POINT m_hotspot{};RECT m_bounds{};
 static QByteArray nativeShape(const ICONINFO &icon){
  BITMAP bitmap{};HBITMAP source=icon.hbmColor?icon.hbmColor:icon.hbmMask;if(!source||!GetObject(source,sizeof bitmap,&bitmap))return {};
  const int w=bitmap.bmWidth,h=bitmap.bmHeight,rows=icon.hbmColor?h:h/2;if(w<1||rows<1||w>512||rows>512)return {};
  HDC dc=GetDC(nullptr);if(!dc)return {};unsigned kind=FR_CURSOR_COLOR;std::vector<uint8_t> pixels(size_t(w)*rows*4);
  if(icon.hbmColor){BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=w;info.bmiHeader.biHeight=-h;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
   if(!GetDIBits(dc,source,0,h,pixels.data(),&info,DIB_RGB_COLORS)){ReleaseDC(nullptr,dc);return {};}
   bool alpha=false;for(size_t i=3;i<pixels.size();i+=4)alpha|=pixels[i]!=0;
   if(!alpha){kind=FR_CURSOR_MASKED;const int pitch=((w+31)/32)*4;std::vector<uint8_t> mask(size_t(pitch)*h);struct MonoInfo{BITMAPINFOHEADER header;RGBQUAD colors[2];} mono{};mono.header=info.bmiHeader;mono.header.biBitCount=1;mono.colors[1]={255,255,255,0};
    if(!GetDIBits(dc,icon.hbmMask,0,h,mask.data(),reinterpret_cast<BITMAPINFO*>(&mono),DIB_RGB_COLORS)){ReleaseDC(nullptr,dc);return {};}
    for(int y=0;y<h;++y)for(int x=0;x<w;++x)pixels[(size_t(y)*w+x)*4+3]=(mask[size_t(y)*pitch+x/8]&(0x80>>(x&7)))?255:0;
   }
  }else{kind=FR_CURSOR_MONO;const int pitch=((w+31)/32)*4;std::vector<uint8_t> mask(size_t(pitch)*h);struct MonoInfo{BITMAPINFOHEADER header;RGBQUAD colors[2];} info{};info.header.biSize=sizeof(BITMAPINFOHEADER);info.header.biWidth=w;info.header.biHeight=-h;info.header.biPlanes=1;info.header.biBitCount=1;info.header.biCompression=BI_RGB;info.colors[1]={255,255,255,0};
   if(!GetDIBits(dc,source,0,h,mask.data(),reinterpret_cast<BITMAPINFO*>(&info),DIB_RGB_COLORS)||!fr_cursor_unpack(kind,w,h,pitch,mask.data(),mask.size(),pixels.data(),pixels.size())){ReleaseDC(nullptr,dc);return {};}
  }
  ReleaseDC(nullptr,dc);QByteArray result(16+pixels.size(),Qt::Uninitialized);auto d=reinterpret_cast<uint8_t*>(result.data());fr_put_u32(d,2);fr_put_u32(d+4,kind);fr_put_u32(d+8,w);fr_put_u32(d+12,rows);memcpy(d+16,pixels.data(),pixels.size());return result;
 }
#endif
 void poll(){
#ifdef Q_OS_WIN
  if(!m_layoutSample.isValid()||m_layoutSample.elapsed()>=250){DEVMODEW mode{};mode.dmSize=sizeof(mode);if(!EnumDisplaySettingsW(reinterpret_cast<const wchar_t*>(m_display.utf16()),ENUM_CURRENT_SETTINGS,&mode))return;
  m_bounds={mode.dmPosition.x,mode.dmPosition.y,mode.dmPosition.x+LONG(mode.dmPelsWidth),mode.dmPosition.y+LONG(mode.dmPelsHeight)};m_layoutSample.start();}
  CURSORINFO ci{};ci.cbSize=sizeof(ci);if(!GetCursorInfo(&ci))return;
  if(m_handle!=ci.hCursor){m_handle=ci.hCursor;ICONINFO icon{};if(GetIconInfo(ci.hCursor,&icon)){m_hotspot={LONG(icon.xHotspot),LONG(icon.yHotspot)};auto shape=nativeShape(icon);if(!shape.isEmpty())setShape(shape);if(icon.hbmColor)DeleteObject(icon.hbmColor);if(icon.hbmMask)DeleteObject(icon.hbmMask);}}
  const bool show=(ci.flags&CURSOR_SHOWING)&&PtInRect(&m_bounds,ci.ptScreenPos);
  int x=show?ci.ptScreenPos.x-m_bounds.left-m_hotspot.x:0,y=show?ci.ptScreenPos.y-m_bounds.top-m_hotspot.y:0;
  auto state=fr_cursor_position(qMax(-512,x),qMax(-512,y),m_bounds.right-m_bounds.left,m_bounds.bottom-m_bounds.top,show);
  if(state!=m_last||m_sample.elapsed()>1000){m_last=state;m_sample.restart();m_send(state);}
#endif
 }
public:
 std::function<void()> shapeChanged;
 DesktopCursorStream(QObject *owner,std::function<void(const QByteArray&)> send):m_send(std::move(send)){m_tick.setTimerType(Qt::PreciseTimer);m_tick.setInterval(8);QObject::connect(&m_tick,&QTimer::timeout,owner,[this]{poll();});m_sample.start();}
 void setDisplay(const QString &display){m_display=display;m_layoutSample.invalidate();m_last.clear();if(m_enabled)poll();}
 void setShape(const QByteArray &shape){if(shape==m_shape)return;m_shape=shape;if(m_enabled)m_send(shape);if(shapeChanged)shapeChanged();}
 bool hasShape()const{return !m_shape.isEmpty();}
 void setEnabled(bool enabled){if(m_enabled==enabled)return;m_enabled=enabled;m_last.clear();if(enabled){if(!m_shape.isEmpty())m_send(m_shape);m_tick.start();poll();}else{m_tick.stop();m_send(fr_cursor_position(0,0,1,1,false));}}
 void stop(){setEnabled(false);m_display.clear();m_shape.clear();m_last.clear();
#ifdef Q_OS_WIN
 m_handle=nullptr;
#endif
 }
};
