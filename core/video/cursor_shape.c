#include "cursor_shape.h"
#include <string.h>

int fr_cursor_unpack(unsigned type, unsigned w, unsigned h, unsigned pitch, const uint8_t* input, size_t size,
                     uint8_t* out, size_t capacity) {
    unsigned rows = type == FR_CURSOR_MONO ? h / 2 : h;
    if (!input || !out || !w || !rows || w > 512 || h > 1024 ||
        (type != FR_CURSOR_MONO && type != FR_CURSOR_COLOR && type != FR_CURSOR_MASKED) ||
        (type == FR_CURSOR_MONO && (h & 1)) || pitch < (type == FR_CURSOR_MONO ? (w + 7) / 8 : w * 4) ||
        !pitch || h > size / pitch || capacity < (size_t)w * rows * 4)
        return 0;
    for (unsigned y = 0; y < rows; ++y) {
        if (type != FR_CURSOR_MONO) {
            memcpy(out + (size_t)y * w * 4, input + (size_t)y * pitch, w * 4);
            continue;
        }
        for (unsigned x = 0; x < w; ++x) {
            uint8_t* p = out + ((size_t)y * w + x) * 4;
            unsigned bit = 0x80u >> (x & 7);
            p[0] = 0;
            p[1] = (input[(size_t)(y + rows) * pitch + x / 8] & bit) ? 255 : 0;
            p[2] = (input[(size_t)y * pitch + x / 8] & bit) ? 255 : 0;
            p[3] = 255;
        }
    }
    return 1;
}

void fr_cursor_pixel(unsigned type, const uint8_t* p, uint8_t* b) {
    for (int c = 0; c < 3; ++c) {
        if (type == FR_CURSOR_MONO)
            b[c] = (b[c] & p[2]) ^ p[1];
        else if (type == FR_CURSOR_MASKED)
            b[c] = p[3] ? b[c] ^ p[c] : p[c];
        else
            b[c] = (uint8_t)((p[c] * p[3] + b[c] * (255 - p[3]) + 127) / 255);
    }
    b[3] = 255;
}

void fr_cursor_composite(unsigned type, const uint8_t* shape, int cw, int ch, uint8_t* image, int w, int h,
                         size_t stride, int x, int y) {
    if (!shape || !image || cw <= 0 || ch <= 0 || w <= 0 || h <= 0 || stride < (size_t)w * 4)
        return;
    for (int cy = 0; cy < ch; ++cy) {
        int dy = y + cy;
        if (dy < 0 || dy >= h)
            continue;
        for (int cx = 0; cx < cw; ++cx) {
            int dx = x + cx;
            if (dx < 0 || dx >= w)
                continue;
            fr_cursor_pixel(type, shape + ((size_t)cy * cw + cx) * 4, image + (size_t)dy * stride + dx * 4);
        }
    }
}
