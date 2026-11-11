#include "furina_security.h"
#include "monocypher.h"
#include "quirc.h"
#include <string.h>

static const char k_alphabet[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";

static int nonzero(const uint8_t* p, size_t n) {
    uint8_t x = 0;
    size_t i;
    for (i = 0; i < n; i++)
        x |= p[i];
    return x != 0;
}

static void put_u16(uint8_t* p, uint16_t n) {
    p[0] = (uint8_t)(n >> 8);
    p[1] = (uint8_t)n;
}

static void put_u32(uint8_t* p, uint32_t n) {
    p[0] = (uint8_t)(n >> 24);
    p[1] = (uint8_t)(n >> 16);
    p[2] = (uint8_t)(n >> 8);
    p[3] = (uint8_t)n;
}

static void put_u64(uint8_t* p, uint64_t n) {
    int i;
    for (i = 0; i < 8; i++)
        p[i] = (uint8_t)(n >> (56 - 8 * i));
}

static uint16_t get_u16(const uint8_t* p) {
    return (uint16_t)((p[0] << 8) | p[1]);
}

static uint32_t get_u32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static uint64_t get_u64(const uint8_t* p) {
    uint64_t n = 0;
    int i;
    for (i = 0; i < 8; i++)
        n = (n << 8) | p[i];
    return n;
}

static int fail(fr_handshake* hs, int error) {
    if (hs)
        hs->error = error;
    return 0;
}

const char* fr_security_error_text(int error) {
    switch (error) {
    case FR_SEC_LEGACY:
        return "Peer still uses the v1 cleartext protocol. Update both devices to 0.4.21.";
    case FR_SEC_VERSION:
        return "Unsupported pairing protocol version.";
    case FR_SEC_AUTH:
        return "Pairing authentication failed.";
    case FR_SEC_PIN:
        return "Peer key does not match the pinned fingerprint.";
    case FR_SEC_FORMAT:
        return "Pairing code must be 20 letters or digits, grouped with dashes, without I, O, 0 or 1.";
    case FR_SEC_EXPIRED:
        return "Pairing offer has expired.";
    case FR_SEC_USED:
        return "Pairing offer was already used.";
    case FR_SEC_LIMIT:
        return "Too many pairing attempts. Wait and try again.";
    default:
        return "Pairing failed.";
    }
}

void fr_identity_from_secret(fr_identity* id, const uint8_t secret[32]) {
    memcpy(id->secret, secret, 32);
    crypto_x25519_public_key(id->public_key, id->secret);
}

void fr_identity_wipe(fr_identity* id) {
    crypto_wipe(id, sizeof *id);
}

int fr_public_equal(const uint8_t a[32], const uint8_t b[32]) {
    return a && b && crypto_verify32(a, b) == 0;
}

void fr_fingerprint(const uint8_t public_key[32], char out[20]) {
    uint8_t hash[8];
    int i, j = 0;
    crypto_blake2b(hash, sizeof hash, public_key, 32);
    for (i = 0; i < 8; i++) {
        static const char hex[] = "0123456789ABCDEF";
        if (i && (i % 2) == 0)
            out[j++] = '-';
        out[j++] = hex[hash[i] >> 4];
        out[j++] = hex[hash[i] & 15];
    }
    out[j] = 0;
}

static int alphabet_index(char c) {
    const char* p;
    if (c >= 'a' && c <= 'z')
        c = (char)(c - 'a' + 'A');
    p = strchr(k_alphabet, c);
    return p ? (int)(p - k_alphabet) : -1;
}

int fr_pairing_code_normalize(const char* text, char out[FR_PAIRING_RAW + 1]) {
    int n = 0, letters = 0, digits = 0;
    if (!text)
        return 0;
    for (; *text; text++) {
        char c = *text;
        int index;
        if (c == '-' || c == ' ')
            continue;
        index = alphabet_index(c);
        if (index < 0 || n >= FR_PAIRING_RAW)
            return 0;
        out[n++] = k_alphabet[index];
        if (k_alphabet[index] >= '2' && k_alphabet[index] <= '9')
            digits++;
        else
            letters++;
    }
    out[n] = 0;
    return n == FR_PAIRING_RAW && letters > 0 && digits > 0;
}

int fr_pairing_code_valid(const char* text) {
    char out[FR_PAIRING_RAW + 1];
    return fr_pairing_code_normalize(text, out);
}

void fr_pairing_code_generate(char out[FR_PAIRING_TEXT], const uint8_t random[16]) {
    char raw[FR_PAIRING_RAW + 1];
    int i, bit = 0, j = 0, letters = 0, digits = 0;
    for (i = 0; i < FR_PAIRING_RAW; i++) {
        int value = 0, b;
        for (b = 0; b < 5; b++, bit++)
            value = (value << 1) | ((random[bit / 8] >> (7 - (bit % 8))) & 1);
        raw[i] = k_alphabet[value];
        if (raw[i] >= '2' && raw[i] <= '9')
            digits++;
        else
            letters++;
    }
    if (!letters)
        raw[0] = k_alphabet[random[15] % 24];
    if (!digits)
        raw[FR_PAIRING_RAW - 1] = k_alphabet[24 + (random[14] % 8)];
    raw[FR_PAIRING_RAW] = 0;
    for (i = 0; i < FR_PAIRING_RAW; i++) {
        if (i && i % 4 == 0)
            out[j++] = '-';
        out[j++] = raw[i];
    }
    out[j] = 0;
}

void fr_pairing_psk_from_code(uint8_t psk[32], const char normalized[FR_PAIRING_RAW + 1]) {
    static const char label[] = "furina-manual-psk-v2";
    crypto_blake2b_keyed(psk, 32, (const uint8_t*)label, sizeof label - 1, (const uint8_t*)normalized,
                         FR_PAIRING_RAW);
}

static const char k_b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

static int b64_index(char c) {
    const char* p = strchr(k_b64, c);
    return p ? (int)(p - k_b64) : -1;
}

static int b64_encode(char* out, size_t cap, const uint8_t* in, size_t n) {
    size_t i = 0, o = 0;
    while (i < n) {
        uint32_t v = (uint32_t)in[i++] << 16;
        int remain = 1;
        if (i < n) {
            v |= (uint32_t)in[i++] << 8;
            remain = 2;
        }
        if (i < n) {
            v |= in[i++];
            remain = 3;
        }
        if (o + 4 >= cap)
            return 0;
        out[o++] = k_b64[(v >> 18) & 63];
        out[o++] = k_b64[(v >> 12) & 63];
        if (remain >= 2)
            out[o++] = k_b64[(v >> 6) & 63];
        if (remain == 3)
            out[o++] = k_b64[v & 63];
    }
    if (o >= cap)
        return 0;
    out[o] = 0;
    return 1;
}

static int b64_decode(uint8_t* out, size_t cap, size_t* out_n, const char* text) {
    uint32_t acc = 0;
    int bits = 0;
    size_t n = 0;
    for (; *text; text++) {
        int v;
        if (*text == '=' || *text == '\n' || *text == '\r')
            continue;
        v = b64_index(*text);
        if (v < 0)
            return 0;
        acc = (acc << 6) | (uint32_t)v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            if (n >= cap)
                return 0;
            out[n++] = (uint8_t)((acc >> bits) & 0xff);
        }
    }
    *out_n = n;
    return 1;
}

int fr_qr_payload_build(uint8_t out[FR_QR_PAYLOAD], const uint8_t ipv4[4], uint16_t port, int64_t expiry_unix,
                        const uint8_t secret[32], const uint8_t public_key[32]) {
    if (!out || !ipv4 || !secret || !public_key || port == 0 || expiry_unix <= 0 || !nonzero(secret, 32) ||
        !nonzero(public_key, 32))
        return 0;
    memcpy(out, "FRP2", 4);
    out[4] = FR_WIRE_VERSION;
    memcpy(out + 5, ipv4, 4);
    put_u16(out + 9, port);
    put_u64(out + 11, (uint64_t)expiry_unix);
    memcpy(out + 19, secret, 32);
    memcpy(out + 51, public_key, 32);
    return 1;
}

int fr_qr_payload_parse(const uint8_t* in, size_t n, fr_qr_info* out) {
    if (!in || !out || n != FR_QR_PAYLOAD || memcmp(in, "FRP2", 4) != 0 || in[4] != FR_WIRE_VERSION)
        return 0;
    memset(out, 0, sizeof *out);
    memcpy(out->ipv4, in + 5, 4);
    out->port = get_u16(in + 9);
    out->expiry_unix = (int64_t)get_u64(in + 11);
    memcpy(out->secret, in + 19, 32);
    memcpy(out->public_key, in + 51, 32);
    return out->port != 0 && out->expiry_unix > 0 && nonzero(out->secret, 32) && nonzero(out->public_key, 32);
}

int fr_qr_text_encode(char* out, size_t cap, const uint8_t payload[FR_QR_PAYLOAD]) {
    static const char prefix[] = "furina:";
    if (!out || cap < sizeof prefix || !payload)
        return 0;
    memcpy(out, prefix, sizeof prefix - 1);
    return b64_encode(out + sizeof prefix - 1, cap - (sizeof prefix - 1), payload, FR_QR_PAYLOAD);
}

int fr_qr_text_decode(const char* text, uint8_t payload[FR_QR_PAYLOAD]) {
    uint8_t raw[96];
    size_t n = 0;
    if (!text || strncmp(text, "furina:", 7) != 0)
        return 0;
    if (!b64_decode(raw, sizeof raw, &n, text + 7) || n != FR_QR_PAYLOAD)
        return 0;
    memcpy(payload, raw, FR_QR_PAYLOAD);
    crypto_wipe(raw, sizeof raw);
    return 1;
}

int fr_credential_from_text(const char* text, uint8_t psk[32], fr_qr_info* info, int* error) {
    char code[FR_PAIRING_RAW + 1];
    uint8_t payload[FR_QR_PAYLOAD];
    fr_qr_info local;
    if (fr_pairing_code_normalize(text, code)) {
        fr_pairing_psk_from_code(psk, code);
        crypto_wipe(code, sizeof code);
        if (info)
            memset(info, 0, sizeof *info);
        if (error)
            *error = FR_SEC_OK;
        return 1;
    }
    if (fr_qr_text_decode(text, payload) && fr_qr_payload_parse(payload, FR_QR_PAYLOAD, &local)) {
        memcpy(psk, local.secret, 32);
        if (info)
            *info = local;
        crypto_wipe(payload, sizeof payload);
        crypto_wipe(&local, sizeof local);
        if (error)
            *error = FR_SEC_OK;
        return 2;
    }
    if (error)
        *error = FR_SEC_FORMAT;
    return 0;
}

int fr_attempt_begin(fr_attempt_gate* gate, int64_t now_ms) {
    if (!gate)
        return 0;
    if (gate->lock_until_ms && now_ms < gate->lock_until_ms)
        return 0;
    if (gate->lock_until_ms && now_ms >= gate->lock_until_ms) {
        gate->lock_until_ms = 0;
        gate->failures = 0;
    }
    if (!gate->window_start_ms || now_ms - gate->window_start_ms >= 10000) {
        gate->window_start_ms = now_ms;
        gate->window_count = 0;
    }
    if (gate->window_count >= 8)
        return 0;
    if (gate->failures > 0 && gate->last_fail_ms && now_ms - gate->last_fail_ms < 500)
        return 0;
    gate->window_count++;
    return 1;
}

void fr_attempt_fail(fr_attempt_gate* gate, int64_t now_ms) {
    if (!gate)
        return;
    gate->failures++;
    gate->last_fail_ms = now_ms;
    if (gate->failures >= 5) {
        gate->lock_until_ms = now_ms + (int64_t)5 * 60 * 1000;
        gate->failures = 0;
    }
}

void fr_attempt_success(fr_attempt_gate* gate) {
    if (!gate)
        return;
    gate->failures = 0;
    gate->last_fail_ms = 0;
    gate->lock_until_ms = 0;
}

static void derive_label(uint8_t* out, size_t out_n, const uint8_t prk[64], const uint8_t* transcript, size_t tlen,
                         const char* label) {
    uint8_t msg[180];
    size_t llen = strlen(label);
    memcpy(msg, transcript, tlen);
    memcpy(msg + tlen, label, llen);
    crypto_blake2b_keyed(out, out_n, prk, 64, msg, tlen + llen);
    crypto_wipe(msg, sizeof msg);
}

static void nonce_label(uint8_t nonce[24], const uint8_t key[32], const uint8_t* transcript, size_t tlen,
                        const char* label) {
    uint8_t msg[180];
    size_t llen = strlen(label);
    memcpy(msg, transcript, tlen);
    memcpy(msg + tlen, label, llen);
    crypto_blake2b_keyed(nonce, 24, key, 32, msg, tlen + llen);
    crypto_wipe(msg, sizeof msg);
}

static int shared_keys(fr_handshake* hs, const uint8_t ee[32], const uint8_t es[32], const uint8_t se[32]) {
    uint8_t ikm[128], prk[64];
    if (!nonzero(ee, 32) || !nonzero(es, 32) || !nonzero(se, 32))
        return fail(hs, FR_SEC_AUTH);
    memcpy(ikm, ee, 32);
    memcpy(ikm + 32, es, 32);
    memcpy(ikm + 64, se, 32);
    memcpy(ikm + 96, hs->psk, 32);
    crypto_blake2b(prk, 64, ikm, sizeof ikm);
    crypto_wipe(ikm, sizeof ikm);
    derive_label(hs->confirm_key, 32, prk, hs->transcript, hs->transcript_len, "confirm");
    derive_label(hs->send_key, 32, prk, hs->transcript, hs->transcript_len, hs->is_server ? "s2c" : "c2s");
    derive_label(hs->recv_key, 32, prk, hs->transcript, hs->transcript_len, hs->is_server ? "c2s" : "s2c");
    derive_label(hs->cursor_key, 32, prk, hs->transcript, hs->transcript_len, "cursor");
    derive_label(hs->send_salt, 16, prk, hs->transcript, hs->transcript_len, hs->is_server ? "ssalt" : "csalt");
    derive_label(hs->recv_salt, 16, prk, hs->transcript, hs->transcript_len, hs->is_server ? "csalt" : "ssalt");
    crypto_wipe(prk, sizeof prk);
    return 1;
}

static int dh_triplet(const uint8_t our_eph[32], const uint8_t their_eph[32], const uint8_t our_static[32],
                      const uint8_t their_static[32], int server, uint8_t ee[32], uint8_t es[32], uint8_t se[32]) {
    crypto_x25519(ee, our_eph, their_eph);
    if (server) {
        crypto_x25519(es, our_static, their_eph);
        crypto_x25519(se, our_eph, their_static);
    } else {
        crypto_x25519(es, our_eph, their_static);
        crypto_x25519(se, our_static, their_eph);
    }
    return nonzero(ee, 32) && nonzero(es, 32) && nonzero(se, 32);
}

int fr_hs_client_start(fr_handshake* hs, const fr_identity* self, const uint8_t eph[32], const uint8_t psk[32],
                       int mode, const uint8_t* pin, uint8_t hello[FR_HS_CLIENT_HELLO]) {
    memset(hs, 0, sizeof *hs);
    if (!self || !eph || !hello || (mode != 1 && mode != 2))
        return fail(hs, FR_SEC_FORMAT);
    hs->mode = mode;
    hs->local = *self;
    memcpy(hs->eph_secret, eph, 32);
    crypto_x25519_public_key(hs->eph_public, hs->eph_secret);
    if (mode == 1) {
        if (!psk || !nonzero(psk, 32))
            return fail(hs, FR_SEC_AUTH);
        memcpy(hs->psk, psk, 32);
    }
    if (pin && nonzero(pin, 32)) {
        hs->have_pin = 1;
        memcpy(hs->pinned, pin, 32);
    }
    if (mode == 2 && !hs->have_pin)
        return fail(hs, FR_SEC_PIN);
    memcpy(hello, "FUR2", 4);
    hello[4] = FR_WIRE_VERSION;
    hello[5] = (uint8_t)mode;
    memcpy(hello + 6, hs->eph_public, 32);
    memcpy(hello + 38, hs->local.public_key, 32);
    memcpy(hs->transcript, hello, FR_HS_CLIENT_HELLO);
    hs->transcript_len = FR_HS_CLIENT_HELLO;
    return 1;
}

int fr_hs_server_start(fr_handshake* hs, const fr_identity* self, const uint8_t eph[32], const uint8_t psk[32],
                       const uint8_t* pin, const uint8_t hello[FR_HS_CLIENT_HELLO],
                       uint8_t reply[FR_HS_SERVER_HELLO]) {
    uint8_t ee[32], es[32], se[32], plain[32], nonce[24];
    const uint8_t *client_eph, *client_static;
    memset(hs, 0, sizeof *hs);
    hs->is_server = 1;
    if (!hello)
        return fail(hs, FR_SEC_FORMAT);
    if (memcmp(hello, "FUR1", 4) == 0)
        return fail(hs, FR_SEC_LEGACY);
    if (memcmp(hello, "FUR2", 4) != 0 || hello[4] != FR_WIRE_VERSION)
        return fail(hs, FR_SEC_VERSION);
    if (hello[5] != 1 && hello[5] != 2)
        return fail(hs, FR_SEC_VERSION);
    hs->mode = hello[5];
    client_eph = hello + 6;
    client_static = hello + 38;
    if (!nonzero(client_eph, 32) || !nonzero(client_static, 32) || !self || !eph)
        return fail(hs, FR_SEC_FORMAT);
    if (pin && nonzero(pin, 32)) {
        hs->have_pin = 1;
        memcpy(hs->pinned, pin, 32);
        if (crypto_verify32(client_static, pin) != 0)
            return fail(hs, FR_SEC_PIN);
    } else if (hs->mode == 2)
        return fail(hs, FR_SEC_PIN);
    if (hs->mode == 1) {
        if (!psk || !nonzero(psk, 32))
            return fail(hs, FR_SEC_AUTH);
        memcpy(hs->psk, psk, 32);
    }
    hs->local = *self;
    memcpy(hs->peer_static, client_static, 32);
    memcpy(hs->eph_secret, eph, 32);
    crypto_x25519_public_key(hs->eph_public, hs->eph_secret);
    memcpy(hs->transcript, hello, FR_HS_CLIENT_HELLO);
    memcpy(hs->transcript + FR_HS_CLIENT_HELLO, hs->eph_public, 32);
    memcpy(hs->transcript + FR_HS_CLIENT_HELLO + 32, hs->local.public_key, 32);
    hs->transcript_len = FR_HS_CLIENT_HELLO + 64;
    if (!dh_triplet(hs->eph_secret, client_eph, hs->local.secret, client_static, 1, ee, es, se) ||
        !shared_keys(hs, ee, es, se))
        return fail(hs, FR_SEC_AUTH);
    crypto_blake2b(plain, 32, hs->transcript, hs->transcript_len);
    nonce_label(nonce, hs->confirm_key, hs->transcript, hs->transcript_len, "server-nonce");
    crypto_aead_lock(hs->server_box, hs->server_mac, hs->confirm_key, nonce, hs->transcript, hs->transcript_len,
                     plain, 32);
    memcpy(reply, hs->eph_public, 32);
    memcpy(reply + 32, hs->local.public_key, 32);
    memcpy(reply + 64, nonce, 24);
    memcpy(reply + 88, hs->server_mac, 16);
    memcpy(reply + 104, hs->server_box, 32);
    crypto_wipe(ee, sizeof ee);
    crypto_wipe(es, sizeof es);
    crypto_wipe(se, sizeof se);
    crypto_wipe(plain, sizeof plain);
    return 1;
}

int fr_hs_client_finish(fr_handshake* hs, const uint8_t reply[FR_HS_SERVER_HELLO],
                        uint8_t confirm[FR_HS_CLIENT_CONFIRM]) {
    uint8_t ee[32], es[32], se[32], expect[32], plain[32], got[32], nonce[24], mac[16];
    const uint8_t *server_eph, *server_static;
    if (!hs || hs->is_server || hs->transcript_len != FR_HS_CLIENT_HELLO || !reply || !confirm)
        return fail(hs, FR_SEC_FORMAT);
    if (memcmp(reply, "FUR1", 4) == 0)
        return fail(hs, FR_SEC_LEGACY);
    server_eph = reply;
    server_static = reply + 32;
    if (!nonzero(server_eph, 32) || !nonzero(server_static, 32))
        return fail(hs, FR_SEC_AUTH);
    if (hs->have_pin && crypto_verify32(server_static, hs->pinned) != 0)
        return fail(hs, FR_SEC_PIN);
    memcpy(hs->peer_static, server_static, 32);
    memcpy(hs->transcript + hs->transcript_len, server_eph, 32);
    memcpy(hs->transcript + hs->transcript_len + 32, server_static, 32);
    hs->transcript_len += 64;
    if (!dh_triplet(hs->eph_secret, server_eph, hs->local.secret, server_static, 0, ee, es, se) ||
        !shared_keys(hs, ee, es, se))
        return fail(hs, FR_SEC_AUTH);
    crypto_blake2b(expect, 32, hs->transcript, hs->transcript_len);
    memcpy(nonce, reply + 64, 24);
    memcpy(mac, reply + 88, 16);
    if (crypto_aead_unlock(got, mac, hs->confirm_key, nonce, hs->transcript, hs->transcript_len, reply + 104, 32) !=
            0 ||
        crypto_verify32(got, expect) != 0)
        return fail(hs, FR_SEC_AUTH);
    memcpy(hs->server_mac, mac, 16);
    memcpy(hs->server_box, reply + 104, 32);
    crypto_blake2b_keyed(plain, 32, hs->confirm_key, 32, got, 32);
    nonce_label(nonce, hs->confirm_key, hs->transcript, hs->transcript_len, "client-nonce");
    crypto_aead_lock(confirm + 40, confirm + 24, hs->confirm_key, nonce, hs->server_box, 32, plain, 32);
    memcpy(confirm, nonce, 24);
    hs->open = 1;
    crypto_wipe(hs->eph_secret, sizeof hs->eph_secret);
    crypto_wipe(hs->psk, sizeof hs->psk);
    crypto_wipe(hs->confirm_key, sizeof hs->confirm_key);
    crypto_wipe(ee, sizeof ee);
    crypto_wipe(es, sizeof es);
    crypto_wipe(se, sizeof se);
    crypto_wipe(expect, sizeof expect);
    crypto_wipe(got, sizeof got);
    crypto_wipe(plain, sizeof plain);
    return 1;
}

int fr_hs_server_finish(fr_handshake* hs, const uint8_t confirm[FR_HS_CLIENT_CONFIRM]) {
    uint8_t expect[32], got[32], transcript_hash[32];
    if (!hs || !hs->is_server || hs->open || !confirm)
        return fail(hs, FR_SEC_FORMAT);
    crypto_blake2b(transcript_hash, 32, hs->transcript, hs->transcript_len);
    crypto_blake2b_keyed(expect, 32, hs->confirm_key, 32, transcript_hash, 32);
    if (crypto_aead_unlock(got, confirm + 24, hs->confirm_key, confirm, hs->server_box, 32, confirm + 40, 32) != 0 ||
        crypto_verify32(got, expect) != 0)
        return fail(hs, FR_SEC_AUTH);
    hs->open = 1;
    crypto_wipe(hs->eph_secret, sizeof hs->eph_secret);
    crypto_wipe(hs->psk, sizeof hs->psk);
    crypto_wipe(hs->confirm_key, sizeof hs->confirm_key);
    crypto_wipe(expect, sizeof expect);
    crypto_wipe(got, sizeof got);
    crypto_wipe(transcript_hash, sizeof transcript_hash);
    return 1;
}

void fr_hs_wipe(fr_handshake* hs) {
    if (hs)
        crypto_wipe(hs, sizeof *hs);
}

static int seal_with(const uint8_t key[32], const uint8_t nonce[24], const uint8_t* plain, size_t plain_len,
                     uint8_t* out, size_t out_cap, size_t* out_len) {
    if (!out || plain_len > 8u * 1024u * 1024u + 12u || out_cap < 4 + 16 + plain_len)
        return -1;
    put_u32(out, (uint32_t)plain_len);
    crypto_aead_lock(out + 20, out + 4, key, nonce, NULL, 0, plain, plain_len);
    *out_len = 4 + 16 + plain_len;
    return 1;
}

static int open_with(const uint8_t key[32], const uint8_t nonce[24], const uint8_t* input, size_t input_len,
                     size_t* consumed, uint8_t* plain, size_t plain_cap, size_t* plain_len) {
    uint32_t len;
    if (input_len < 4)
        return 0;
    len = get_u32(input);
    if (len > 8u * 1024u * 1024u + 12u)
        return -1;
    if (input_len < 4u + 16u + len)
        return 0;
    if (plain_cap < len)
        return -1;
    if (crypto_aead_unlock(plain, input + 4, key, nonce, NULL, 0, input + 20, len) != 0)
        return -1;
    *consumed = 4u + 16u + len;
    *plain_len = len;
    return 1;
}

static void nonce_from(uint8_t nonce[24], const uint8_t salt[16], uint64_t counter, uint8_t direction) {
    memset(nonce, 0, 24);
    if (salt)
        memcpy(nonce, salt, 16);
    nonce[0] ^= direction;
    put_u64(nonce + 16, counter);
}

int fr_record_seal(const uint8_t key[32], const uint8_t salt[16], uint64_t* counter, const uint8_t* plain,
                   size_t plain_len, uint8_t* out, size_t out_cap, size_t* out_len) {
    uint8_t nonce[24];
    uint64_t next;
    int rc;
    if (!key || !salt || !counter || !plain || !out || !out_len || *counter == UINT64_MAX)
        return -1;
    next = *counter + 1;
    nonce_from(nonce, salt, next, 0);
    rc = seal_with(key, nonce, plain, plain_len, out, out_cap, out_len);
    if (rc == 1)
        *counter = next;
    return rc;
}

int fr_record_open(const uint8_t key[32], const uint8_t salt[16], uint64_t* counter, const uint8_t* input,
                   size_t input_len, size_t* consumed, uint8_t* plain, size_t plain_cap, size_t* plain_len) {
    uint8_t nonce[24];
    int rc;
    if (!key || !salt || !counter || !input || !consumed || !plain || !plain_len || *counter == UINT64_MAX)
        return -1;
    if (input_len < 4)
        return 0;
    nonce_from(nonce, salt, *counter + 1, 0);
    rc = open_with(key, nonce, input, input_len, consumed, plain, plain_cap, plain_len);
    if (rc == 1)
        *counter = *counter + 1;
    return rc;
}

int fr_cursor_seal(const uint8_t key[32], uint8_t direction, uint64_t* counter, const uint8_t* plain, size_t plain_len,
                   uint8_t* out, size_t out_cap, size_t* out_len) {
    uint8_t nonce[24];
    uint64_t next;
    int rc;
    if (!key || !counter || !direction || *counter == UINT64_MAX)
        return -1;
    next = *counter + 1;
    nonce_from(nonce, NULL, next, direction);
    rc = seal_with(key, nonce, plain, plain_len, out, out_cap, out_len);
    if (rc == 1)
        *counter = next;
    return rc;
}

int fr_cursor_open(const uint8_t key[32], uint8_t direction, uint64_t* counter, const uint8_t* input, size_t input_len,
                   size_t* consumed, uint8_t* plain, size_t plain_cap, size_t* plain_len) {
    uint8_t nonce[24];
    int rc;
    if (!key || !counter || !direction || *counter == UINT64_MAX)
        return -1;
    if (input_len < 4)
        return 0;
    nonce_from(nonce, NULL, *counter + 1, direction);
    rc = open_with(key, nonce, input, input_len, consumed, plain, plain_cap, plain_len);
    if (rc == 1)
        *counter = *counter + 1;
    return rc;
}

int fr_qr_decode_luma(const uint8_t* luma, int width, int height, char* text, size_t text_cap) {
    struct quirc* qr;
    int i, count, dw = 0, dh = 0;
    if (!luma || !text || text_cap < 2 || width < 21 || height < 21)
        return 0;
    qr = quirc_new();
    if (!qr || quirc_resize(qr, width, height) < 0) {
        quirc_destroy(qr);
        return 0;
    }
    memcpy(quirc_begin(qr, &dw, &dh), luma, (size_t)width * (size_t)height);
    quirc_end(qr);
    count = quirc_count(qr);
    for (i = 0; i < count; i++) {
        struct quirc_code code;
        struct quirc_data data;
        quirc_extract(qr, i, &code);
        if (quirc_decode(&code, &data) != QUIRC_SUCCESS || data.payload_len <= 0)
            continue;
        if ((size_t)data.payload_len >= text_cap)
            continue;
        memcpy(text, data.payload, (size_t)data.payload_len);
        text[data.payload_len] = 0;
        quirc_destroy(qr);
        return 1;
    }
    quirc_destroy(qr);
    return 0;
}
