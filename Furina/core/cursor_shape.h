#ifndef FURINA_CURSOR_SHAPE_H
#define FURINA_CURSOR_SHAPE_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Values match DXGI_OUTDUPL_POINTER_SHAPE_TYPE. Monochrome input height
   includes both AND and XOR planes; output height is half that value. */
enum { FR_CURSOR_MONO = 1, FR_CURSOR_COLOR = 2, FR_CURSOR_MASKED = 4 };
int fr_cursor_unpack(unsigned type, unsigned w, unsigned h, unsigned pitch,
                     const uint8_t *input, size_t size, uint8_t *bgra, size_t capacity);
void fr_cursor_pixel(unsigned type, const uint8_t *shape, uint8_t *background);
void fr_cursor_composite(unsigned type, const uint8_t *shape, int cw, int ch,
                         uint8_t *image, int w, int h, size_t stride, int x, int y);
#ifdef __cplusplus
}
#endif
#endif
