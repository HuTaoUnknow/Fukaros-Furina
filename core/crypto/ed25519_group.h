#ifndef FURINA_ED25519_GROUP_H
#define FURINA_ED25519_GROUP_H
/*
 * Prime-order-subgroup arithmetic on edwards25519 (RFC 8032).
 * Scalars are 32-byte little-endian integers reduced modulo L.
 * Points are 32-byte compressed Edwards y coordinates with the x sign
 * in bit 255, the same encoding SPAKE2 uses for edwards25519 (RFC 9382).
 */
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* out = (in mod L), in is 64-byte little-endian. Constant time. */
void sc_reduce512(uint8_t out[32], const uint8_t in[64]);
int sc_is_zero(const uint8_t s[32]);

/* 1 when p is on the curve, in the prime-order subgroup, and not identity. */
int ed_point_valid(const uint8_t p[32]);
void ed_basepoint(uint8_t out[32]);

/* Return 1 on success. Scalars must already be reduced and non-zero. */
int ed_scalarmult(uint8_t out[32], const uint8_t scalar_le[32], const uint8_t point[32]);
int ed_scalarmult_base(uint8_t out[32], const uint8_t scalar_le[32]);
int ed_add(uint8_t out[32], const uint8_t a[32], const uint8_t b[32]);
int ed_sub(uint8_t out[32], const uint8_t a[32], const uint8_t b[32]);

#ifdef __cplusplus
}
#endif
#endif
