#pragma once
/* Windows desktop topology belongs here, independent of the Qt UI. */
#ifdef _WIN32
#include <windows.h>
typedef struct fr_virtual_display {
 wchar_t name[32];
 char monitor_name[128];
 long x,y;
 void *paths,*modes;
 unsigned path_count,mode_count;
 int original_attached;
 int acquired;
} fr_virtual_display;
#else
typedef struct fr_virtual_display { int acquired; } fr_virtual_display;
#endif
#ifdef __cplusplus
extern "C" {
#endif
int fr_virtual_available(void);
int fr_virtual_device_present(void);
/* Uses a 1920x1200 / 60 Hz desktop. Video resolution remains independent. */
long fr_virtual_acquire(fr_virtual_display *display);
long fr_virtual_release(fr_virtual_display *display);
long fr_virtual_detach_idle(void);
int fr_virtual_name(const fr_virtual_display *display, char *name, unsigned size);
#ifdef __cplusplus
}
#endif
