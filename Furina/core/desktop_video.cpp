#include "desktop_video.h"
#include "video_policy.h"
#include "interface_support.h"
#include <QCoreApplication>
#include <QLibrary>
#include <QElapsedTimer>
#include <QBuffer>
#include <chrono>
#include <vector>
#ifdef Q_OS_WIN
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <mmsystem.h>
#include "desktop_cursor.h"
#include "vendor/nvcodec/nvEncodeAPI.h"
extern "C" {
#include "vendor/ffmpeg/libavcodec/avcodec.h"
#include "vendor/ffmpeg/libavutil/opt.h"
#include "vendor/ffmpeg/libavutil/hwcontext.h"
#include "vendor/ffmpeg/libswscale/swscale.h"
}
namespace {
#define CODEC_FUNCTIONS(X) X(avcodec_version) X(avcodec_find_encoder_by_name) X(avcodec_find_decoder_by_name) X(avcodec_alloc_context3) X(avcodec_open2) X(avcodec_free_context) X(avcodec_send_frame) X(avcodec_receive_packet) X(avcodec_send_packet) X(avcodec_receive_frame) X(avcodec_flush_buffers) X(av_packet_alloc) X(av_packet_free) X(av_packet_unref) X(av_new_packet)
#define UTIL_FUNCTIONS(X) X(av_frame_alloc) X(av_frame_free) X(av_frame_unref) X(av_frame_get_buffer) X(av_frame_make_writable) X(av_opt_set) X(av_opt_set_int) X(av_strerror) X(av_buffer_unref) X(av_buffer_ref) X(av_hwdevice_ctx_create) X(av_hwframe_transfer_data)
#define SCALE_FUNCTIONS(X) X(sws_getCachedContext) X(sws_scale) X(sws_freeContext) X(sws_getCoefficients) X(sws_setColorspaceDetails)
struct Api {
 QLibrary codec,util,scale;QString failure;
#define DECLARE(name) decltype(&::name) name=nullptr;
 CODEC_FUNCTIONS(DECLARE) UTIL_FUNCTIONS(DECLARE) SCALE_FUNCTIONS(DECLARE)
#undef DECLARE
 Api(){
  auto load=[](QLibrary &lib,const QString &name){lib.setFileName(QCoreApplication::applicationDirPath()+"/"+name);if(lib.load())return true;lib.setFileName(name);return lib.load();};
  if(!load(util,"avutil-59")||!load(codec,"avcodec-61")||!load(scale,"swscale-8")){failure="FFmpeg 7.1 DLL missing: "+codec.errorString();return;}
#define LOAD_CODEC(name) name=reinterpret_cast<decltype(name)>(codec.resolve(#name));if(!name){failure=#name;return;}
#define LOAD_UTIL(name) name=reinterpret_cast<decltype(name)>(util.resolve(#name));if(!name){failure=#name;return;}
#define LOAD_SCALE(name) name=reinterpret_cast<decltype(name)>(scale.resolve(#name));if(!name){failure=#name;return;}
  CODEC_FUNCTIONS(LOAD_CODEC) UTIL_FUNCTIONS(LOAD_UTIL) SCALE_FUNCTIONS(LOAD_SCALE)
#undef LOAD_CODEC
#undef LOAD_UTIL
#undef LOAD_SCALE
  if((avcodec_version()>>16)!=61)failure="Incompatible avcodec ABI; requires major 61";
 }
 QString error(int code){char buffer[256]{};av_strerror(code,buffer,sizeof buffer);return QString::fromUtf8(buffer);}
};
Api &api(){static Api value;return value;}
template<class T> struct Com {
 T *p=nullptr;~Com(){clear();}void clear(){if(p)p->Release();p=nullptr;}T **put(){clear();return &p;}T *operator->()const{return p;}operator bool()const{return p;}
};
class Capture {
 Com<ID3D11Device> device;Com<ID3D11DeviceContext> context;Com<IDXGIOutputDuplication> duplicate;Com<ID3D11Texture2D> staging;
 Com<ID3D11Texture2D> sourceTexture,scaledTexture;
 Com<ID3D11VideoDevice> videoDevice;Com<ID3D11VideoContext> videoContext;
 Com<ID3D11VideoProcessorEnumerator> enumerator;Com<ID3D11VideoProcessor> processor;
 Com<ID3D11VideoProcessorInputView> inputView;Com<ID3D11VideoProcessorOutputView> outputView;
 int sourceW=0,sourceH=0,scaledW=0,scaledH=0;
 QImage latest;
 DesktopCursor cursor;
 QImage cpuFrame(){if(latest.isNull())return {};QImage frame=latest.copy();cursor.drawCpu(frame.bits(),frame.width(),frame.height(),frame.bytesPerLine());return frame;}
public:
 QString error;
 bool recoverable=false;
 ID3D11Device *gpuDevice()const{return device.p;}
 QSize sourceSize()const{return QSize(sourceW,sourceH);}
 bool open(const QString &name){
  Com<IDXGIFactory1> factory;if(FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1),reinterpret_cast<void**>(factory.put())))){error="DXGI factory";return false;}
  for(UINT a=0;;++a){Com<IDXGIAdapter1> adapter;const auto adapterResult=factory->EnumAdapters1(a,adapter.put());if(adapterResult==DXGI_ERROR_NOT_FOUND)break;if(FAILED(adapterResult)){error="DXGI adapter enumeration failed";return false;}
   for(UINT o=0;;++o){Com<IDXGIOutput> output;const auto outputResult=adapter->EnumOutputs(o,output.put());if(outputResult==DXGI_ERROR_NOT_FOUND)break;if(FAILED(outputResult)){error="DXGI output enumeration failed";return false;}
    DXGI_OUTPUT_DESC desc{};if(FAILED(output->GetDesc(&desc)))continue;if(!desc.AttachedToDesktop||QString::fromWCharArray(desc.DeviceName)!=name)continue;
    if(FAILED(D3D11CreateDevice(adapter.p,D3D_DRIVER_TYPE_UNKNOWN,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,nullptr,0,D3D11_SDK_VERSION,device.put(),nullptr,context.put()))){error="D3D11 device";return false;}
    Com<IDXGIOutput1> modern;if(FAILED(output->QueryInterface(__uuidof(IDXGIOutput1),reinterpret_cast<void**>(modern.put())))){error="DXGI output";return false;}
    const auto hr=modern->DuplicateOutput(device.p,duplicate.put());if(FAILED(hr)){error=QString("DuplicateOutput HRESULT %1").arg(quint32(hr),8,16,QChar('0'));return false;}DXGI_OUTDUPL_DESC d{};duplicate->GetDesc(&d);sourceW=d.ModeDesc.Width;sourceH=d.ModeDesc.Height;return true;
   }
  }error="Display not found: "+name;return false;
 }
 ID3D11Texture2D *gpuFrame(int w,int h){
  DXGI_OUTDUPL_FRAME_INFO info{};Com<IDXGIResource> resource;auto hr=duplicate->AcquireNextFrame(0,&info,resource.put());
  if(SUCCEEDED(hr)){
   const bool pointerOk=cursor.update(device.p,duplicate.p,info);
   Com<ID3D11Texture2D> texture;resource->QueryInterface(__uuidof(ID3D11Texture2D),reinterpret_cast<void**>(texture.put()));
   if(texture){D3D11_TEXTURE2D_DESC desc{};texture->GetDesc(&desc);if(!sourceTexture){desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET;desc.MiscFlags=0;desc.CPUAccessFlags=0;device->CreateTexture2D(&desc,nullptr,sourceTexture.put());}if(sourceTexture)context->CopyResource(sourceTexture.p,texture.p);}
   duplicate->ReleaseFrame();
   if(!pointerOk){recoverable=true;error="DXGI pointer shape changed during capture";return nullptr;}
  }else if(hr!=DXGI_ERROR_WAIT_TIMEOUT){recoverable=hr==DXGI_ERROR_ACCESS_LOST||hr==DXGI_ERROR_DEVICE_REMOVED||hr==DXGI_ERROR_DEVICE_RESET;error=QString("DXGI capture HRESULT %1").arg(quint32(hr),8,16,QChar('0'));return nullptr;}
  if(!sourceTexture)return nullptr;
  if(scaledW!=w||scaledH!=h){
   outputView.clear();inputView.clear();processor.clear();enumerator.clear();scaledTexture.clear();
   device->QueryInterface(__uuidof(ID3D11VideoDevice),reinterpret_cast<void**>(videoDevice.put()));context->QueryInterface(__uuidof(ID3D11VideoContext),reinterpret_cast<void**>(videoContext.put()));if(!videoDevice||!videoContext){error="GPU video processor unavailable";return nullptr;}
   D3D11_VIDEO_PROCESSOR_CONTENT_DESC content{};content.InputFrameFormat=D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;content.InputWidth=sourceW;content.InputHeight=sourceH;content.OutputWidth=w;content.OutputHeight=h;content.InputFrameRate={60,1};content.OutputFrameRate={60,1};content.Usage=D3D11_VIDEO_USAGE_PLAYBACK_NORMAL;
   if(FAILED(videoDevice->CreateVideoProcessorEnumerator(&content,enumerator.put()))||FAILED(videoDevice->CreateVideoProcessor(enumerator.p,0,processor.put()))){error="GPU scaler initialization failed";return nullptr;}
   D3D11_TEXTURE2D_DESC desc{};desc.Width=w;desc.Height=h;desc.MipLevels=1;desc.ArraySize=1;desc.Format=DXGI_FORMAT_B8G8R8A8_UNORM;desc.SampleDesc.Count=1;desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=D3D11_BIND_RENDER_TARGET;
   if(FAILED(device->CreateTexture2D(&desc,nullptr,scaledTexture.put()))){error="GPU scaler texture failed";return nullptr;}
   D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC input{};input.ViewDimension=D3D11_VPIV_DIMENSION_TEXTURE2D;
   D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC output{};output.ViewDimension=D3D11_VPOV_DIMENSION_TEXTURE2D;
   if(FAILED(videoDevice->CreateVideoProcessorInputView(sourceTexture.p,enumerator.p,&input,inputView.put()))||FAILED(videoDevice->CreateVideoProcessorOutputView(scaledTexture.p,enumerator.p,&output,outputView.put()))){error="GPU scaler views failed";return nullptr;}
   RECT source{0,0,sourceW,sourceH},dest{0,0,w,h};videoContext->VideoProcessorSetStreamFrameFormat(processor.p,0,D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE);videoContext->VideoProcessorSetStreamSourceRect(processor.p,0,TRUE,&source);videoContext->VideoProcessorSetStreamDestRect(processor.p,0,TRUE,&dest);videoContext->VideoProcessorSetOutputTargetRect(processor.p,TRUE,&dest);
   scaledW=w;scaledH=h;
  }
  D3D11_VIDEO_PROCESSOR_STREAM stream{};stream.Enable=TRUE;stream.pInputSurface=inputView.p;
  if(FAILED(videoContext->VideoProcessorBlt(processor.p,outputView.p,0,1,&stream))){error="GPU scaling failed";return nullptr;}
  if(!cursor.draw(device.p,context.p,scaledTexture.p,sourceW,sourceH,w,h)){error="GPU cursor composition failed";return nullptr;}
  context->Flush();return scaledTexture.p;
 }
 QImage grab(){
  DXGI_OUTDUPL_FRAME_INFO info{};Com<IDXGIResource> resource;const auto hr=duplicate->AcquireNextFrame(0,&info,resource.put());
  if(hr==DXGI_ERROR_WAIT_TIMEOUT)return cpuFrame();
  if(FAILED(hr)){recoverable=hr==DXGI_ERROR_ACCESS_LOST||hr==DXGI_ERROR_DEVICE_REMOVED||hr==DXGI_ERROR_DEVICE_RESET;error=QString("Desktop capture HRESULT %1").arg(quint32(hr),8,16,QChar('0'));return {};}
  if(!cursor.update(device.p,duplicate.p,info)){duplicate->ReleaseFrame();recoverable=true;error="DXGI pointer shape changed during capture";return {};}
  Com<ID3D11Texture2D> texture;resource->QueryInterface(__uuidof(ID3D11Texture2D),reinterpret_cast<void**>(texture.put()));
  if(texture){D3D11_TEXTURE2D_DESC desc{};texture->GetDesc(&desc);
   if(!staging){desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.MiscFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;device->CreateTexture2D(&desc,nullptr,staging.put());}
   if(staging){context->CopyResource(staging.p,texture.p);D3D11_MAPPED_SUBRESOURCE mapped{};
    if(SUCCEEDED(context->Map(staging.p,0,D3D11_MAP_READ,0,&mapped))){
     if(latest.size()!=QSize(desc.Width,desc.Height))latest=QImage(desc.Width,desc.Height,QImage::Format_RGB32);
     for(int y=0;y<latest.height();++y)memcpy(latest.scanLine(y),static_cast<const char*>(mapped.pData)+y*mapped.RowPitch,latest.width()*4);
     context->Unmap(staging.p,0);
    }
   }
  }duplicate->ReleaseFrame();return cpuFrame();
 }
};
class NvEncoder {
 QLibrary library;NV_ENCODE_API_FUNCTION_LIST functions{};void *encoder=nullptr;NV_ENC_REGISTERED_PTR registered=nullptr;NV_ENC_OUTPUT_PTR output=nullptr;ID3D11Texture2D *input=nullptr;int width=0,height=0;uint64_t sequence=0;
public:
 int lastQp=0;QString error;~NvEncoder(){clear();}
 void clear(){if(encoder){if(registered)functions.nvEncUnregisterResource(encoder,registered);if(output)functions.nvEncDestroyBitstreamBuffer(encoder,output);functions.nvEncDestroyEncoder(encoder);}encoder=nullptr;registered=nullptr;output=nullptr;input=nullptr;}
 bool open(ID3D11Device *device,ID3D11Texture2D *texture,int w,int h){
  clear();wchar_t system[MAX_PATH]{};GetSystemDirectoryW(system,MAX_PATH);library.setFileName(QString::fromWCharArray(system)+"/nvEncodeAPI64.dll");if(!library.load()){error="NVENC driver API unavailable";return false;}
  auto create=reinterpret_cast<NVENCSTATUS(NVENCAPI*)(NV_ENCODE_API_FUNCTION_LIST*)>(library.resolve("NvEncodeAPICreateInstance"));functions.version=NV_ENCODE_API_FUNCTION_LIST_VER;if(!create||create(&functions)!=NV_ENC_SUCCESS){error="NVENC API version unavailable";return false;}
  NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS session{};session.version=NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS_VER;session.device=device;session.deviceType=NV_ENC_DEVICE_TYPE_DIRECTX;session.apiVersion=NVENCAPI_VERSION;
  if(functions.nvEncOpenEncodeSessionEx(&session,&encoder)!=NV_ENC_SUCCESS){error="NVENC session unavailable";return false;}
  NV_ENC_PRESET_CONFIG preset{};preset.version=NV_ENC_PRESET_CONFIG_VER;preset.presetCfg.version=NV_ENC_CONFIG_VER;
  if(functions.nvEncGetEncodePresetConfigEx(encoder,NV_ENC_CODEC_H264_GUID,NV_ENC_PRESET_P3_GUID,NV_ENC_TUNING_INFO_LOW_LATENCY,&preset)!=NV_ENC_SUCCESS){error="NVENC preset unavailable";clear();return false;}
  NV_ENC_CONFIG config=preset.presetCfg;config.version=NV_ENC_CONFIG_VER;config.profileGUID=NV_ENC_H264_PROFILE_BASELINE_GUID;config.gopLength=FR_VIDEO_GOP;config.frameIntervalP=1;
  // A one-frame CBR budget forced periodic IDRs to QP 50/51, blurring text.
  // Give intra frames a short burst budget and cap quantization, without B frames
  // or lookahead. VBV is a rate-control budget, not a queued playback delay.
  config.rcParams.rateControlMode=NV_ENC_PARAMS_RC_VBR;config.rcParams.averageBitRate=fr_video_bitrate(w,h);config.rcParams.maxBitRate=config.rcParams.averageBitRate*3/2;
  config.rcParams.vbvBufferSize=config.rcParams.averageBitRate/5;config.rcParams.vbvInitialDelay=config.rcParams.vbvBufferSize;
  config.rcParams.targetQuality=22;config.rcParams.enableMaxQP=1;config.rcParams.maxQP.qpIntra=28;config.rcParams.maxQP.qpInterP=30;config.rcParams.maxQP.qpInterB=30;config.rcParams.enableLookahead=0;config.rcParams.enableAQ=0;config.rcParams.zeroReorderDelay=1;config.encodeCodecConfig.h264Config.idrPeriod=FR_VIDEO_GOP;config.encodeCodecConfig.h264Config.repeatSPSPPS=1;
  // Declare the RGB -> YUV matrix so Android and PC decode the same colors.
  auto &vui=config.encodeCodecConfig.h264Config.h264VUIParameters;
  vui.videoSignalTypePresentFlag=1;vui.videoFormat=NV_ENC_VUI_VIDEO_FORMAT_UNSPECIFIED;vui.videoFullRangeFlag=0;vui.colourDescriptionPresentFlag=1;
  vui.colourPrimaries=NV_ENC_VUI_COLOR_PRIMARIES_BT709;vui.transferCharacteristics=NV_ENC_VUI_TRANSFER_CHARACTERISTIC_BT709;vui.colourMatrix=NV_ENC_VUI_MATRIX_COEFFS_BT709;

  NV_ENC_INITIALIZE_PARAMS init{};init.version=NV_ENC_INITIALIZE_PARAMS_VER;init.encodeGUID=NV_ENC_CODEC_H264_GUID;init.presetGUID=NV_ENC_PRESET_P3_GUID;init.encodeWidth=w;init.encodeHeight=h;init.darWidth=w;init.darHeight=h;init.frameRateNum=60;init.frameRateDen=1;init.enablePTD=1;init.encodeConfig=&config;init.maxEncodeWidth=w;init.maxEncodeHeight=h;init.tuningInfo=NV_ENC_TUNING_INFO_LOW_LATENCY;
  auto result=functions.nvEncInitializeEncoder(encoder,&init);if(result!=NV_ENC_SUCCESS){error=QString("NVENC initialize %1").arg(result);clear();return false;}
  NV_ENC_REGISTER_RESOURCE resource{};resource.version=NV_ENC_REGISTER_RESOURCE_VER;resource.resourceType=NV_ENC_INPUT_RESOURCE_TYPE_DIRECTX;resource.resourceToRegister=texture;resource.width=w;resource.height=h;resource.bufferFormat=NV_ENC_BUFFER_FORMAT_ARGB;resource.bufferUsage=NV_ENC_INPUT_IMAGE;
  result=functions.nvEncRegisterResource(encoder,&resource);if(result!=NV_ENC_SUCCESS){error=QString("NVENC register %1").arg(result);clear();return false;}registered=resource.registeredResource;
  NV_ENC_CREATE_BITSTREAM_BUFFER buffer{};buffer.version=NV_ENC_CREATE_BITSTREAM_BUFFER_VER;result=functions.nvEncCreateBitstreamBuffer(encoder,&buffer);if(result!=NV_ENC_SUCCESS){error="NVENC bitstream buffer";clear();return false;}output=buffer.bitstreamBuffer;input=texture;width=w;height=h;return true;
 }
 bool encode(bool key,QByteArray *bytes,bool *isKey){
  NV_ENC_MAP_INPUT_RESOURCE map{};map.version=NV_ENC_MAP_INPUT_RESOURCE_VER;map.registeredResource=registered;
  auto result=functions.nvEncMapInputResource(encoder,&map);if(result!=NV_ENC_SUCCESS){error="NVENC map";return false;}
  NV_ENC_PIC_PARAMS picture{};picture.version=NV_ENC_PIC_PARAMS_VER;picture.inputBuffer=map.mappedResource;picture.bufferFmt=map.mappedBufferFmt;picture.inputWidth=width;picture.inputHeight=height;picture.outputBitstream=output;picture.pictureStruct=NV_ENC_PIC_STRUCT_FRAME;picture.inputTimeStamp=sequence++;picture.inputDuration=1;
  if(key)picture.encodePicFlags=NV_ENC_PIC_FLAG_FORCEIDR|NV_ENC_PIC_FLAG_OUTPUT_SPSPPS;
  result=functions.nvEncEncodePicture(encoder,&picture);if(result!=NV_ENC_SUCCESS){functions.nvEncUnmapInputResource(encoder,map.mappedResource);error=QString("NVENC encode %1").arg(result);return false;}
  NV_ENC_LOCK_BITSTREAM lock{};lock.version=NV_ENC_LOCK_BITSTREAM_VER;lock.outputBitstream=output;
  result=functions.nvEncLockBitstream(encoder,&lock);if(result==NV_ENC_SUCCESS){lastQp=int(lock.frameAvgQP);*bytes=QByteArray(static_cast<const char*>(lock.bitstreamBufferPtr),lock.bitstreamSizeInBytes);*isKey=lock.pictureType==NV_ENC_PIC_TYPE_IDR||lock.pictureType==NV_ENC_PIC_TYPE_I;functions.nvEncUnlockBitstream(encoder,output);}
  functions.nvEncUnmapInputResource(encoder,map.mappedResource);if(result!=NV_ENC_SUCCESS){error="NVENC output lock";return false;}return true;
 }
};
QByteArray annexB(const uint8_t *data,int size){
 if(!data||size<=0)return {};
 if(size>=4&&data[0]==0&&data[1]==0&&(data[2]==1||(data[2]==0&&data[3]==1)))return QByteArray(reinterpret_cast<const char*>(data),size);
 QByteArray out;int pos=0;
 if(size>=7&&data[0]==1){
  pos=5;int count=data[pos++]&31;
  for(int pass=0;pass<2;++pass){for(int i=0;i<count;++i){if(pos+2>size)return {};int n=(data[pos]<<8)|data[pos+1];pos+=2;if(n<=0||pos+n>size)return {};out.append("\0\0\0\1",4);out.append(reinterpret_cast<const char*>(data+pos),n);pos+=n;}if(pass==0){if(pos>=size)return {};count=data[pos++];}}
 }else{
  while(pos+4<=size){uint32_t n=fr_get_u32(data+pos);pos+=4;if(n==0||n>uint32_t(size-pos))return {};out.append("\0\0\0\1",4);out.append(reinterpret_cast<const char*>(data+pos),n);pos+=n;}if(pos!=size)return {};
 }return out;
}
struct Encoder {
 AVCodecContext *codec=nullptr;AVFrame *frame=nullptr;AVPacket *packet=nullptr;SwsContext *scaler=nullptr;int64_t sequence=0;QString error;
 ~Encoder(){clear();}
 void clear(){auto &a=api();if(scaler)a.sws_freeContext(scaler);scaler=nullptr;if(frame)a.av_frame_free(&frame);if(packet)a.av_packet_free(&packet);if(codec)a.avcodec_free_context(&codec);}
 bool open(int w,int h){
  clear();auto &a=api();const auto *encoder=a.avcodec_find_encoder_by_name("h264_mf");if(!encoder){error="h264_mf encoder unavailable";return false;}
  codec=a.avcodec_alloc_context3(encoder);codec->width=w;codec->height=h;codec->pix_fmt=AV_PIX_FMT_NV12;codec->time_base={1,60};codec->framerate={60,1};codec->gop_size=FR_VIDEO_GOP;codec->max_b_frames=0;codec->flags|=AV_CODEC_FLAG_LOW_DELAY;codec->profile=AV_PROFILE_H264_BASELINE;
  codec->bit_rate=fr_video_bitrate(w,h);codec->rc_max_rate=codec->bit_rate*3/2;codec->rc_buffer_size=int(codec->bit_rate/5);codec->compression_level=0;
  a.av_opt_set_int(codec->priv_data,"hw_encoding",1,0);a.av_opt_set(codec->priv_data,"scenario","display_remoting",0);a.av_opt_set(codec->priv_data,"rate_control","ld_vbr",0);
  const int result=a.avcodec_open2(codec,encoder,nullptr);if(result<0){error=a.error(result);clear();return false;}
  frame=a.av_frame_alloc();packet=a.av_packet_alloc();frame->format=codec->pix_fmt;frame->width=w;frame->height=h;
  if(a.av_frame_get_buffer(frame,32)<0){error="NV12 buffer allocation";clear();return false;}return true;
 }
 bool encode(const QImage &image,bool key,std::vector<QByteArray> *out,std::vector<bool> *keys){
  auto &a=api();if(a.av_frame_make_writable(frame)<0)return false;
  scaler=a.sws_getCachedContext(scaler,image.width(),image.height(),AV_PIX_FMT_BGRA,codec->width,codec->height,AV_PIX_FMT_NV12,SWS_FAST_BILINEAR,nullptr,nullptr,nullptr);
  if(!scaler){error="Video scaler";return false;}const uint8_t *planes[]={image.constBits(),nullptr,nullptr,nullptr};int strides[]={int(image.bytesPerLine()),0,0,0};
  a.sws_scale(scaler,planes,strides,0,image.height(),frame->data,frame->linesize);frame->pts=sequence++;frame->duration=1;frame->pict_type=key?AV_PICTURE_TYPE_I:AV_PICTURE_TYPE_NONE;
  auto drain=[&]{int result;while((result=a.avcodec_receive_packet(codec,packet))>=0){const bool k=(packet->flags&AV_PKT_FLAG_KEY)!=0;auto bytes=annexB(packet->data,packet->size);if(k)bytes=annexB(codec->extradata,codec->extradata_size)+bytes;out->push_back(bytes);keys->push_back(k);a.av_packet_unref(packet);}return result;};
  int result=a.avcodec_send_frame(codec,frame);if(result==AVERROR(EAGAIN)){drain();result=a.avcodec_send_frame(codec,frame);}if(result<0){error=a.error(result);return false;}result=drain();if(result!=AVERROR(EAGAIN)&&result!=AVERROR_EOF){error=a.error(result);return false;}return true;
 }
};
struct Decoder {
 AVCodecContext *codec=nullptr;AVFrame *frame=nullptr,*cpu=nullptr;AVPacket *packet=nullptr;SwsContext *scaler=nullptr;QString error;bool hardware=false;AVBufferRef *device=nullptr;
 ~Decoder(){clear();if(device)api().av_buffer_unref(&device);}
 void clear(){auto &a=api();if(scaler)a.sws_freeContext(scaler);scaler=nullptr;if(frame)a.av_frame_free(&frame);if(cpu)a.av_frame_free(&cpu);if(packet)a.av_packet_free(&packet);if(codec)a.avcodec_free_context(&codec);}
 static AVPixelFormat format(AVCodecContext*,const AVPixelFormat *formats){for(auto p=formats;*p!=AV_PIX_FMT_NONE;++p)if(*p==AV_PIX_FMT_D3D11)return *p;return formats[0];}
 bool open(){
  clear();auto &a=api();const auto *decoder=a.avcodec_find_decoder_by_name("h264");if(!decoder){error="H.264 decoder unavailable";return false;}codec=a.avcodec_alloc_context3(decoder);codec->thread_count=2;codec->flags|=AV_CODEC_FLAG_LOW_DELAY;
  if(!device)a.av_hwdevice_ctx_create(&device,AV_HWDEVICE_TYPE_D3D11VA,nullptr,nullptr,0);hardware=device!=nullptr;if(hardware){codec->hw_device_ctx=a.av_buffer_ref(device);codec->get_format=&format;}
  const int result=a.avcodec_open2(codec,decoder,nullptr);if(result<0){error=a.error(result);clear();return false;}
  frame=a.av_frame_alloc();cpu=a.av_frame_alloc();packet=a.av_packet_alloc();return true;
 }
 bool decode(const QByteArray &bytes,QImage *out){
  auto &a=api();a.av_packet_unref(packet);if(a.av_new_packet(packet,bytes.size())<0)return false;memcpy(packet->data,bytes.constData(),bytes.size());
  const int sent=a.avcodec_send_packet(codec,packet);if(sent<0&&sent!=AVERROR(EAGAIN)){error=a.error(sent);return false;}
  int result;while((result=a.avcodec_receive_frame(codec,frame))>=0){AVFrame *image=frame;
   if(frame->format==AV_PIX_FMT_D3D11){a.av_frame_unref(cpu);if(a.av_hwframe_transfer_data(cpu,frame,0)<0){a.av_frame_unref(frame);continue;}image=cpu;}
   if(image->width>4096||image->height>4096||image->width<2||image->height<2){a.av_frame_unref(frame);return false;}
   scaler=a.sws_getCachedContext(scaler,image->width,image->height,AVPixelFormat(image->format),image->width,image->height,AV_PIX_FMT_BGRA,SWS_FAST_BILINEAR,nullptr,nullptr,nullptr);
   if(!scaler){a.av_frame_unref(frame);return false;}
   // swscale does not automatically use AVFrame color metadata. Read it from
   // the decoded frame (hardware transfer only copies the pixel planes).
   const int matrix=frame->colorspace==AVCOL_SPC_BT709?SWS_CS_ITU709:(frame->colorspace==AVCOL_SPC_BT2020_NCL?SWS_CS_BT2020:SWS_CS_DEFAULT);
   const int *coefficients=a.sws_getCoefficients(matrix);
   a.sws_setColorspaceDetails(scaler,coefficients,frame->color_range==AVCOL_RANGE_JPEG,coefficients,1,0,1<<16,1<<16);
   QImage rgb(image->width,image->height,QImage::Format_RGB32);uint8_t *dst[]={rgb.bits(),nullptr,nullptr,nullptr};int stride[]={int(rgb.bytesPerLine()),0,0,0};a.sws_scale(scaler,image->data,image->linesize,0,image->height,dst,stride);*out=rgb;a.av_frame_unref(frame);
  }return result==AVERROR(EAGAIN)||result==AVERROR_EOF;
 }
};
}
#endif
DesktopVideoSender::DesktopVideoSender(QObject *parent):QObject(parent){}
DesktopVideoSender::~DesktopVideoSender(){stop();}
void DesktopVideoSender::setResolution(int edge){if(fr_resolution_valid(edge))m_resolution=edge;}
void DesktopVideoSender::feedback(double fps,unsigned queueMs,double workMs){m_remoteFps=fps;m_queueMs=queueMs;m_remoteWorkMs=workMs;}
void DesktopVideoSender::accepted(bool delivered){m_pending=false;if(!delivered)m_key=true;}
void DesktopVideoSender::start(const QString &display,const QRect &geometry){
 Q_UNUSED(geometry);stop();{std::lock_guard<std::mutex> lock(m_sourceMutex);m_display=display;}
 m_stop=false;m_refresh=false;m_key=true;m_pending=false;m_remoteFps=60;m_queueMs=0;m_remoteWorkMs=0;m_localDecodeMs=0;
 const quint64 token=++m_runToken;m_thread=QThread::create([this]{run();});
 connect(m_thread,&QThread::finished,this,[this,token]{if(token==m_runToken&&!m_stop)emit failed();},Qt::QueuedConnection);m_thread->start();
}
void DesktopVideoSender::refreshDisplay(const QString &display){
 {std::lock_guard<std::mutex> lock(m_sourceMutex);m_display=display;}m_refresh=true;
}
void DesktopVideoSender::stop(){++m_runToken;m_stop=true;if(m_thread){m_thread->wait();delete m_thread;m_thread=nullptr;}m_pending=false;}
void DesktopVideoSender::run(){
#ifdef Q_OS_WIN
 timeBeginPeriod(1);SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_ABOVE_NORMAL);
 const HRESULT com=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
 {auto &a=api();if(!a.failure.isEmpty()){emit status(furinaStatus("视频组件不可用："+a.failure,"Video component unavailable: "+a.failure));}
 else{
  fr_video_policy policy;fr_video_policy_init(&policy,m_resolution);
  QElapsedTimer clock,recovery;clock.start();recovery.start();uint32_t sequence=0;bool fatal=false,announced=false;
  // Display changes invalidate DXGI duplication, D3D surfaces and NVENC registration.
  // Recreate the whole capture/encoder pair while retaining the peer and monotonic PTS.
  while(!m_stop&&!fatal){
   m_refresh.exchange(false);QString display;{std::lock_guard<std::mutex> lock(m_sourceMutex);display=m_display;}
   Capture capture;
   if(!capture.open(display)){
    if(recovery.elapsed()>10000){emit status(furinaStatus("显示器采集恢复超时："+capture.error,"Display capture recovery timed out: "+capture.error));break;}
    if(!announced){emit status(furinaStatus("显示器暂不可用，正在等待显示配置稳定。","Display temporarily unavailable. Waiting for the configuration to settle."));announced=true;}
    QThread::msleep(100);continue;
   }
   Encoder encoder;NvEncoder nv;bool native=true,retry=false;int currentEdge=0,w=0,h=0,frames=0;uint32_t epoch=0;double work=0;
   QElapsedTimer sample;sample.start();qint64 due=clock.nsecsElapsed();
   while(!m_stop){
    if(m_refresh.exchange(false)){retry=true;break;}
    if(recovery.elapsed()>10000){emit status(furinaStatus("显示器未能恢复实时画面，采集已停止。","The monitor did not resume live frames. Capture stopped."));fatal=true;break;}
    const auto now=clock.nsecsElapsed();if(now<due){QThread::usleep(qMin<qint64>((due-now)/1000,2000));continue;}due+=1000000000LL/60;if(now-due>1000000000LL/60)due=now+1000000000LL/60;
    if(m_resolution!=policy.selected)fr_video_policy_init(&policy,m_resolution);
    if(m_pending)continue;
    QElapsedTimer processing;processing.start();std::vector<QByteArray> output;std::vector<bool> keys;
    if(policy.edge!=currentEdge){
     nv.clear();encoder.clear();fr_video_dimensions(capture.sourceSize().width(),capture.sourceSize().height(),policy.edge,&w,&h);
     auto *texture=native?capture.gpuFrame(w,h):nullptr;
     if(native&&!texture){if(capture.error.isEmpty())continue;if(capture.recoverable){retry=true;break;}native=false;}
     if(native&&!nv.open(capture.gpuDevice(),texture,w,h)){native=false;emit status(furinaStatus("NVENC 不可用，切换 Media Foundation："+nv.error,"NVENC unavailable; switching to Media Foundation: "+nv.error));}
     if(!native&&!encoder.open(w,h)){emit status(furinaStatus("硬件编码启动失败："+encoder.error,"Hardware encoder could not start: "+encoder.error));fatal=true;break;}
     currentEdge=policy.edge;epoch=++m_epoch;if(!epoch)epoch=++m_epoch;m_key=true;
     emit status(furinaStatus(QString("DXGI + %1，%2×%3，目标 60 FPS").arg(native?"GPU 缩放 + NVENC":"Media Foundation").arg(w).arg(h),QString("DXGI + %1, %2x%3, target 60 FPS").arg(native?"GPU scaling + NVENC":"Media Foundation").arg(w).arg(h)));
    }
    if(native){
     if(!capture.gpuFrame(w,h)){
      if(capture.error.isEmpty())continue;
      if(capture.recoverable){retry=true;break;}
      emit status(furinaStatus("采集停止："+capture.error,"Capture stopped: "+capture.error));fatal=true;break;
     }
     QByteArray data;bool key=false;if(!nv.encode(m_key.exchange(false),&data,&key)){retry=true;break;}output.push_back(data);keys.push_back(key);
    }else{
     auto image=capture.grab();if(image.isNull()){if(capture.error.isEmpty())continue;if(capture.recoverable){retry=true;break;}emit status(furinaStatus("采集停止："+capture.error,"Capture stopped: "+capture.error));fatal=true;break;}
     if(!encoder.encode(image,m_key.exchange(false),&output,&keys)){emit status(furinaStatus("编码失败："+encoder.error,"Encoding failed: "+encoder.error));fatal=true;break;}
    }
    work+=processing.nsecsElapsed()/1000000.0;
    for(size_t i=0;i<output.size();++i){if(output[i].isEmpty())continue;
     fr_video_info info{keys[i]?FR_VIDEO_KEY:0u,++sequence,uint32_t(w),uint32_t(h),epoch,uint64_t(clock.nsecsElapsed()/1000)};
     QByteArray packet(FR_VIDEO_HEADER,Qt::Uninitialized);fr_video_write(reinterpret_cast<uint8_t*>(packet.data()),&info);packet+=output[i];m_pending=true;emit this->packet(packet);++frames;recovery.restart();
     if(announced){emit status(furinaStatus("显示配置变化后的实时采集已恢复。","Live capture recovered after the display configuration change."));announced=false;}
    }
    if(sample.elapsed()>=1000){double fps=frames*1000.0/sample.elapsed(),ms=frames?work/frames:0;emit stats(fps,ms,currentEdge);fr_video_policy_sample(&policy,qMin(fps,m_remoteFps.load()),qMax(ms+m_localDecodeMs.load(),m_remoteWorkMs.load()),m_queueMs);frames=0;work=0;sample.restart();}
   }
   if(!retry||m_stop)break;
   if(!announced){emit status(furinaStatus("显示配置变化，正在重建 GPU 采集与编码。","Display configuration changed. Recreating GPU capture and encoding."));announced=true;}
   m_pending=false;QThread::msleep(100);
  }
 }}if(SUCCEEDED(com))CoUninitialize();timeEndPeriod(1);
#else
 emit status(furinaStatus("桌面视频采集仅在 Windows 使用","Desktop capture is available on Windows"));
#endif
}
DesktopVideoReceiver::DesktopVideoReceiver(QObject *parent):QObject(parent){m_thread=QThread::create([this]{run();});m_thread->start();}
DesktopVideoReceiver::~DesktopVideoReceiver(){{std::lock_guard<std::mutex> lock(m_mutex);m_stop=true;}m_wake.notify_all();m_thread->wait();delete m_thread;}
void DesktopVideoReceiver::reset(){{std::lock_guard<std::mutex> lock(m_mutex);++m_generation;m_reset=true;m_packets.clear();m_latest={};m_waitKey=true;m_notifyPending=false;}m_wake.notify_all();}
void DesktopVideoReceiver::submit(const QByteArray &data){fr_video_info info{};if(!fr_video_read(reinterpret_cast<const uint8_t*>(data.constData()),data.size(),&info))return;
 bool recovery=false;{std::lock_guard<std::mutex> lock(m_mutex);if(m_packets.size()>=12){m_packets.clear();m_waitKey=true;m_reset=true;recovery=true;}if(!m_waitKey||(info.flags&FR_VIDEO_KEY)){m_waitKey=false;m_packets.push_back(data);}}if(recovery)emit recover();m_wake.notify_one();
}
bool DesktopVideoReceiver::takeImage(QImage *image){std::lock_guard<std::mutex> lock(m_mutex);m_notifyPending=false;if(m_latest.isNull())return false;*image=std::move(m_latest);m_latest={};return true;}
void DesktopVideoReceiver::run(){
#ifdef Q_OS_WIN
 const HRESULT com=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
 {auto &a=api();if(!a.failure.isEmpty()){emit status(furinaStatus("解码组件不可用："+a.failure,"Decoder component unavailable: "+a.failure));}
 else{Decoder decoder;uint32_t epoch=0;int frames=0;double work=0;unsigned queue=0;QElapsedTimer sample;sample.start();
  while(true){QByteArray bytes;bool reset=false;uint64_t generation=0;{std::unique_lock<std::mutex> lock(m_mutex);m_wake.wait(lock,[this]{return m_stop||m_reset||!m_packets.empty();});if(m_stop)break;generation=m_generation;reset=m_reset;m_reset=false;if(m_packets.empty()){if(reset){decoder.clear();epoch=0;frames=0;work=0;sample.restart();}continue;}bytes=std::move(m_packets.front());m_packets.pop_front();queue=unsigned(m_packets.size()*1000/60);}
   fr_video_info info{};if(!fr_video_read(reinterpret_cast<const uint8_t*>(bytes.constData()),bytes.size(),&info))continue;
   if(reset||!decoder.codec||epoch!=info.epoch){if(!(info.flags&FR_VIDEO_KEY))continue;if(reset&&decoder.codec&&epoch==info.epoch){a.avcodec_flush_buffers(decoder.codec);}else if(!decoder.open()){emit status(furinaStatus("解码启动失败："+decoder.error,"Decoder could not start: "+decoder.error));continue;}if(!epoch)sample.restart();epoch=info.epoch;emit status(furinaStatus(decoder.hardware?"电脑 H.264 解码使用 D3D11VA":"电脑 H.264 解码使用软件回退",decoder.hardware?"PC H.264 decoding uses D3D11VA":"PC H.264 decoding uses software fallback"));}
   QElapsedTimer processing;processing.start();QImage image;if(!decoder.decode(bytes.mid(FR_VIDEO_HEADER),&image)){emit status(furinaStatus("视频解码失败："+decoder.error,"Video decode failed: "+decoder.error));emit recover();{std::lock_guard<std::mutex> lock(m_mutex);m_waitKey=true;m_reset=true;}continue;}work+=processing.nsecsElapsed()/1000000.0;bool notify=false;if(!image.isNull()){std::lock_guard<std::mutex> lock(m_mutex);if(generation!=m_generation)continue;m_latest=std::move(image);++frames;if(!m_notifyPending){m_notifyPending=true;notify=true;}}if(notify)emit frameReady();
   if(sample.elapsed()>=1000){emit stats(frames*1000.0/sample.elapsed(),frames?work/frames:0,queue);frames=0;work=0;sample.restart();}
  }
 }}if(SUCCEEDED(com))CoUninitialize();
#else
 std::unique_lock<std::mutex> lock(m_mutex);m_wake.wait(lock,[this]{return m_stop;});
#endif
}
