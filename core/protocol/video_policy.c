#include "video_policy.h"
#include "furina_protocol.h"
#include <string.h>

int fr_resolution_valid(uint32_t edge) {
    return edge == 0 || edge == 720 || edge == 1080 || edge == 1920;
}

int fr_video_bitrate(int width, int height) {
    int64_t rate = (int64_t)width * height * 10;
    return (int)(rate < 6000000 ? 6000000 : (rate > 28000000 ? 28000000 : rate));
}

void fr_video_policy_init(fr_video_policy* p, int selected) {
    memset(p, 0, sizeof(*p));
    p->selected = fr_resolution_valid(selected) ? selected : 0;
    p->edge = p->selected ? p->selected : 1080;
}

static uint32_t elapsed_add(uint32_t value, uint32_t elapsed) {
    return elapsed > UINT32_MAX - value ? UINT32_MAX : value + elapsed;
}

int fr_video_policy_sample_elapsed(fr_video_policy* p, double fps, double work_ms, unsigned queue_ms,
                                   uint32_t elapsed_ms) {
    (void)work_ms;
    (void)queue_ms;
    if (p->selected || !elapsed_ms)
        return 0;
    p->retry_ms = p->retry_ms > elapsed_ms ? p->retry_ms - elapsed_ms : 0;
    // Only sustained measured frame loss lowers quality. A slow key frame, a
    // work-time spike, or queued packets alone must not change the resolution.
    if (fps < 50.0) {
        p->low_ms = elapsed_add(p->low_ms, elapsed_ms);
        p->stable_ms = 0;
        if (p->low_ms >= 5000 && p->edge > 720) {
            p->edge = p->edge > 1440 ? 1440 : p->edge > 1080 ? 1080 : 720;
            p->low_ms = p->stable_ms = 0;
            p->retry_ms = 10000;
            return 1;
        }
        return 0;
    }
    p->low_ms = 0;
    p->stable_ms = fps >= 59.0 ? elapsed_add(p->stable_ms, elapsed_ms) : 0;
    // Probe progressively, including retrying higher rungs after recovery.
    // Ten seconds after a downgrade avoids immediately repeating a failed probe.
    if (p->stable_ms >= 5000 && !p->retry_ms && p->edge < 1920) {
        p->edge = p->edge < 1080 ? 1080 : p->edge < 1440 ? 1440 : 1920;
        p->low_ms = p->stable_ms = 0;
        return 1;
    }
    return 0;
}

int fr_video_policy_sample(fr_video_policy* p, double fps, double work_ms, unsigned queue_ms) {
    return fr_video_policy_sample_elapsed(p, fps, work_ms, queue_ms, 1000);
}

void fr_video_dimensions(int w, int h, int edge, int* out_w, int* out_h) {
    if (w < 2 || h < 2) {
        *out_w = *out_h = 0;
        return;
    }
    const int max = w > h ? w : h;
    double scale = max > edge ? (double)edge / max : 1.0;
    *out_w = ((int)(w * scale) / 2) * 2;
    *out_h = ((int)(h * scale) / 2) * 2;
    if (*out_w < 2)
        *out_w = 2;
    if (*out_h < 2)
        *out_h = 2;
}

int fr_video_write(uint8_t* p, const fr_video_info* i) {
    if (!p || !i || (i->flags & ~(FR_VIDEO_KEY | FR_VIDEO_CURSOR_SEPARATE)) || i->width < 2 ||
        i->height < 2 || i->width > 4096 || i->height > 4096 || (i->width & 1) || (i->height & 1))
        return 0;
    fr_put_u32(p, i->flags);
    fr_put_u32(p + 4, i->sequence);
    fr_put_u32(p + 8, i->width);
    fr_put_u32(p + 12, i->height);
    fr_put_u32(p + 16, (uint32_t)(i->pts_us >> 32));
    fr_put_u32(p + 20, (uint32_t)i->pts_us);
    fr_put_u32(p + 24, i->epoch);
    fr_put_u32(p + 28, 1);
    return 1;
}

int fr_video_read(const uint8_t* p, size_t n, fr_video_info* i) {
    if (!p || !i || n <= FR_VIDEO_HEADER || n > FR_MAX_PAYLOAD || fr_get_u32(p + 28) != 1)
        return 0;
    i->flags = fr_get_u32(p);
    i->sequence = fr_get_u32(p + 4);
    i->width = fr_get_u32(p + 8);
    i->height = fr_get_u32(p + 12);
    i->pts_us = ((uint64_t)fr_get_u32(p + 16) << 32) | fr_get_u32(p + 20);
    i->epoch = fr_get_u32(p + 24);
    return !(i->flags & ~(FR_VIDEO_KEY | FR_VIDEO_CURSOR_SEPARATE)) && i->width >= 2 && i->height >= 2 &&
           i->width <= 4096 && i->height <= 4096 && !(i->width & 1) && !(i->height & 1);
}
