#ifndef FURINA_VIDEO_POLICY_H
#define FURINA_VIDEO_POLICY_H
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
#define FR_TARGET_FPS 60
#define FR_VIDEO_GOP (FR_TARGET_FPS * 5)
#define FR_VIDEO_HEADER 32
#define FR_VIDEO_KEY 1u
#define FR_VIDEO_CURSOR_SEPARATE 2u

typedef struct {
    uint32_t flags, sequence, width, height, epoch;
    uint64_t pts_us;
} fr_video_info;

typedef struct {
    int selected, edge;
    uint32_t low_ms, stable_ms, retry_ms;
} fr_video_policy;

int fr_resolution_valid(uint32_t edge);
int fr_video_bitrate(int width, int height);
void fr_video_policy_init(fr_video_policy* policy, int selected);
/* Compatibility helper for a one-second sample. FPS is a measured rate. */
int fr_video_policy_sample(fr_video_policy* policy, double fps, double work_ms, unsigned queue_ms);
/* Consecutive measured FPS <50 for 5000 ms lowers one rung. */
int fr_video_policy_sample_elapsed(fr_video_policy* policy, double fps, double work_ms, unsigned queue_ms,
                                   uint32_t elapsed_ms);
void fr_video_dimensions(int width, int height, int edge, int* out_w, int* out_h);
int fr_video_write(uint8_t* out, const fr_video_info* info);
int fr_video_read(const uint8_t* data, size_t size, fr_video_info* info);
#ifdef __cplusplus
}
#endif
#endif
