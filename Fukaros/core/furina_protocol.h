#ifndef FURINA_PROTOCOL_H
#define FURINA_PROTOCOL_H
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
#define FR_HEADER_SIZE 12
#define FR_MAX_PAYLOAD (8u * 1024u * 1024u)
enum fr_type { FR_CHALLENGE=1, FR_AUTH=2, FR_READY=3, FR_FRAME=4, FR_GESTURE=5, FR_KEY=6, FR_STATUS=7, FR_PING=8, FR_PONG=9, FR_SESSION=10, FR_VIDEO=11, FR_PROFILE=12, FR_FEEDBACK=13, FR_APPROVAL=14 };
enum fr_session_action { FR_START_SHARE=1, FR_STOP_SHARE=2, FR_DUPLICATE=3, FR_EXTEND=4, FR_MAXIMIZE=5, FR_RESTORE=6, FR_START_RECEIVE=7, FR_STOP_RECEIVE=8, FR_REQUEST_KEY=9 };
enum fr_approval_verb { FR_APPROVAL_REQUEST=1, FR_APPROVAL_ACCEPT=2, FR_APPROVAL_REJECT=3, FR_APPROVAL_CANCEL=4 };
enum fr_approval_option { FR_APPROVAL_SHARE=1, FR_APPROVAL_RECEIVE=2, FR_APPROVAL_EXTEND=4 };
int fr_valid_approval(uint32_t id,uint32_t verb,uint32_t options);
typedef struct { uint8_t type; uint32_t length; } fr_header;
typedef struct { uint32_t x1,y1,x2,y2,duration_ms; } fr_gesture;
void fr_put_u32(uint8_t *p, uint32_t n);
uint32_t fr_get_u32(const uint8_t *p);
int fr_valid_session_action(uint32_t action);
int fr_encode_header(uint8_t out[FR_HEADER_SIZE], uint8_t type, uint32_t length);
int fr_decode_header(const uint8_t in[FR_HEADER_SIZE], fr_header *out);
int fr_encode_gesture(uint8_t out[20], const fr_gesture *g);
int fr_decode_gesture(const uint8_t *in, size_t n, fr_gesture *g);
/* Map a pointer in a letterboxed viewport to normalized 0..65535 content coordinates. */
int fr_map_point(double x,double y,int view_w,int view_h,int image_w,int image_h,uint32_t *nx,uint32_t *ny);
#ifdef __cplusplus
}
#endif
#endif
