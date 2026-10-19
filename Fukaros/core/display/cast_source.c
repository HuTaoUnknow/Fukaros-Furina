#include "cast_source.h"

int fr_cast_source_usable(fr_cast_mode mode, const fr_cast_display* d, const fr_cast_display* p) {
    if (!d || d->width <= 0 || d->height <= 0)
        return 0;
    if (mode == FR_CAST_DUPLICATE)
        return 1;
    if (mode != FR_CAST_EXTEND || d->primary || !p || p->width <= 0 || p->height <= 0)
        return 0;
    /* Use 64-bit arithmetic for monitors left/above the primary and large coordinates. */
    return (int64_t)d->x + d->width <= p->x || (int64_t)p->x + p->width <= d->x ||
           (int64_t)d->y + d->height <= p->y || (int64_t)p->y + p->height <= d->y;
}
