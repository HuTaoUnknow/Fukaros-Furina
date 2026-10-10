#include "desktop_cursor.h"
#include <cstdio>
#include <cstdlib>
#include <vector>
#define CHECK(v) do{if(!(v)){fprintf(stderr,"cursor test failed at %d: %s\n",__LINE__,#v);return 1;}}while(0)
int main(){
 CursorCom<ID3D11Device> device;CursorCom<ID3D11DeviceContext> context;
 CHECK(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,nullptr,0,D3D11_SDK_VERSION,device.put(),nullptr,context.put())));
 DesktopCursor cursor;
 uint8_t invalid[4]={},small[4]={};
 CHECK(!fr_cursor_unpack(FR_CURSOR_MONO,8,3,1,invalid,4,small,4));
 CHECK(!fr_cursor_unpack(FR_CURSOR_COLOR,512,512,0xffffffff,invalid,4,small,4));
 CHECK(!fr_cursor_unpack(99,1,1,4,invalid,4,small,4));
 int cases=0;
 for(unsigned type:{FR_CURSOR_COLOR,FR_CURSOR_MONO,FR_CURSOR_MASKED}){
  unsigned cw=type==FR_CURSOR_MONO?10:4,ch=2,pitch=type==FR_CURSOR_MONO?2:20,ih=type==FR_CURSOR_MONO?4:ch;
  std::vector<uint8_t> raw(pitch*ih,0xdd),shape(cw*ch*4);
  if(type==FR_CURSOR_MONO){raw={0x33,0xff,0x33,0xff,0x55,0xff,0x55,0xff};}
  else for(unsigned row=0;row<ch;++row)for(unsigned col=0;col<cw;++col){uint8_t *p=raw.data()+row*pitch+col*4;p[0]=25+col*30;p[1]=195-row*50;p[2]=35+col*60;p[3]=type==FR_CURSOR_COLOR?uint8_t(col*85):uint8_t((col&1)?255:0);}
  CHECK(fr_cursor_unpack(type,cw,ih,pitch,raw.data(),raw.size(),shape.data(),shape.size()));
  CHECK(cursor.setShape(device.p,type,cw,ih,pitch,raw.data(),raw.size()));
  for(int scale:{1,2}){
   const int w=20,h=12,sw=w*scale,sh=h*scale;
   std::vector<uint8_t> base(w*h*4),actual(w*h*4);
   for(int i=0;i<w*h;++i){base[i*4]=i%201;base[i*4+1]=123;base[i*4+2]=234;base[i*4+3]=255;}
   D3D11_TEXTURE2D_DESC desc{};desc.Width=w;desc.Height=h;desc.MipLevels=1;desc.ArraySize=1;desc.Format=DXGI_FORMAT_B8G8R8A8_UNORM;desc.SampleDesc.Count=1;desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=D3D11_BIND_RENDER_TARGET;
   CursorCom<ID3D11Texture2D> output,readback;CHECK(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,output.put())));
   desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;CHECK(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,readback.put())));
   for(int test=0;test<6;++test){
    const int x=test==1?-3:test==2?sw-2:4+test*2,y=test==3?sh-1:test==4?-1:4;
    const bool visible=test!=5;
    context->UpdateSubresource(output.p,0,nullptr,base.data(),w*4,0);
    cursor.setPosition(x,y,visible);CHECK(cursor.draw(device.p,context.p,output.p,sw,sh,w,h));
    context->CopyResource(readback.p,output.p);D3D11_MAPPED_SUBRESOURCE map{};
    CHECK(SUCCEEDED(context->Map(readback.p,0,D3D11_MAP_READ,0,&map)));
    for(int row=0;row<h;++row)memcpy(actual.data()+row*w*4,(uint8_t*)map.pData+row*map.RowPitch,w*4);
    context->Unmap(readback.p,0);
    auto expected=base;
    for(int row=0;row<h;++row)for(int col=0;col<w;++col){
     const float cx=(col+0.5f)*scale-x,cy=(row+0.5f)*scale-y;
     if(visible&&cx>=0&&cy>=0&&cx<cw&&cy<ch)fr_cursor_pixel(type,shape.data()+((int)cy*cw+(int)cx)*4,expected.data()+(row*w+col)*4);
    }
    for(size_t i=0;i<actual.size();++i){if(abs(int(actual[i])-int(expected[i]))>1){fprintf(stderr,"type %u scale %d case %d byte %zu actual %u expected %u\n",type,scale,test,i,actual[i],expected[i]);return 1;}}
    if(scale==1){auto cpu=base;if(visible)fr_cursor_composite(type,shape.data(),cw,ch,cpu.data(),w,h,w*4,x,y);CHECK(cpu==expected);}
    ++cases;
   }
  }
 }
 printf("%d GPU/CPU cursor cases passed: alpha, AND/XOR, masked color, clipping, scaling, moving and hiding.\n",cases);
 return 0;
}
