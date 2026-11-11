#ifndef FURINA_SECURITY_H
#define FURINA_SECURITY_H
/*
 * Wire version 2. Application frames stay FUR1 version 1 and travel inside
 * encrypted records. A peer that still speaks FUR1 in the clear is rejected.
 *
 * The record layer is mutually authenticated: each side proves its X25519
 * identity, and a high-entropy pairing secret (QR) or a 20-character code
 * is mixed into the key derivation. This is the same security goal as
 * TLS 1.3 with pinned keys plus an out-of-band pairing secret, using
 * Monocypher's X25519, BLAKE2b and XChaCha20-Poly1305.
 */
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define FR_WIRE_VERSION 2
#define FR_HS_CLIENT_HELLO 70
#define FR_HS_SERVER_HELLO 136
#define FR_HS_CLIENT_CONFIRM 72
#define FR_QR_PAYLOAD 83
#define FR_PAIRING_RAW 20
#define FR_PAIRING_TEXT 25

enum fr_security_error {
    FR_SEC_OK = 0,
    FR_SEC_LEGACY = 1,
    FR_SEC_VERSION = 2,
    FR_SEC_AUTH = 3,
    FR_SEC_PIN = 4,
    FR_SEC_FORMAT = 5,
    FR_SEC_EXPIRED = 6,
    FR_SEC_USED = 7,
    FR_SEC_LIMIT = 8,
    FR_SEC_BUFFER = 9
};

typedef struct {
    uint8_t secret[32];
    uint8_t public_key[32];
} fr_identity;

typedef struct {
    int is_server;
    int mode;
    int open;
    int error;
    int have_pin;
    fr_identity local;
    uint8_t eph_secret[32];
    uint8_t eph_public[32];
    uint8_t peer_static[32];
    uint8_t pinned[32];
    uint8_t psk[32];
    uint8_t transcript[160];
    size_t transcript_len;
    uint8_t confirm_key[32];
    uint8_t send_key[32];
    uint8_t recv_key[32];
    uint8_t cursor_key[32];
    uint8_t send_salt[16];
    uint8_t recv_salt[16];
    uint8_t server_box[32];
    uint8_t server_mac[16];
} fr_handshake;

typedef struct {
    int failures;
    int window_count;
    int64_t lock_until_ms;
    int64_t window_start_ms;
    int64_t last_fail_ms;
} fr_attempt_gate;

typedef struct {
    uint8_t ipv4[4];
    uint16_t port;
    int64_t expiry_unix;
    uint8_t secret[32];
    uint8_t public_key[32];
} fr_qr_info;

void fr_identity_from_secret(fr_identity* id, const uint8_t secret[32]);
void fr_identity_wipe(fr_identity* id);
void fr_fingerprint(const uint8_t public_key[32], char out[20]);
int fr_public_equal(const uint8_t a[32], const uint8_t b[32]);

int fr_pairing_code_valid(const char* text);
int fr_pairing_code_normalize(const char* text, char out[FR_PAIRING_RAW + 1]);
void fr_pairing_code_generate(char out[FR_PAIRING_TEXT], const uint8_t random[16]);
void fr_pairing_psk_from_code(uint8_t psk[32], const char normalized[FR_PAIRING_RAW + 1]);

int fr_qr_payload_build(uint8_t out[FR_QR_PAYLOAD], const uint8_t ipv4[4], uint16_t port, int64_t expiry_unix,
                        const uint8_t secret[32], const uint8_t public_key[32]);
int fr_qr_payload_parse(const uint8_t* in, size_t n, fr_qr_info* out);
int fr_qr_text_encode(char* out, size_t cap, const uint8_t payload[FR_QR_PAYLOAD]);
int fr_qr_text_decode(const char* text, uint8_t payload[FR_QR_PAYLOAD]);

/* Manual code, or a furina: QR string. QR fills info when info is non-NULL. */
int fr_credential_from_text(const char* text, uint8_t psk[32], fr_qr_info* info, int* error);

int fr_attempt_begin(fr_attempt_gate* gate, int64_t now_ms);
void fr_attempt_fail(fr_attempt_gate* gate, int64_t now_ms);
void fr_attempt_success(fr_attempt_gate* gate);

int fr_hs_client_start(fr_handshake* hs, const fr_identity* self, const uint8_t eph[32], const uint8_t psk[32],
                       int mode, const uint8_t* pin, uint8_t hello[FR_HS_CLIENT_HELLO]);
int fr_hs_server_start(fr_handshake* hs, const fr_identity* self, const uint8_t eph[32], const uint8_t psk[32],
                       const uint8_t* pin, const uint8_t hello[FR_HS_CLIENT_HELLO],
                       uint8_t reply[FR_HS_SERVER_HELLO]);
int fr_hs_client_finish(fr_handshake* hs, const uint8_t reply[FR_HS_SERVER_HELLO],
                        uint8_t confirm[FR_HS_CLIENT_CONFIRM]);
int fr_hs_server_finish(fr_handshake* hs, const uint8_t confirm[FR_HS_CLIENT_CONFIRM]);
void fr_hs_wipe(fr_handshake* hs);

int fr_record_seal(const uint8_t key[32], const uint8_t salt[16], uint64_t* counter, const uint8_t* plain,
                   size_t plain_len, uint8_t* out, size_t out_cap, size_t* out_len);
int fr_record_open(const uint8_t key[32], const uint8_t salt[16], uint64_t* counter, const uint8_t* input,
                   size_t input_len, size_t* consumed, uint8_t* plain, size_t plain_cap, size_t* plain_len);

int fr_cursor_seal(const uint8_t key[32], uint8_t direction, uint64_t* counter, const uint8_t* plain, size_t plain_len,
                   uint8_t* out, size_t out_cap, size_t* out_len);
int fr_cursor_open(const uint8_t key[32], uint8_t direction, uint64_t* counter, const uint8_t* input, size_t input_len,
                   size_t* consumed, uint8_t* plain, size_t plain_cap, size_t* plain_len);

int fr_qr_decode_luma(const uint8_t* luma, int width, int height, char* text, size_t text_cap);
const char* fr_security_error_text(int error);
#ifdef __cplusplus
}
#endif
#endif
