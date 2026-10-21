#pragma once
#include "virtual_display.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Only an acquired, dedicated virtual source is eligible; never a clone/main source. */
long fr_virtual_get_scale(const fr_virtual_display* display, int* percent);
long fr_virtual_set_scale(const fr_virtual_display* display, int percent, int* applied);
#ifdef __cplusplus
}
#endif
