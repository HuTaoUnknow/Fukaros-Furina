#include "virtual_display.h"
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <wctype.h>
#ifdef _WIN32
#include <wchar.h>
#include <setupapi.h>
static const GUID display_class={0x4d36e968,0xe325,0x11ce,{0xbf,0xc1,0x08,0x00,0x2b,0xe1,0x03,0x18}};
static int virtual_instance(wchar_t *instance,unsigned size){
 HDEVINFO list=SetupDiGetClassDevsW(&display_class,NULL,NULL,DIGCF_PRESENT);SP_DEVINFO_DATA dev;wchar_t ids[1024];DWORD i;int found=0;
 if(list==INVALID_HANDLE_VALUE)return 0;
 for(i=0;;i++){memset(&dev,0,sizeof(dev));dev.cbSize=sizeof(dev);if(!SetupDiEnumDeviceInfo(list,i,&dev))break;
  if(SetupDiGetDeviceRegistryPropertyW(list,&dev,SPDRP_HARDWAREID,NULL,(BYTE*)ids,sizeof(ids),NULL)&&!_wcsicmp(ids,L"Root\\MttVDD")){
   found=!instance||SetupDiGetDeviceInstanceIdW(list,&dev,instance,size,NULL);break;
  }
 }SetupDiDestroyDeviceInfoList(list);return found;
}
static int find_virtual(DISPLAY_DEVICEW *out){
 DISPLAY_DEVICEW d;DWORD i;
 for(i=0;;i++){memset(&d,0,sizeof(d));d.cb=sizeof(d);if(!EnumDisplayDevicesW(NULL,i,&d,0))break;
  if(!_wcsicmp(d.DeviceID,L"Root\\MttVDD")&&!(d.StateFlags&DISPLAY_DEVICE_PRIMARY_DEVICE)){*out=d;return 1;}
 }return 0;
}
static int virtual_path(const DISPLAYCONFIG_PATH_INFO *path){
 DISPLAYCONFIG_ADAPTER_NAME adapter={0};wchar_t instance[1024],normalized[128];unsigned i;
 if(!virtual_instance(instance,1024))return 0;
 adapter.header.type=DISPLAYCONFIG_DEVICE_INFO_GET_ADAPTER_NAME;adapter.header.size=sizeof(adapter);adapter.header.adapterId=path->targetInfo.adapterId;
 if(DisplayConfigGetDeviceInfo(&adapter.header))return 0;
 for(i=0;i<127&&adapter.adapterDevicePath[i];i++)normalized[i]=adapter.adapterDevicePath[i]==L'#'?L'\\':towupper(adapter.adapterDevicePath[i]);normalized[i]=0;
 for(i=0;instance[i];i++)instance[i]=towupper(instance[i]);
 return wcsstr(normalized,instance)!=NULL;
}
static LONG query_paths(DISPLAYCONFIG_PATH_INFO **paths,UINT32 *count,DISPLAYCONFIG_MODE_INFO **modes,UINT32 *mode_count){
 LONG result;unsigned attempt;
 *paths=NULL;*modes=NULL;
 for(attempt=0;attempt<4;attempt++){
  result=GetDisplayConfigBufferSizes(QDC_ALL_PATHS,count,mode_count);if(result)return result;
  *paths=calloc(*count+1,sizeof(**paths));*modes=calloc(*mode_count+2,sizeof(**modes));
  if(!*paths||!*modes){free(*paths);free(*modes);*paths=NULL;*modes=NULL;return ERROR_OUTOFMEMORY;}
  result=QueryDisplayConfig(QDC_ALL_PATHS,count,*paths,mode_count,*modes,NULL);
  if(result!=ERROR_INSUFFICIENT_BUFFER)return result;
  free(*paths);free(*modes);*paths=NULL;*modes=NULL;
 }return ERROR_INSUFFICIENT_BUFFER;
}
int fr_virtual_available(void){DISPLAY_DEVICEW d;return find_virtual(&d);}
int fr_virtual_device_present(void){return virtual_instance(NULL,0);}
int fr_virtual_name(const fr_virtual_display *d,char *name,unsigned size){
 DISPLAY_DEVICEW current;
 if(!d||!d->acquired||!name||!size)return 0;
 /* GDI source names may be reassigned after a display topology change. */
 if(!find_virtual(&current))return 0;
 return WideCharToMultiByte(CP_UTF8,0,current.DeviceName,-1,name,(int)size,NULL,NULL)>0;
}
long fr_virtual_detach_idle(void){
 DISPLAYCONFIG_PATH_INFO *paths=NULL,*active=NULL;DISPLAYCONFIG_MODE_INFO *modes=NULL;UINT32 count=0,mode_count=0,n=0,i;LONG result;int removed=0;
 result=query_paths(&paths,&count,&modes,&mode_count);if(result)goto done;
 active=calloc(count+1,sizeof(*active));if(!active){result=ERROR_OUTOFMEMORY;goto done;}
 for(i=0;i<count;i++)if(paths[i].flags&DISPLAYCONFIG_PATH_ACTIVE){if(virtual_path(&paths[i]))removed=1;else active[n++]=paths[i];}
 if(!removed){result=0;goto done;}if(!n){result=ERROR_ACCESS_DENIED;goto done;}
 result=SetDisplayConfig(n,active,mode_count,modes,SDC_APPLY|SDC_USE_SUPPLIED_DISPLAY_CONFIG|SDC_ALLOW_CHANGES|SDC_SAVE_TO_DATABASE);
 done:free(active);free(paths);free(modes);return result;
}
long fr_virtual_acquire(fr_virtual_display *d){
 DISPLAY_DEVICEW v;DISPLAYCONFIG_PATH_INFO *paths=NULL,*active=NULL;DISPLAYCONFIG_MODE_INFO *modes=NULL;UINT32 count=0,mode_count=0,n=0,i,index=UINT_MAX;LONG result,right=0,top=0;int positioned=0;
 if(!d)return ERROR_INVALID_PARAMETER;if(d->acquired)return 0;if(!find_virtual(&v))return ERROR_NOT_FOUND;
 memset(d,0,sizeof(*d));wcscpy(d->name,v.DeviceName);
 result=query_paths(&paths,&count,&modes,&mode_count);if(result)goto done;
 active=calloc(count+1,sizeof(*active));if(!active){result=ERROR_OUTOFMEMORY;goto done;}
 for(i=0;i<count;i++){
  if(virtual_path(&paths[i])&&paths[i].targetInfo.targetAvailable){if(index==UINT_MAX||(paths[i].flags&DISPLAYCONFIG_PATH_ACTIVE))index=i;}
  if(paths[i].flags&DISPLAYCONFIG_PATH_ACTIVE){active[n++]=paths[i];if(virtual_path(&paths[i]))d->original_attached=1;
   else if(paths[i].sourceInfo.modeInfoIdx<mode_count){DISPLAYCONFIG_SOURCE_MODE *source=&modes[paths[i].sourceInfo.modeInfoIdx].sourceMode;
    if(!positioned||source->position.x+(LONG)source->width>right){right=source->position.x+(LONG)source->width;top=source->position.y;positioned=1;}
   }
  }
 }
 if(index==UINT_MAX||!positioned){result=ERROR_NOT_FOUND;goto done;}
 {
  DISPLAYCONFIG_TARGET_DEVICE_NAME target={0};target.header.type=DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;target.header.size=sizeof(target);target.header.adapterId=paths[index].targetInfo.adapterId;target.header.id=paths[index].targetInfo.id;
  if(!DisplayConfigGetDeviceInfo(&target.header))WideCharToMultiByte(CP_UTF8,0,target.monitorFriendlyDeviceName,-1,d->monitor_name,sizeof(d->monitor_name),NULL,NULL);
  d->x=right;d->y=top;
 }
 d->paths=malloc(n*sizeof(*active));d->modes=malloc(mode_count*sizeof(*modes));if(!d->paths||(!d->modes&&mode_count)){result=ERROR_OUTOFMEMORY;goto done;}
 memcpy(d->paths,active,n*sizeof(*active));memcpy(d->modes,modes,mode_count*sizeof(*modes));d->path_count=n;d->mode_count=mode_count;
 n=0;for(i=0;i<count;i++)if((paths[i].flags&DISPLAYCONFIG_PATH_ACTIVE)&&!virtual_path(&paths[i]))active[n++]=paths[i];
 active[n]=paths[index];active[n].flags|=DISPLAYCONFIG_PATH_ACTIVE;
 /* Clone installation can have the physical adapter's source. Give the IDD
  * its own source instead of trying to enable the dormant GDI adapter. */
 active[n].sourceInfo.adapterId=active[n].targetInfo.adapterId;active[n].sourceInfo.id=0;active[n].sourceInfo.modeInfoIdx=mode_count;
 active[n].targetInfo.modeInfoIdx=DISPLAYCONFIG_PATH_MODE_IDX_INVALID;active[n].targetInfo.refreshRate.Numerator=60;active[n].targetInfo.refreshRate.Denominator=1;active[n].targetInfo.rotation=DISPLAYCONFIG_ROTATION_IDENTITY;active[n].targetInfo.scaling=DISPLAYCONFIG_SCALING_IDENTITY;
 memset(&modes[mode_count],0,sizeof(*modes));modes[mode_count].infoType=DISPLAYCONFIG_MODE_INFO_TYPE_SOURCE;modes[mode_count].adapterId=active[n].sourceInfo.adapterId;modes[mode_count].id=0;
 modes[mode_count].sourceMode.width=1920;modes[mode_count].sourceMode.height=1200;modes[mode_count].sourceMode.pixelFormat=DISPLAYCONFIG_PIXELFORMAT_32BPP;modes[mode_count].sourceMode.position.x=right;modes[mode_count].sourceMode.position.y=top;
 /* Inactive paths have no scan timing. Supplying only width/height is
  * rejected with ERROR_INVALID_PARAMETER, even with SDC_ALLOW_CHANGES. */
 {
  DISPLAYCONFIG_TARGET_PREFERRED_MODE preferred={0};
  preferred.header.type=DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_PREFERRED_MODE;preferred.header.size=sizeof(preferred);preferred.header.adapterId=active[n].targetInfo.adapterId;preferred.header.id=active[n].targetInfo.id;
  result=DisplayConfigGetDeviceInfo(&preferred.header);if(result)goto done;
  memset(&modes[mode_count+1],0,sizeof(*modes));modes[mode_count+1].infoType=DISPLAYCONFIG_MODE_INFO_TYPE_TARGET;modes[mode_count+1].adapterId=active[n].targetInfo.adapterId;modes[mode_count+1].id=active[n].targetInfo.id;modes[mode_count+1].targetMode=preferred.targetMode;
  active[n].targetInfo.modeInfoIdx=mode_count+1;active[n].targetInfo.scanLineOrdering=preferred.targetMode.targetVideoSignalInfo.scanLineOrdering;
 }
 result=SetDisplayConfig(n+1,active,mode_count+2,modes,SDC_VALIDATE|SDC_USE_SUPPLIED_DISPLAY_CONFIG|SDC_ALLOW_CHANGES);
 if(!result)result=SetDisplayConfig(n+1,active,mode_count+2,modes,SDC_APPLY|SDC_USE_SUPPLIED_DISPLAY_CONFIG|SDC_ALLOW_CHANGES|SDC_SAVE_TO_DATABASE);
 if(!result){d->acquired=1;if(find_virtual(&v))wcscpy(d->name,v.DeviceName);}
 done:free(active);free(paths);free(modes);if(result){free(d->paths);free(d->modes);memset(d,0,sizeof(*d));}return result;
}
long fr_virtual_release(fr_virtual_display *d){
 LONG result;if(!d||!d->acquired)return 0;
 if(d->original_attached)result=SetDisplayConfig(d->path_count,d->paths,d->mode_count,d->modes,SDC_APPLY|SDC_USE_SUPPLIED_DISPLAY_CONFIG|SDC_ALLOW_CHANGES|SDC_SAVE_TO_DATABASE);
 else result=fr_virtual_detach_idle();
 if(!result){free(d->paths);free(d->modes);memset(d,0,sizeof(*d));}return result;
}
#else
int fr_virtual_available(void){return 0;}
int fr_virtual_device_present(void){return 0;}
int fr_virtual_name(const fr_virtual_display *d,char *n,unsigned s){(void)d;(void)n;(void)s;return 0;}
long fr_virtual_acquire(fr_virtual_display *d){(void)d;return 50;}
long fr_virtual_release(fr_virtual_display *d){(void)d;return 0;}
long fr_virtual_detach_idle(void){return 0;}
#endif
