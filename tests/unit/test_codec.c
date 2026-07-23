#include "rtw_test.h"
#include "../../src/core/rtw_mulaw.h"
#include "../../src/core/rtw_base64.h"

#include <string.h>

static void test_mulaw_roundtrip(void)
{
    int16_t samples[] = {0, 1, -1, 128, -128, 1000, -1000, 16000, -16000, 32500, -32500};
    size_t i;
    /* Known: silence encodes to 0xFF on standard µ-law */
    RTW_CHECK(rtw_linear_to_mulaw(0) == 0xFF);
    for (i = 0; i < sizeof(samples) / sizeof(samples[0]); i++) {
        uint8_t m = rtw_linear_to_mulaw(samples[i]);
        int16_t back = rtw_mulaw_to_linear(m);
        int abs_in = samples[i] < 0 ? -samples[i] : samples[i];
        int diff = samples[i] - back;
        if (diff < 0) diff = -diff;
        /* Step size grows with magnitude; allow ~6% or 256 floor. */
        int tol = abs_in / 16;
        if (tol < 256) tol = 256;
        RTW_CHECK(diff <= tol);
    }
}

static void test_base64(void)
{
    const uint8_t in[] = {0x00, 0x01, 0xff, 0x7e, 0x20};
    char enc[64];
    uint8_t dec[64];
    size_t n;
    RTW_CHECK(rtw_base64_encode(in, sizeof(in), enc, sizeof(enc)) > 0);
    n = rtw_base64_decode(enc, strlen(enc), dec, sizeof(dec));
    RTW_CHECK(n == sizeof(in));
    RTW_CHECK(memcmp(in, dec, sizeof(in)) == 0);
}

static void run(void)
{
    test_mulaw_roundtrip();
    test_base64();
}

RTW_TEST_MAIN(run)
