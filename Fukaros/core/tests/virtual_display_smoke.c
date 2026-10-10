/* Explicit real-desktop smoke test; intentionally not in automatic CTest. */
#include "virtual_display.h"
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
typedef struct saved_display {wchar_t name[32];DEVMODEW mode;} saved_display;
static int unchanged(saved_display *saved,unsigned n){
 for(unsigned i=0;i<n;i++){DEVMODEW m={0};m.dmSize=sizeof(m);if(!EnumDisplaySettingsW(saved[i].name,ENUM_CURRENT_SETTINGS,&m))return 0;
  DEVMODEW *b=&saved[i].mode;if(m.dmPelsWidth!=b->dmPelsWidth||m.dmPelsHeight!=b->dmPelsHeight||m.dmDisplayFrequency!=b->dmDisplayFrequency||m.dmPosition.x!=b->dmPosition.x||m.dmPosition.y!=b->dmPosition.y)return 0;
 }return 1;
}
int main(void){
 saved_display saved[32];unsigned count=0;DISPLAY_DEVICEW device;
 if(!fr_virtual_available()){puts("Virtual driver unavailable");return 77;}
 for(DWORD i=0;;i++){memset(&device,0,sizeof(device));device.cb=sizeof(device);if(!EnumDisplayDevicesW(NULL,i,&device,0))break;
  if((device.StateFlags&DISPLAY_DEVICE_ATTACHED_TO_DESKTOP)&&_wcsicmp(device.DeviceID,L"Root\\MttVDD")&&count<32){wcscpy(saved[count].name,device.DeviceName);memset(&saved[count].mode,0,sizeof(DEVMODEW));saved[count].mode.dmSize=sizeof(DEVMODEW);if(EnumDisplaySettingsW(device.DeviceName,ENUM_CURRENT_SETTINGS,&saved[count].mode))count++;}
 }
 for(unsigned cycle=0;cycle<2;cycle++){
  fr_virtual_display d={0};LONG e=fr_virtual_acquire(&d);if(e){printf("acquire error %ld\n",e);return 1;}
  DEVMODEW mode={0};mode.dmSize=sizeof(mode);int correct=EnumDisplaySettingsW(d.name,ENUM_CURRENT_SETTINGS,&mode)&&mode.dmPelsWidth==1920&&mode.dmPelsHeight==1200&&mode.dmDisplayFrequency==60&&unchanged(saved,count);
  Sleep(200);e=fr_virtual_release(&d);if(!correct||e||!unchanged(saved,count)){printf("mode/restore failure: correct=%d error=%ld\n",correct,e);return 2;}
  printf("cycle %u: 1920x1200 / 60 Hz; physical displays preserved; released\n",cycle+1);
 }return 0;
}
#else
int main(void){return 77;}
#endif
