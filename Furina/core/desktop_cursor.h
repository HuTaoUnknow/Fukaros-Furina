#ifndef FURINA_DESKTOP_CURSOR_H
#define FURINA_DESKTOP_CURSOR_H
#ifdef _WIN32
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi1_2.h>
#include <vector>
#include <algorithm>
#include <cmath>
#include <cstring>
#include "cursor_shape.h"
template<class T> struct CursorCom {
 T *p=nullptr;~CursorCom(){clear();}void clear(){if(p)p->Release();p=nullptr;}
 T **put(){clear();return &p;}T *operator->()const{return p;}
};
/* Cursor composition stays on the capture device. Only changed, small pointer
   bitmaps cross the CPU/GPU boundary; desktop pixels never leave the GPU. */
class DesktopCursor {
 CursorCom<ID3D11Texture2D> texture,background;
 CursorCom<ID3D11ShaderResourceView> shapeView,backgroundView;
 CursorCom<ID3D11RenderTargetView> targetView;
 CursorCom<ID3D11VertexShader> vs;CursorCom<ID3D11PixelShader> ps;
 CursorCom<ID3D11Buffer> constants;CursorCom<ID3D11RasterizerState> raster;
 CursorCom<ID3D11BlendState> alphaBlend;
 ID3D11Texture2D *target=nullptr;
 unsigned type=0;int width=0,height=0,x=0,y=0;bool visible=false;
 std::vector<uint8_t> pixels,raw;
 bool initialize(ID3D11Device *device){
  if(ps.p&&constants.p&&raster.p&&alphaBlend.p)return true;
  const char *shader=R"(
cbuffer Cursor : register(b0) { float4 rect; float4 mode; };
Texture2D<float4> shape : register(t0);
Texture2D<float4> background : register(t1);
float4 vertex(uint id:SV_VertexID):SV_POSITION {
 return float4(id==2?3:-1,id==1?3:-1,0,1);
}
float4 pixel(float4 pos:SV_POSITION):SV_TARGET {
 float2 uv=(pos.xy-rect.xy)*rect.zw;
 uint w,h;shape.GetDimensions(w,h);
 if(any(uv<0)||uv.x>=w||uv.y>=h)discard;
 float4 p=shape.Load(int3(int2(uv),0));
 if(mode.x==2)return p;
 float3 b=background.Load(int3(int2(pos.xy),0)).rgb;
 uint3 bi=(uint3)round(saturate(b)*255),pi=(uint3)round(saturate(p.rgb)*255);
 if(mode.x==1) return float4(float3((bi & pi.r) ^ pi.g)/255,1);
 if(mode.x==4) return float4(p.a>0.5?float3(bi ^ pi)/255:p.rgb,1);
 return float4(b,1);
})";
  CursorCom<ID3DBlob> vertex,pixel,errors;
  if(FAILED(D3DCompile(shader,strlen(shader),nullptr,nullptr,nullptr,"vertex","vs_4_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,vertex.put(),errors.put()))||
     FAILED(D3DCompile(shader,strlen(shader),nullptr,nullptr,nullptr,"pixel","ps_4_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,pixel.put(),errors.put())))return false;
  if(FAILED(device->CreateVertexShader(vertex->GetBufferPointer(),vertex->GetBufferSize(),nullptr,vs.put()))||
     FAILED(device->CreatePixelShader(pixel->GetBufferPointer(),pixel->GetBufferSize(),nullptr,ps.put())))return false;
  D3D11_BUFFER_DESC bd{};bd.ByteWidth=32;bd.Usage=D3D11_USAGE_DEFAULT;bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
  D3D11_RASTERIZER_DESC rd{};rd.FillMode=D3D11_FILL_SOLID;rd.CullMode=D3D11_CULL_NONE;rd.DepthClipEnable=TRUE;
  D3D11_BLEND_DESC blend{};auto &rt=blend.RenderTarget[0];rt.BlendEnable=TRUE;
  rt.SrcBlend=D3D11_BLEND_SRC_ALPHA;rt.DestBlend=D3D11_BLEND_INV_SRC_ALPHA;rt.BlendOp=D3D11_BLEND_OP_ADD;
  rt.SrcBlendAlpha=D3D11_BLEND_ZERO;rt.DestBlendAlpha=D3D11_BLEND_ONE;rt.BlendOpAlpha=D3D11_BLEND_OP_ADD;rt.RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;
  return SUCCEEDED(device->CreateBuffer(&bd,nullptr,constants.put()))&&SUCCEEDED(device->CreateRasterizerState(&rd,raster.put()))&&SUCCEEDED(device->CreateBlendState(&blend,alphaBlend.put()));
 }
public:
 void setPosition(int px,int py,bool show){x=px;y=py;visible=show;}
 bool setShape(ID3D11Device *device,unsigned kind,unsigned w,unsigned h,unsigned pitch,const uint8_t *data,size_t size){
  const unsigned rows=kind==FR_CURSOR_MONO?h/2:h;
  if(w>512||h>1024||!w||!rows)return false;
  std::vector<uint8_t> next((size_t)w*rows*4);
  if(!fr_cursor_unpack(kind,w,h,pitch,data,size,next.data(),next.size()))return false;
  D3D11_TEXTURE2D_DESC desc{};desc.Width=w;desc.Height=rows;desc.MipLevels=1;desc.ArraySize=1;desc.Format=DXGI_FORMAT_B8G8R8A8_UNORM;
  desc.SampleDesc.Count=1;desc.Usage=D3D11_USAGE_IMMUTABLE;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
  D3D11_SUBRESOURCE_DATA initial{next.data(),w*4,0};
  shapeView.clear();texture.clear();
  if(FAILED(device->CreateTexture2D(&desc,&initial,texture.put()))||FAILED(device->CreateShaderResourceView(texture.p,nullptr,shapeView.put())))return false;
  pixels.swap(next);type=kind;width=w;height=rows;return true;
 }
 bool update(ID3D11Device *device,IDXGIOutputDuplication *dup,const DXGI_OUTDUPL_FRAME_INFO &info){
  if(info.LastMouseUpdateTime.QuadPart)setPosition(info.PointerPosition.Position.x,info.PointerPosition.Position.y,info.PointerPosition.Visible);
  if(!info.PointerShapeBufferSize)return true;
  if(info.PointerShapeBufferSize>4*512*1024)return false;
  raw.resize(info.PointerShapeBufferSize);UINT needed=0;DXGI_OUTDUPL_POINTER_SHAPE_INFO shape{};
  if(FAILED(dup->GetFramePointerShape((UINT)raw.size(),raw.data(),&needed,&shape)))return false;
  // DXGI position is already the top-left corner; subtracting HotSpot would offset it twice.
  return setShape(device,shape.Type,shape.Width,shape.Height,shape.Pitch,raw.data(),raw.size());
 }
 bool draw(ID3D11Device *device,ID3D11DeviceContext *context,ID3D11Texture2D *output,int sw,int sh,int w,int h){
  if(!visible||!shapeView.p)return true;
  float left=float(x)*w/sw,top=float(y)*h/sh,right=float(x+width)*w/sw,bottom=float(y+height)*h/sh;
  int l=std::max(0,int(std::floor(left))),t=std::max(0,int(std::floor(top)));
  int r=std::min(w,int(std::ceil(right))),b=std::min(h,int(std::ceil(bottom)));
  if(l>=r||t>=b)return true;
  if(!initialize(device))return false;
  if(target!=output){
   target=nullptr;targetView.clear();backgroundView.clear();background.clear();
   D3D11_TEXTURE2D_DESC desc{};output->GetDesc(&desc);desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;desc.MiscFlags=0;
   if(FAILED(device->CreateTexture2D(&desc,nullptr,background.put()))||FAILED(device->CreateShaderResourceView(background.p,nullptr,backgroundView.put()))||
      FAILED(device->CreateRenderTargetView(output,nullptr,targetView.put())))return false;
   target=output;
  }
  // Copy after the scaler rewrites output, so moving/hiding a cursor cannot leave trails.
  // Sampling this separate texture also supports monochrome and masked-color XOR cursors.
  // Color cursors blend directly into the target; only XOR cursors need a desktop copy.
  if(type!=FR_CURSOR_COLOR)context->CopyResource(background.p,output);
  float params[8]={left,top,float(sw)/w,float(sh)/h,float(type),0,0,0};
  context->UpdateSubresource(constants.p,0,nullptr,params,0,0);
  D3D11_VIEWPORT viewport{float(l),float(t),float(r-l),float(b-t),0,1};
  context->RSSetViewports(1,&viewport);context->RSSetState(raster.p);
  context->OMSetRenderTargets(1,&targetView.p,nullptr);context->OMSetBlendState(type==FR_CURSOR_COLOR?alphaBlend.p:nullptr,nullptr,0xffffffff);
  context->OMSetDepthStencilState(nullptr,0);context->IASetInputLayout(nullptr);
  context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  context->VSSetShader(vs.p,nullptr,0);context->GSSetShader(nullptr,nullptr,0);context->PSSetShader(ps.p,nullptr,0);
  context->PSSetConstantBuffers(0,1,&constants.p);
  ID3D11ShaderResourceView *views[]={shapeView.p,type==FR_CURSOR_COLOR?nullptr:backgroundView.p};context->PSSetShaderResources(0,2,views);context->Draw(3,0);
  ID3D11ShaderResourceView *empty[2]={};context->PSSetShaderResources(0,2,empty);context->OMSetRenderTargets(0,nullptr,nullptr);
  context->OMSetBlendState(nullptr,nullptr,0xffffffff);context->RSSetState(nullptr);
  context->VSSetShader(nullptr,nullptr,0);context->PSSetShader(nullptr,nullptr,0);
  ID3D11Buffer *noConstants=nullptr;context->PSSetConstantBuffers(0,1,&noConstants);
  return true;
 }
 void drawCpu(uint8_t *image,int w,int h,size_t stride)const{
  if(visible&&!pixels.empty())fr_cursor_composite(type,pixels.data(),width,height,image,w,h,stride,x,y);
 }
};
#endif
#endif
