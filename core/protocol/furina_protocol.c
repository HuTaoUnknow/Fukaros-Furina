#include "furina_protocol.h"
#include <math.h>

void fr_put_u32(uint8_t* p, uint32_t n) {
    p[0] = (uint8_t)(n >> 24);
    p[1] = (uint8_t)(n >> 16);
    p[2] = (uint8_t)(n >> 8);
    p[3] = (uint8_t)n;
}

uint32_t fr_get_u32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

int fr_encode_header(uint8_t* p, uint8_t t, uint32_t n) {
    if (!p || t < FR_CHALLENGE || t > FR_SCREEN_SCALE || n > FR_MAX_PAYLOAD)
        return 0;
    p[0] = 'F';
    p[1] = 'U';
    p[2] = 'R';
    p[3] = '1';
    p[4] = 1;
    p[5] = t;
    p[6] = p[7] = 0;
    fr_put_u32(p + 8, n);
    return 1;
}

int fr_decode_header(const uint8_t* p, fr_header* h) {
    if (!p || !h || p[0] != 'F' || p[1] != 'U' || p[2] != 'R' || p[3] != '1' || p[4] != 1 || p[6] || p[7] ||
        p[5] < FR_CHALLENGE || p[5] > FR_SCREEN_SCALE)
        return 0;
    h->type = p[5];
    h->length = fr_get_u32(p + 8);
    return h->length <= FR_MAX_PAYLOAD;
}

int fr_valid_approval(uint32_t id, uint32_t verb, uint32_t options) {
    const uint32_t direction = options & 3u;
    return id != 0 && verb >= FR_APPROVAL_REQUEST && verb <= FR_APPROVAL_CANCEL && (options & ~15u) == 0 &&
           (!(options & FR_APPROVAL_PORTRAIT) || (options & FR_APPROVAL_EXTEND)) &&
           (direction == FR_APPROVAL_SHARE || direction == FR_APPROVAL_RECEIVE);
}

int fr_valid_session_action(uint32_t action) {
    return action >= FR_START_SHARE && action <= FR_RESTORED_CONFIRMED;
}

int fr_encode_gesture(uint8_t* p, const fr_gesture* g) {
    if (!p || !g || g->x1 > 65535 || g->y1 > 65535 || g->x2 > 65535 || g->y2 > 65535 || g->duration_ms < 1 ||
        g->duration_ms > 2000)
        return 0;
    fr_put_u32(p, g->x1);
    fr_put_u32(p + 4, g->y1);
    fr_put_u32(p + 8, g->x2);
    fr_put_u32(p + 12, g->y2);
    fr_put_u32(p + 16, g->duration_ms);
    return 1;
}

int fr_decode_gesture(const uint8_t* p, size_t n, fr_gesture* g) {
    if (!p || !g || n != 20)
        return 0;
    g->x1 = fr_get_u32(p);
    g->y1 = fr_get_u32(p + 4);
    g->x2 = fr_get_u32(p + 8);
    g->y2 = fr_get_u32(p + 12);
    g->duration_ms = fr_get_u32(p + 16);
    return g->x1 <= 65535 && g->y1 <= 65535 && g->x2 <= 65535 && g->y2 <= 65535 && g->duration_ms >= 1 &&
           g->duration_ms <= 2000;
}

int fr_map_point(double x, double y, int vw, int vh, int iw, int ih, uint32_t* nx, uint32_t* ny) {
    double s, w, h, left, top;
    if (!nx || !ny || vw <= 0 || vh <= 0 || iw <= 0 || ih <= 0 || !isfinite(x) || !isfinite(y))
        return 0;
    s = (double)vw / iw;
    if ((double)vh / ih < s)
        s = (double)vh / ih;
    w = iw * s;
    h = ih * s;
    left = (vw - w) / 2;
    top = (vh - h) / 2;
    if (x < left || y < top || x >= left + w || y >= top + h)
        return 0;
    *nx = (uint32_t)((x - left) / w * 65535.0 + 0.5);
    *ny = (uint32_t)((y - top) / h * 65535.0 + 0.5);
    return 1;
}
