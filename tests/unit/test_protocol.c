#include "rtw_test.h"
#include "../../src/core/rtw_protocol.h"
#include "../../src/core/rtw_mulaw.h"

#include <stdlib.h>
#include <string.h>

static void test_build_parse_media(void)
{
    int16_t pcm[160];
    uint8_t mulaw[160];
    uint8_t decoded[512];
    rtw_inbound_event_t ev;
    char *json;
    size_t i;
    for (i = 0; i < 160; i++) {
        pcm[i] = (int16_t)(i * 20);
    }
    rtw_pcm16_to_mulaw(pcm, 160, mulaw);
    json = rtw_build_media("MZabc", "inbound", 1, 20, 2, mulaw, 160);
    RTW_CHECK(json != NULL);
    RTW_CHECK(strstr(json, "\"event\":\"media\"") != NULL);
    RTW_CHECK(strstr(json, "\"streamSid\":\"MZabc\"") != NULL);
    RTW_CHECK(rtw_parse_inbound(json, &ev, decoded, sizeof(decoded)) == 0);
    RTW_CHECK(ev.type == RTW_EVT_MEDIA);
    RTW_CHECK(strcmp(ev.stream_sid, "MZabc") == 0);
    RTW_CHECK(ev.payload_len == 160);
    RTW_CHECK(memcmp(ev.payload, mulaw, 160) == 0);
    free(json);
}

static void test_clear_and_mark_parse(void)
{
    rtw_inbound_event_t ev;
    uint8_t buf[8];
    RTW_CHECK(rtw_parse_inbound("{\"event\":\"clear\",\"streamSid\":\"MZ1\"}", &ev, buf, sizeof(buf)) == 0);
    RTW_CHECK(ev.type == RTW_EVT_CLEAR);
    RTW_CHECK(rtw_parse_inbound("{\"event\":\"mark\",\"streamSid\":\"MZ1\",\"mark\":{\"name\":\"x\"}}",
                                &ev, buf, sizeof(buf)) == 0);
    RTW_CHECK(ev.type == RTW_EVT_MARK);
    RTW_CHECK(strcmp(ev.mark_name, "x") == 0);
}

static void test_connected_start_stop(void)
{
    char *c = rtw_build_connected("1.0.0");
    char *s = rtw_build_start("MZsid", "ACacc", "CAcall", "{\"foo\":\"bar\"}", 1);
    char *t = rtw_build_stop("MZsid", "ACacc", "CAcall", 9);
    RTW_CHECK(c && strstr(c, "connected"));
    RTW_CHECK(s && strstr(s, "audio/x-mulaw") && strstr(s, "\"foo\":\"bar\""));
    RTW_CHECK(t && strstr(t, "stop"));
    free(c);
    free(s);
    free(t);
}

static void run(void)
{
    test_build_parse_media();
    test_clear_and_mark_parse();
    test_connected_start_stop();
}

RTW_TEST_MAIN(run)
