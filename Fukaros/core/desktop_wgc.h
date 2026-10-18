#pragma once
#ifdef _WIN32
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <inspectable.h>
#include <roapi.h>
#include <winstring.h>
// Minimal WinRT ABI declarations from Windows SDK windows.graphics.capture.h.
// Kept local because the bundled MinGW SDK omits the frame-pool interfaces.
namespace FurinaWgc {
struct Size {INT32 width,height;};
struct Session:IInspectable{virtual HRESULT STDMETHODCALLTYPE StartCapture()=0;};
struct CursorSession:IInspectable{virtual HRESULT STDMETHODCALLTYPE get_Enabled(boolean*)=0;virtual HRESULT STDMETHODCALLTYPE put_Enabled(boolean)=0;};
struct Item:IInspectable{virtual HRESULT STDMETHODCALLTYPE get_Name(HSTRING*)=0;virtual HRESULT STDMETHODCALLTYPE get_Size(Size*)=0;};
struct Frame:IInspectable{virtual HRESULT STDMETHODCALLTYPE get_Surface(IInspectable**)=0;virtual HRESULT STDMETHODCALLTYPE get_Time(INT64*)=0;virtual HRESULT STDMETHODCALLTYPE get_Size(Size*)=0;};
struct Pool:IInspectable{virtual HRESULT STDMETHODCALLTYPE Recreate(IInspectable*,INT32,INT32,Size)=0;virtual HRESULT STDMETHODCALLTYPE Next(Frame**)=0;virtual HRESULT STDMETHODCALLTYPE Add(IUnknown*,INT64*)=0;virtual HRESULT STDMETHODCALLTYPE Remove(INT64)=0;virtual HRESULT STDMETHODCALLTYPE SessionFor(Item*,Session**)=0;};
struct PoolFactory:IInspectable{virtual HRESULT STDMETHODCALLTYPE Create(IInspectable*,INT32,INT32,Size,Pool**)=0;};
struct ItemInterop:IUnknown{virtual HRESULT STDMETHODCALLTYPE Window(HWND,REFIID,void**)=0;virtual HRESULT STDMETHODCALLTYPE Monitor(HMONITOR,REFIID,void**)=0;};
struct DxgiAccess:IUnknown{virtual HRESULT STDMETHODCALLTYPE GetInterface(REFIID,void**)=0;};
struct Closable:IInspectable{virtual HRESULT STDMETHODCALLTYPE Close()=0;};
inline constexpr GUID itemId{0x79c3f95b,0x31f7,0x4ec2,{0xa4,0x64,0x63,0x2e,0xf5,0xd3,0x07,0x60}};
inline constexpr GUID interopId{0x3628e81b,0x3cac,0x4c60,{0xb7,0xf4,0x23,0xce,0x0e,0x0c,0x33,0x56}};
inline constexpr GUID factoryId{0x589b103f,0x6bbc,0x5df5,{0xa9,0x91,0x02,0xe2,0x8b,0x3b,0x66,0xd5}};
inline constexpr GUID cursorId{0x2c39ae40,0x7d2e,0x5044,{0x80,0x4e,0x8b,0x67,0x99,0xd4,0xcf,0x9e}};
inline constexpr GUID accessId{0xa9b3d012,0x3df2,0x4ee3,{0xb8,0xd1,0x86,0x95,0xf4,0x57,0xd3,0xc1}};
inline constexpr GUID closeId{0x30d5a829,0x7fa4,0x4026,{0x83,0xbb,0xd7,0x5b,0xae,0x4e,0xa9,0x9e}};
template<class T>struct Ptr{T*p=nullptr;~Ptr(){reset();}void reset(){if(p)p->Release();p=nullptr;}T**put(){reset();return &p;}T*operator->()const{return p;}};
inline void closeObject(IInspectable *p){if(!p)return;Ptr<Closable> c;if(SUCCEEDED(p->QueryInterface(closeId,reinterpret_cast<void**>(c.put()))))c->Close();}
class Capture {
 Ptr<Pool> pool;Ptr<Session> session;Ptr<ID3D11Texture2D> copy;bool initialized=false;
 static HRESULT factory(const wchar_t *name,REFIID iid,void **out){HSTRING text=nullptr;auto hr=WindowsCreateString(name,UINT32(wcslen(name)),&text);if(SUCCEEDED(hr))hr=RoGetActivationFactory(text,iid,out);WindowsDeleteString(text);return hr;}
public:
 QString error;int width=0,height=0;
 ~Capture(){close();if(initialized)RoUninitialize();}
 bool opened()const{return session.p!=nullptr;}
 void close(){closeObject(session.p);session.reset();closeObject(pool.p);pool.reset();copy.reset();}
 bool open(ID3D11Device *device,HMONITOR monitor){
  if(opened())return true;if(!initialized){auto hr=RoInitialize(RO_INIT_MULTITHREADED);if(FAILED(hr)){error="WinRT initialization";return false;}initialized=true;}
  Ptr<ItemInterop> interop;Ptr<Item> item;Ptr<PoolFactory> statics;Ptr<IDXGIDevice> dxgi;Ptr<IInspectable> rtDevice;
  HRESULT hr=factory(L"Windows.Graphics.Capture.GraphicsCaptureItem",interopId,reinterpret_cast<void**>(interop.put()));
  if(SUCCEEDED(hr))hr=interop->Monitor(monitor,itemId,reinterpret_cast<void**>(item.put()));Size size{};if(SUCCEEDED(hr))hr=item->get_Size(&size);
  if(SUCCEEDED(hr))hr=factory(L"Windows.Graphics.Capture.Direct3D11CaptureFramePool",factoryId,reinterpret_cast<void**>(statics.put()));
  if(SUCCEEDED(hr))hr=device->QueryInterface(__uuidof(IDXGIDevice),reinterpret_cast<void**>(dxgi.put()));
  using CreateDevice=HRESULT(WINAPI*)(IDXGIDevice*,IInspectable**);auto create=reinterpret_cast<CreateDevice>(GetProcAddress(GetModuleHandleW(L"d3d11.dll"),"CreateDirect3D11DeviceFromDXGIDevice"));
  if(SUCCEEDED(hr))hr=create?create(dxgi.p,rtDevice.put()):E_NOINTERFACE;
  if(SUCCEEDED(hr))hr=statics->Create(rtDevice.p,87,3,size,pool.put());
  if(SUCCEEDED(hr))hr=pool->SessionFor(item.p,session.put());Ptr<CursorSession> cursor;
  if(SUCCEEDED(hr))hr=session->QueryInterface(cursorId,reinterpret_cast<void**>(cursor.put()));
  if(SUCCEEDED(hr))hr=cursor->put_Enabled(false);
  if(SUCCEEDED(hr))hr=session->StartCapture();
  if(FAILED(hr)){error=QString("WGC HRESULT %1").arg(quint32(hr),8,16,QChar('0'));close();return false;}
  width=size.width;height=size.height;return true;
 }
 bool next(ID3D11Device *device,ID3D11DeviceContext *context,ID3D11Texture2D **out){
  *out=nullptr;for(int i=0;i<3;++i){Ptr<Frame> frame;auto hr=pool->Next(frame.put());if(FAILED(hr)){error="WGC frame acquisition";return false;}if(!frame.p)break;
   Ptr<IInspectable> surface;Ptr<DxgiAccess> access;Size size{};hr=frame->get_Size(&size);
   if(SUCCEEDED(hr)&&(size.width!=width||size.height!=height))hr=E_PENDING;
   if(SUCCEEDED(hr))hr=frame->get_Surface(surface.put());if(SUCCEEDED(hr))hr=surface->QueryInterface(accessId,reinterpret_cast<void**>(access.put()));
   ID3D11Texture2D *texture=nullptr;if(SUCCEEDED(hr))hr=access->GetInterface(__uuidof(ID3D11Texture2D),reinterpret_cast<void**>(&texture));
   if(SUCCEEDED(hr)){if(!copy.p){D3D11_TEXTURE2D_DESC desc{};texture->GetDesc(&desc);desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET;desc.MiscFlags=0;desc.CPUAccessFlags=0;hr=device->CreateTexture2D(&desc,nullptr,copy.put());}if(SUCCEEDED(hr))context->CopyResource(copy.p,texture);}
   if(texture)texture->Release();closeObject(frame.p);if(FAILED(hr)){error="WGC display changed";return false;}
  }if(copy.p){copy->AddRef();*out=copy.p;}return true;
 }
};
}
#endif
