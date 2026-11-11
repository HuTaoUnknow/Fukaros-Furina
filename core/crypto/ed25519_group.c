#include "ed25519_group.h"
#include <string.h>

/* 51-bit limbs, value = sum limb[i] * 2^(51*i), reduced mod 2^255-19. */
typedef uint64_t fe[5];

#define FE_MASK ((1ULL << 51) - 1)

typedef struct {
    fe x, y, z, t;
} ge;

static void fe_0(fe r) {
    memset(r, 0, sizeof(fe));
}

static void fe_1(fe r) {
    fe_0(r);
    r[0] = 1;
}

static void fe_copy(fe r, const fe a) {
    memcpy(r, a, sizeof(fe));
}

static void fe_cmov(fe f, const fe g, uint64_t b) {
    uint64_t mask = 0ULL - b;
    for (int i = 0; i < 5; ++i)
        f[i] ^= mask & (f[i] ^ g[i]);
}

static void fe_carry(fe t) {
    uint64_t c;
    c = t[0] >> 51;
    t[0] &= FE_MASK;
    t[1] += c;
    c = t[1] >> 51;
    t[1] &= FE_MASK;
    t[2] += c;
    c = t[2] >> 51;
    t[2] &= FE_MASK;
    t[3] += c;
    c = t[3] >> 51;
    t[3] &= FE_MASK;
    t[4] += c;
    c = t[4] >> 51;
    t[4] &= FE_MASK;
    t[0] += c * 19;
    c = t[0] >> 51;
    t[0] &= FE_MASK;
    t[1] += c;
}

static void fe_freeze(fe t) {
    fe_carry(t);
    fe_carry(t);
    uint64_t t0 = t[0] + 19;
    uint64_t c = t0 >> 51;
    t0 &= FE_MASK;
    uint64_t t1 = t[1] + c;
    c = t1 >> 51;
    t1 &= FE_MASK;
    uint64_t t2 = t[2] + c;
    c = t2 >> 51;
    t2 &= FE_MASK;
    uint64_t t3 = t[3] + c;
    c = t3 >> 51;
    t3 &= FE_MASK;
    uint64_t t4 = t[4] + c;
    c = t4 >> 51;
    t4 &= FE_MASK;
    uint64_t mask = 0ULL - c;
    t[0] = (t[0] & ~mask) | (t0 & mask);
    t[1] = (t[1] & ~mask) | (t1 & mask);
    t[2] = (t[2] & ~mask) | (t2 & mask);
    t[3] = (t[3] & ~mask) | (t3 & mask);
    t[4] = (t[4] & ~mask) | (t4 & mask);
}

static void fe_add(fe r, const fe a, const fe b) {
    for (int i = 0; i < 5; ++i)
        r[i] = a[i] + b[i];
    fe_carry(r);
}

static void fe_sub(fe r, const fe a, const fe b) {
    /* Add 16*p so every limb stays non-negative for reduced inputs. */
    const uint64_t k = 16;
    __int128 t0 = (__int128)a[0] - b[0] + (__int128)k * ((1ULL << 51) - 19);
    __int128 t1 = (__int128)a[1] - b[1] + (__int128)k * FE_MASK;
    __int128 t2 = (__int128)a[2] - b[2] + (__int128)k * FE_MASK;
    __int128 t3 = (__int128)a[3] - b[3] + (__int128)k * FE_MASK;
    __int128 t4 = (__int128)a[4] - b[4] + (__int128)k * FE_MASK;
    __int128 c;
    c = t0 >> 51;
    t0 &= FE_MASK;
    t1 += c;
    c = t1 >> 51;
    t1 &= FE_MASK;
    t2 += c;
    c = t2 >> 51;
    t2 &= FE_MASK;
    t3 += c;
    c = t3 >> 51;
    t3 &= FE_MASK;
    t4 += c;
    c = t4 >> 51;
    t4 &= FE_MASK;
    t0 += c * 19;
    c = t0 >> 51;
    t0 &= FE_MASK;
    t1 += c;
    c = t1 >> 51;
    t1 &= FE_MASK;
    t2 += c;
    r[0] = (uint64_t)t0;
    r[1] = (uint64_t)t1;
    r[2] = (uint64_t)t2;
    r[3] = (uint64_t)t3;
    r[4] = (uint64_t)t4;
}

static void fe_mul(fe r, const fe a, const fe b) {
    __int128 t[5] = {0, 0, 0, 0, 0};
    for (int i = 0; i < 5; ++i) {
        for (int j = 0; j < 5; ++j) {
            int k = i + j;
            __int128 p = (__int128)a[i] * (__int128)b[j];
            if (k >= 5) {
                p *= 19;
                k -= 5;
            }
            t[k] += p;
        }
    }
    __int128 c;
    c = t[0] >> 51;
    t[0] &= FE_MASK;
    t[1] += c;
    c = t[1] >> 51;
    t[1] &= FE_MASK;
    t[2] += c;
    c = t[2] >> 51;
    t[2] &= FE_MASK;
    t[3] += c;
    c = t[3] >> 51;
    t[3] &= FE_MASK;
    t[4] += c;
    c = t[4] >> 51;
    t[4] &= FE_MASK;
    t[0] += c * 19;
    c = t[0] >> 51;
    t[0] &= FE_MASK;
    t[1] += c;
    c = t[1] >> 51;
    t[1] &= FE_MASK;
    t[2] += c;
    r[0] = (uint64_t)t[0];
    r[1] = (uint64_t)t[1];
    r[2] = (uint64_t)t[2];
    r[3] = (uint64_t)t[3];
    r[4] = (uint64_t)t[4];
}

static void fe_sq(fe r, const fe a) {
    fe_mul(r, a, a);
}

static void fe_neg(fe r, const fe a) {
    fe z;
    fe_0(z);
    fe_sub(r, z, a);
}

static void fe_pow(fe out, const fe base, const uint8_t exp_le[32], int bits) {
    fe result;
    fe_1(result);
    fe b;
    fe_copy(b, base);
    for (int i = bits - 1; i >= 0; --i) {
        fe_sq(result, result);
        fe doubled;
        fe_mul(doubled, result, b);
        uint64_t bit = (exp_le[i >> 3] >> (i & 7)) & 1;
        fe_cmov(result, doubled, bit);
    }
    fe_copy(out, result);
}

/* exponent p-2 = 2^255-21 for inversion. */
static void fe_invert(fe out, const fe z) {
    uint8_t exp[32];
    /* 2^255 - 21 as a 255-bit little-endian integer (bit 255 is not stored). */
    memset(exp, 0xff, 32);
    exp[0] = 0xeb; /* bits: 0 and 1 set, 2 clear, 3 set, 4 clear, 5.. set → 11101011 = 0xEB */
    exp[31] = 0x7f;
    fe_pow(out, z, exp, 255);
}

static int fe_is_zero(const fe a) {
    fe t;
    fe_copy(t, a);
    fe_freeze(t);
    uint64_t v = t[0] | t[1] | t[2] | t[3] | t[4];
    return v == 0;
}

static int fe_equal(const fe a, const fe b) {
    fe d;
    fe_sub(d, a, b);
    return fe_is_zero(d);
}

static void fe_tobytes(uint8_t s[32], const fe f) {
    fe t;
    fe_copy(t, f);
    fe_freeze(t);
    uint64_t n[5] = {t[0], t[1], t[2], t[3], t[4]};
    memset(s, 0, 32);
    int bit = 0;
    for (int limb = 0; limb < 5; ++limb) {
        uint64_t v = n[limb];
        int width = 51;
        if (limb == 4)
            width = 51; /* bit 255 is not part of a canonical field element */
        for (int i = 0; i < width && bit < 255; ++i, ++bit) {
            if ((v >> i) & 1)
                s[bit >> 3] |= (uint8_t)(1u << (bit & 7));
        }
    }
}

static void fe_frombytes(fe h, const uint8_t s[32]) {
    fe_0(h);
    for (int bit = 0; bit < 255; ++bit) {
        if ((s[bit >> 3] >> (bit & 7)) & 1)
            h[bit / 51] |= 1ULL << (bit % 51);
    }
}

static int fe_is_negative(const fe f) {
    uint8_t s[32];
    fe_tobytes(s, f);
    return s[0] & 1;
}

static void ge_identity(ge* r) {
    fe_0(r->x);
    fe_1(r->y);
    fe_1(r->z);
    fe_0(r->t);
}

static void ge_cmov(ge* f, const ge* g, uint64_t b) {
    fe_cmov(f->x, g->x, b);
    fe_cmov(f->y, g->y, b);
    fe_cmov(f->z, g->z, b);
    fe_cmov(f->t, g->t, b);
}

/* d = -121665 / 121666, d2 = 2*d. Both writers store the same bytes. */
static fe FE_D, FE_D2;
static int fe_consts_ready = 0;

static void fe_consts(void) {
    if (__atomic_load_n(&fe_consts_ready, __ATOMIC_ACQUIRE))
        return;
    fe n, d, inv, D, D2;
    fe_0(n);
    n[0] = 121665;
    fe_neg(n, n);
    fe_0(d);
    d[0] = 121666;
    fe_invert(inv, d);
    fe_mul(D, n, inv);
    fe_add(D2, D, D);
    memcpy(FE_D, D, sizeof FE_D);
    memcpy(FE_D2, D2, sizeof FE_D2);
    __atomic_store_n(&fe_consts_ready, 1, __ATOMIC_RELEASE);
}

/* dbl-2008-hwcd, a = -1. */
static void ge_dbl(ge* r, const ge* p) {
    fe A, B, C, D, E, F, G, H, XY, Z2;
    fe_sq(A, p->x);
    fe_sq(B, p->y);
    fe_sq(Z2, p->z);
    fe_add(C, Z2, Z2);
    fe_neg(D, A); /* a*A with a=-1 */
    fe_add(XY, p->x, p->y);
    fe_sq(E, XY);
    fe_sub(E, E, A);
    fe_sub(E, E, B); /* 2XY */
    fe_add(G, D, B);
    fe_sub(F, G, C);
    fe_sub(H, D, B);
    fe_mul(r->x, E, F);
    fe_mul(r->y, G, H);
    fe_mul(r->t, E, H);
    fe_mul(r->z, F, G);
}

/* add-2008-hwcd-3. */
static void ge_add(ge* r, const ge* p, const ge* q) {
    fe_consts();
    fe A, B, C, D, E, F, G, H, t;
    fe_sub(A, p->y, p->x);
    fe_sub(t, q->y, q->x);
    fe_mul(A, A, t);
    fe_add(B, p->y, p->x);
    fe_add(t, q->y, q->x);
    fe_mul(B, B, t);
    fe_mul(C, p->t, q->t);
    fe_mul(C, C, FE_D2);
    fe_mul(D, p->z, q->z);
    fe_add(D, D, D);
    fe_sub(E, B, A);
    fe_sub(F, D, C);
    fe_add(G, D, C);
    fe_add(H, B, A);
    fe_mul(r->x, E, F);
    fe_mul(r->y, G, H);
    fe_mul(r->t, E, H);
    fe_mul(r->z, F, G);
}

static void ge_neg(ge* r, const ge* p) {
    fe_neg(r->x, p->x);
    fe_copy(r->y, p->y);
    fe_copy(r->z, p->z);
    fe_neg(r->t, p->t);
}

static void ge_scalarmult(ge* r, const uint8_t s[32], const ge* p) {
    ge acc;
    ge_identity(&acc);
    ge base = *p;
    for (int i = 255; i >= 0; --i) {
        ge_dbl(&acc, &acc);
        ge sum;
        ge_add(&sum, &acc, &base);
        uint64_t bit = (s[i >> 3] >> (i & 7)) & 1U;
        ge_cmov(&acc, &sum, bit);
    }
    *r = acc;
}

static int ge_frombytes(ge* r, const uint8_t s[32]) {
    fe_consts();
    uint8_t yb[32];
    memcpy(yb, s, 32);
    int sign = yb[31] >> 7;
    yb[31] &= 127;
    /* Reject y >= p. p = 2^255-19, little-endian, with bit 255 clear. */
    static const uint8_t pbytes[32] = {0xed, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                                        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                                        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x7f};
    int high = 0;
    for (int i = 31; i >= 0; --i) {
        if (yb[i] > pbytes[i]) {
            high = 1;
            break;
        }
        if (yb[i] < pbytes[i])
            break;
        if (i == 0)
            high = 1; /* equal to p, not canonical */
    }
    if (high)
        return 0;

    fe y;
    fe_frombytes(y, yb);
    fe y2, u, v, one;
    fe_sq(y2, y);
    fe_1(one);
    fe_sub(u, y2, one);
    fe dy2;
    fe_mul(dy2, FE_D, y2);
    fe_add(v, dy2, one);

    /* x = u * v^3 * (u * v^7)^((p-5)/8), (p-5)/8 = 2^252 - 3. */
    fe v2, v3, v6, v7, uv7, pow, x;
    fe_sq(v2, v);
    fe_mul(v3, v2, v);
    fe_sq(v6, v3);
    fe_mul(v7, v6, v);
    fe_mul(uv7, u, v7);
    uint8_t exp[32];
    memset(exp, 0xff, 32);
    exp[0] = 0xfd; /* 2^252-3: bit0=1, bit1=0, bits 2..251 = 1. 0xFD = 11111101 */
    exp[31] = 0x0f; /* bits 248..251 set, 252..255 clear. 2^252-3 top bit is 251. */
    fe_pow(pow, uv7, exp, 252);
    fe_mul(x, u, v3);
    fe_mul(x, x, pow);

    fe x2, vx2;
    fe_sq(x2, x);
    fe_mul(vx2, v, x2);
    if (!fe_equal(vx2, u)) {
        fe nu;
        fe_neg(nu, u);
        if (!fe_equal(vx2, nu))
            return 0;
        /* sqrt(-1) = 2^((p-1)/4). */
        fe two, root;
        fe_0(two);
        two[0] = 2;
        uint8_t e2[32];
        memset(e2, 0xff, 32);
        e2[0] = 0xfb; /* (p-1)/4 = 2^253 - 5 = bits: low ... compute 2^253-5.
                         2^n - 5 = all 1s except bits 0 and 2? 
                         5 = 101b. 2^253-5: bit0=1, bit1=1, bit2=0, then 1s through bit 252.
                         Wait: all-1s minus 4 (100b) because 5=4+1 and all-1s - 5 = ... 
                         (2^253 - 1) - 4 = 2^253 - 5. all-1s (253 bits) minus 4 clears bit 2.
                         bit0=1, bit1=1, bit2=0, bits 3..252 = 1.
                         byte0 = 11111011 = 0xFB. Yes. */
        e2[31] = 0x1f; /* bits 248..252 */
        fe_pow(root, two, e2, 253);
        fe_mul(x, x, root);
        fe_sq(x2, x);
        fe_mul(vx2, v, x2);
        if (!fe_equal(vx2, u))
            return 0;
    }
    if (fe_is_zero(x) && sign)
        return 0;
    if (fe_is_negative(x) != sign)
        fe_neg(x, x);
    fe_copy(r->x, x);
    fe_copy(r->y, y);
    fe_1(r->z);
    fe_mul(r->t, x, y);
    return 1;
}

static void ge_tobytes(uint8_t s[32], const ge* p) {
    fe zi, x, y;
    fe_invert(zi, p->z);
    fe_mul(x, p->x, zi);
    fe_mul(y, p->y, zi);
    fe_tobytes(s, y);
    if (fe_is_negative(x))
        s[31] |= 0x80;
}

static int ge_is_identity(const ge* p) {
    uint8_t s[32];
    ge_tobytes(s, p);
    uint8_t id[32] = {1};
    return memcmp(s, id, 32) == 0;
}

/* L = 2^252 + 27742317777372353535851937790883648493 */
static const uint8_t L_BYTES[32] = {0xed, 0xd3, 0xf5, 0x5c, 0x1a, 0x63, 0x12, 0x58, 0xd6, 0x9c, 0xf7,
                                    0xa2, 0xde, 0xf9, 0xde, 0x14, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10};

static int bit_of(const uint8_t* s, int n, int nbytes) {
    if (n < 0 || n >= nbytes * 8)
        return 0;
    return (s[n >> 3] >> (n & 7)) & 1;
}

void sc_reduce512(uint8_t out[32], const uint8_t in[64]) {
    uint8_t x[64];
    memcpy(x, in, 64);
    /* Subtract L<<shift whenever the 512-bit integer is large enough. */
    for (int shift = 259; shift >= 0; --shift) {
        uint8_t tmp[64];
        unsigned borrow = 0;
        for (int i = 0; i < 64; ++i) {
            unsigned lbyte = 0;
            for (int k = 0; k < 8; ++k)
                lbyte |= (unsigned)bit_of(L_BYTES, i * 8 + k - shift, 32) << k;
            unsigned d = (unsigned)x[i] - lbyte - borrow;
            borrow = d >> 31;
            tmp[i] = (uint8_t)d;
        }
        uint8_t m = (uint8_t)(0u - borrow); /* 0xff keeps x when subtraction underflowed */
        for (int i = 0; i < 64; ++i)
            x[i] = (uint8_t)((tmp[i] & (uint8_t)~m) | (x[i] & m));
    }
    memcpy(out, x, 32);
    memset(x, 0, sizeof x);
}

int sc_is_zero(const uint8_t s[32]) {
    unsigned v = 0;
    for (int i = 0; i < 32; ++i)
        v |= s[i];
    return v == 0;
}

static int ge_prime_order(const ge* p) {
    ge r;
    ge_scalarmult(&r, L_BYTES, p);
    return ge_is_identity(&r);
}

int ed_point_valid(const uint8_t p[32]) {
    ge g;
    if (!ge_frombytes(&g, p))
        return 0;
    if (ge_is_identity(&g))
        return 0;
    return ge_prime_order(&g);
}

void ed_basepoint(uint8_t out[32]) {
    /* RFC 8032 base point. */
    static const uint8_t b[32] = {0x58, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66,
                                  0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66,
                                  0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66};
    memcpy(out, b, 32);
}

static int ge_load_valid(ge* g, const uint8_t p[32]) {
    if (!ge_frombytes(g, p))
        return 0;
    if (ge_is_identity(g))
        return 0;
    if (!ge_prime_order(g))
        return 0;
    return 1;
}

int ed_scalarmult(uint8_t out[32], const uint8_t scalar_le[32], const uint8_t point[32]) {
    if (sc_is_zero(scalar_le))
        return 0;
    ge g;
    if (!ge_load_valid(&g, point))
        return 0;
    ge r;
    ge_scalarmult(&r, scalar_le, &g);
    ge_tobytes(out, &r);
    return 1;
}

int ed_scalarmult_base(uint8_t out[32], const uint8_t scalar_le[32]) {
    uint8_t b[32];
    ed_basepoint(b);
    return ed_scalarmult(out, scalar_le, b);
}

int ed_add(uint8_t out[32], const uint8_t a[32], const uint8_t b[32]) {
    ge ga, gb;
    if (!ge_frombytes(&ga, a) || !ge_frombytes(&gb, b))
        return 0;
    ge r;
    ge_add(&r, &ga, &gb);
    ge_tobytes(out, &r);
    return 1;
}

int ed_sub(uint8_t out[32], const uint8_t a[32], const uint8_t b[32]) {
    ge ga, gb;
    if (!ge_frombytes(&ga, a) || !ge_frombytes(&gb, b))
        return 0;
    ge nb;
    ge_neg(&nb, &gb);
    ge r;
    ge_add(&r, &ga, &nb);
    ge_tobytes(out, &r);
    return 1;
}

