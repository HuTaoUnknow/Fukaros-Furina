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
/* Uses a 1920x1200 landscape or 1200x1920 portrait / 60 Hz desktop. */
long fr_virtual_acquire_oriented(fr_virtual_display *display, int portrait);
long fr_virtual_acquire(fr_virtual_display *display);
long fr_virtual_release(fr_virtual_display *display);
long fr_virtual_detach_idle(void);
int fr_virtual_name(const fr_virtual_display *display, char *name, unsigned size);
/* Physical desktop coordinates, independent of Qt's DPI scaling. */
long fr_virtual_get_position(const fr_virtual_display *display, long *x, long *y);
long fr_virtual_set_position(fr_virtual_display *display, long x, long y);
#ifdef __cplusplus
}
#endif
