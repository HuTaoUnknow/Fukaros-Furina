#pragma once
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    double x, y;
} fr_pointer_fraction;

/* Scale only moves wholly within the extended desktop; preserve crossings. */
int fr_pointer_gain(fr_pointer_fraction* fraction, int percent, long left, long top, long right, long bottom,
                    long x, long y, long next_x, long next_y, long* out_x, long* out_y);
#ifdef __cplusplus
}
#endif
