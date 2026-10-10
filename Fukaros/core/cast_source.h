#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef enum { FR_CAST_DUPLICATE = 0, FR_CAST_EXTEND = 1 } fr_cast_mode;
typedef struct { int32_t x, y, width, height; int primary; } fr_cast_display;
/* Extend accepts only a separate desktop area, never a primary or cloned screen. */
int fr_cast_source_usable(fr_cast_mode mode, const fr_cast_display *display,
                          const fr_cast_display *primary);
#ifdef __cplusplus
}
#endif
