#include "helper_launch.h"
#include <stdio.h>
#include <string.h>

#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        if (!(x)) {                                                                                          \
            fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x);                                             \
            return 1;                                                                                        \
        }                                                                                                    \
    } while (0)

static int hash_is(const char* data, const char* expect) {
    char out[65];
    fr_helper_sha256(data, strlen(data), out);
    return strcmp(out, expect) == 0;
}

static int hash_bytes(void) {
    char out[65];
    const char* text = "abc";
    fr_helper_sha256_ctx ctx;
    CHECK(hash_is("", "E3B0C44298FC1C149AFBF4C8996FB92427AE41E4649B934CA495991B7852B855"));
    CHECK(hash_is("abc", "BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD"));
    CHECK(hash_is("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
                  "248D6A61D20638B8E5C026930C3E6039A33CE45964FF2167F6ECEDD419DB06C1"));
    fr_helper_sha256_init(&ctx);
    fr_helper_sha256_update(&ctx, text, 1);
    fr_helper_sha256_update(&ctx, text + 1, 2);
    fr_helper_sha256_final(&ctx, out);
    CHECK(strcmp(out, "BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD") == 0);
    return 0;
}

static int launch_policy(void) {
    /* Tampered source, nothing trustworthy staged. */
    CHECK(fr_helper_launch_decide(0, 0, 0, 0, 0) == FR_HELPER_LAUNCH_REJECT_HASH);
    CHECK(fr_helper_launch_decide(1, 0, 0, 0, 1) == FR_HELPER_LAUNCH_REJECT_HASH);
    CHECK(fr_helper_launch_decide(1, 0, 1, 0, 0) == FR_HELPER_LAUNCH_REJECT_HASH);
    /* A protected staged copy that matches the pin is used even if the source was swapped. */
    CHECK(fr_helper_launch_decide(1, 0, 1, 1, 0) == FR_HELPER_LAUNCH_STAGED);
    /* A staged copy in a user-writable directory is not elevated. */
    CHECK(fr_helper_launch_decide(1, 0, 1, 1, 1) == FR_HELPER_LAUNCH_REJECT_HASH);
    CHECK(fr_helper_launch_decide(0, 1, 0, 0, 0) == FR_HELPER_LAUNCH_IN_PLACE);
    CHECK(fr_helper_launch_decide(0, 1, 1, 0, 0) == FR_HELPER_LAUNCH_IN_PLACE);
    CHECK(fr_helper_launch_decide(0, 1, 1, 1, 1) == FR_HELPER_LAUNCH_IN_PLACE);
    /* Prefer the protected staged image when both copies match. */
    CHECK(fr_helper_launch_decide(0, 1, 1, 1, 0) == FR_HELPER_LAUNCH_STAGED);
    CHECK(fr_helper_launch_decide(1, 1, 1, 1, 0) == FR_HELPER_LAUNCH_STAGED);
    /* Writable source must be copied before elevation. */
    CHECK(fr_helper_launch_decide(1, 1, 0, 0, 0) == FR_HELPER_LAUNCH_NEED_STAGE);
    CHECK(fr_helper_launch_decide(1, 1, 1, 0, 0) == FR_HELPER_LAUNCH_NEED_STAGE);
    CHECK(fr_helper_launch_decide(1, 1, 1, 1, 1) == FR_HELPER_LAUNCH_NEED_STAGE);
    return 0;
}

static int residue_policy(void) {
    CHECK(!fr_helper_residue_stale(0, 0, 0));
    CHECK(!fr_helper_residue_stale(0, 1, 1));
    CHECK(fr_helper_residue_stale(1, 0, 0));
    CHECK(fr_helper_residue_stale(1, 0, 1));
    CHECK(fr_helper_residue_stale(1, 1, 0));
    CHECK(!fr_helper_residue_stale(1, 1, 1));
    return 0;
}

static int settings_policy(void) {
    const char templ[] = "<vdd_settings><monitors/></vdd_settings>";
    const char custom[] = "<vdd_settings>custom</vdd_settings>";
    const char other[] = "custom-settings";
    const char longer[] = "not-our-prefix-but-longer-than-template!!";
    const size_t n = sizeof templ - 1;
    CHECK(fr_helper_settings_incomplete(NULL, 0, templ, n));
    CHECK(fr_helper_settings_incomplete("", 0, templ, n));
    CHECK(fr_helper_settings_incomplete(templ, 12, templ, n));
    CHECK(!fr_helper_settings_incomplete(templ, n, templ, n));
    CHECK(!fr_helper_settings_incomplete(custom, strlen(custom), templ, n));
    CHECK(!fr_helper_settings_incomplete(other, strlen(other), templ, n));
    CHECK(strlen(longer) >= n);
    CHECK(!fr_helper_settings_incomplete(longer, strlen(longer), templ, n));
    return 0;
}

int main(void) {
    CHECK(hash_bytes() == 0);
    CHECK(launch_policy() == 0);
    CHECK(residue_policy() == 0);
    CHECK(settings_policy() == 0);
    return 0;
}
