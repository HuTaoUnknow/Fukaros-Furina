#include "furina_security.h"
#include "qrcodegen.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails = 0;

#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        if (!(x)) {                                                                                          \
            fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x);                                             \
            fails++;                                                                                         \
        }                                                                                                    \
    } while (0)

static void fill(uint8_t* p, int n, uint8_t seed) {
    int i;
    for (i = 0; i < n; i++)
        p[i] = (uint8_t)(seed + i * 17);
}

static void pair(const uint8_t psk[32], const uint8_t* client_pin, const uint8_t* server_pin, int expect_ok) {
    fr_identity client, server;
    fr_handshake hs_c, hs_s;
    uint8_t client_secret[32], server_secret[32], ce[32], se[32];
    uint8_t hello[FR_HS_CLIENT_HELLO], reply[FR_HS_SERVER_HELLO], confirm[FR_HS_CLIENT_CONFIRM];
    uint8_t plain[16], opened[16], record[64], replay[64];
    size_t record_n = 0, consumed = 0, opened_n = 0;
    uint64_t send_n = 0, recv_n = 0;
    fill(client_secret, 32, 3);
    fill(server_secret, 32, 9);
    fill(ce, 32, 11);
    fill(se, 32, 21);
    fr_identity_from_secret(&client, client_secret);
    fr_identity_from_secret(&server, server_secret);
    CHECK(fr_hs_client_start(&hs_c, &client, ce, psk, 1, client_pin, hello));
    if (!fr_hs_server_start(&hs_s, &server, se, psk, server_pin, hello, reply)) {
        CHECK(!expect_ok);
        CHECK(hs_s.error == FR_SEC_PIN || hs_s.error == FR_SEC_AUTH || hs_s.error == FR_SEC_LEGACY);
        return;
    }
    if (!fr_hs_client_finish(&hs_c, reply, confirm)) {
        CHECK(!expect_ok);
        CHECK(hs_c.error == FR_SEC_PIN || hs_c.error == FR_SEC_AUTH);
        return;
    }
    CHECK(fr_hs_server_finish(&hs_s, confirm) == expect_ok);
    if (!expect_ok)
        return;
    CHECK(hs_c.open && hs_s.open);
    CHECK(memcmp(hs_c.peer_static, server.public_key, 32) == 0);
    CHECK(memcmp(hs_s.peer_static, client.public_key, 32) == 0);
    memcpy(plain, "frame-ok-message", 16);
    CHECK(fr_record_seal(hs_c.send_key, hs_c.send_salt, &send_n, plain, 16, record, sizeof record, &record_n) == 1);
    CHECK(fr_record_open(hs_s.recv_key, hs_s.recv_salt, &recv_n, record, record_n, &consumed, opened, sizeof opened,
                         &opened_n) == 1);
    CHECK(opened_n == 16 && memcmp(opened, plain, 16) == 0);
    memcpy(replay, record, record_n);
    CHECK(fr_record_open(hs_s.recv_key, hs_s.recv_salt, &recv_n, replay, record_n, &consumed, opened, sizeof opened,
                         &opened_n) == -1);
    fr_hs_wipe(&hs_c);
    fr_hs_wipe(&hs_s);
}

int main(void) {
    char code[FR_PAIRING_TEXT], normalized[FR_PAIRING_RAW + 1];
    uint8_t random[16], psk[32], other[32], secret[32], pub[32], payload[FR_QR_PAYLOAD], ipv4[4] = {192, 168, 1, 20};
    uint8_t legacy[FR_HS_CLIENT_HELLO];
    fr_identity id, server;
    fr_handshake hs;
    fr_qr_info info;
    fr_attempt_gate gate;
    uint8_t hello[FR_HS_CLIENT_HELLO], reply[FR_HS_SERVER_HELLO];
    uint8_t server_secret[32], eph[32];
    char text[256], decoded[256], fingerprint[20];
    int error = 0, i;
    uint8_t temp[qrcodegen_BUFFER_LEN_FOR_VERSION(12)];
    uint8_t qr[qrcodegen_BUFFER_LEN_FOR_VERSION(12)];
    fill(random, 16, 4);
    fr_pairing_code_generate(code, random);
    CHECK(fr_pairing_code_valid(code));
    CHECK(fr_pairing_code_normalize(code, normalized));
    CHECK(!fr_pairing_code_valid("123456789"));
    CHECK(!fr_pairing_code_valid("ABCDEF"));
    CHECK(!fr_pairing_code_valid("IIIIIIIIIIIIIIIIIIII"));
    CHECK(!fr_pairing_code_valid("00000000000000000000"));
    fr_pairing_psk_from_code(psk, normalized);
    fr_pairing_psk_from_code(other, "ZZZZZZZZZZZZZZZZZZZ2");
    CHECK(memcmp(psk, other, 32) != 0);
    pair(psk, NULL, NULL, 1);
    pair(psk, NULL, psk, 0);

    fill(secret, 32, 40);
    fill(pub, 32, 70);
    CHECK(fr_qr_payload_build(payload, ipv4, 27183, 1893456000, secret, pub));
    CHECK(fr_qr_payload_parse(payload, FR_QR_PAYLOAD, &info));
    CHECK(info.port == 27183 && info.expiry_unix == 1893456000);
    CHECK(memcmp(info.secret, secret, 32) == 0);
    CHECK(fr_qr_text_encode(text, sizeof text, payload));
    CHECK(fr_credential_from_text(text, other, &info, &error) == 2);
    CHECK(memcmp(other, secret, 32) == 0 && memcmp(info.public_key, pub, 32) == 0);
    CHECK(fr_credential_from_text(code, other, NULL, &error) == 1);
    CHECK(memcmp(other, psk, 32) == 0);
    payload[0] = 'X';
    CHECK(!fr_qr_payload_parse(payload, FR_QR_PAYLOAD, &info));

    memset(&gate, 0, sizeof gate);
    for (i = 0; i < 8; i++)
        CHECK(fr_attempt_begin(&gate, 1000 + i));
    CHECK(!fr_attempt_begin(&gate, 2000));
    CHECK(fr_attempt_begin(&gate, 12000));
    for (i = 0; i < 5; i++)
        fr_attempt_fail(&gate, 13000 + i * 500);
    CHECK(!fr_attempt_begin(&gate, 16000));
    CHECK(fr_attempt_begin(&gate, 16000 + 5 * 60 * 1000));

    fill(server_secret, 32, 9);
    fr_identity_from_secret(&server, server_secret);
    memset(legacy, 0, sizeof legacy);
    memcpy(legacy, "FUR1", 4);
    fill(eph, 32, 15);
    CHECK(!fr_hs_server_start(&hs, &server, eph, psk, NULL, legacy, reply));
    CHECK(hs.error == FR_SEC_LEGACY);
    CHECK(strstr(fr_security_error_text(FR_SEC_LEGACY), "0.4.21") != NULL);

    fill(secret, 32, 80);
    fr_identity_from_secret(&id, secret);
    fr_fingerprint(id.public_key, fingerprint);
    CHECK(strlen(fingerprint) == 19);

    CHECK(qrcodegen_encodeText(text, temp, qr, qrcodegen_Ecc_MEDIUM, 1, 12, qrcodegen_Mask_AUTO, 1));
    {
        int size = qrcodegen_getSize(qr), scale = 4, quiet = 4;
        int dim = (size + quiet * 2) * scale, y, x;
        uint8_t* image = malloc((size_t)dim * (size_t)dim);
        CHECK(image != NULL);
        memset(image, 255, (size_t)dim * (size_t)dim);
        for (y = 0; y < size; y++)
            for (x = 0; x < size; x++)
                if (qrcodegen_getModule(qr, x, y)) {
                    int yy, xx;
                    for (yy = 0; yy < scale; yy++)
                        for (xx = 0; xx < scale; xx++)
                            image[(quiet * scale + y * scale + yy) * dim + quiet * scale + x * scale + xx] = 0;
                }
        CHECK(fr_qr_decode_luma(image, dim, dim, decoded, sizeof decoded));
        CHECK(strcmp(decoded, text) == 0);
        free(image);
    }

    {
        fr_identity client, server_id;
        fr_handshake a, b;
        uint8_t cs[32], ss[32], ce[32], se[32], zero[32] = {0}, confirm[FR_HS_CLIENT_CONFIRM];
        fill(cs, 32, 5);
        fill(ss, 32, 8);
        fill(ce, 32, 13);
        fill(se, 32, 19);
        fr_identity_from_secret(&client, cs);
        fr_identity_from_secret(&server_id, ss);
        CHECK(fr_hs_client_start(&a, &client, ce, zero, 2, server_id.public_key, hello));
        CHECK(fr_hs_server_start(&b, &server_id, se, zero, client.public_key, hello, reply));
        CHECK(fr_hs_client_finish(&a, reply, confirm));
        CHECK(fr_hs_server_finish(&b, confirm));
        CHECK(a.open && b.open);
        fill(cs, 32, 99);
        fr_identity_from_secret(&client, cs);
        CHECK(fr_hs_client_start(&a, &client, ce, zero, 2, server_id.public_key, hello));
        CHECK(!fr_hs_server_start(&b, &server_id, se, zero, server_id.public_key, hello, reply));
        CHECK(b.error == FR_SEC_PIN);
    }

    if (fails) {
        fprintf(stderr, "%d pairing checks failed\n", fails);
        return 1;
    }
    puts("Pairing, handshake, records and QR passed");
    return 0;
}
