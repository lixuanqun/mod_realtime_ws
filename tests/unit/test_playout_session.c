#include "rtw_test.h"
#include "../../src/core/rtw_playout.h"
#include "../../src/core/rtw_queue.h"
#include "../../src/core/rtw_session.h"
#include "../../src/core/rtw_mulaw.h"
#include "../../src/core/rtw_base64.h"

#include <stdlib.h>
#include <string.h>

static void test_playout_mark_clear(void)
{
    rtw_playout_t p;
    uint8_t data[100];
    uint8_t out[100];
    char names[8][RTW_MARK_NAME_LEN];
    size_t n;
    memset(data, 0x55, sizeof(data));
    RTW_CHECK(rtw_playout_init(&p, 256) == 0);
    RTW_CHECK(rtw_playout_write(&p, data, 100) == 0);
    RTW_CHECK(rtw_playout_add_mark(&p, "m1") == 0);
    RTW_CHECK(rtw_playout_read(&p, out, 100) == 100);
    n = rtw_playout_pop_completed_marks(&p, names, 8);
    RTW_CHECK(n == 1);
    RTW_CHECK(strcmp(names[0], "m1") == 0);

    RTW_CHECK(rtw_playout_write(&p, data, 50) == 0);
    RTW_CHECK(rtw_playout_add_mark(&p, "m2") == 0);
    n = rtw_playout_clear(&p, names, 8);
    RTW_CHECK(n == 1);
    RTW_CHECK(strcmp(names[0], "m2") == 0);
    RTW_CHECK(rtw_playout_size(&p) == 0);
    rtw_playout_destroy(&p);
}

static void test_queue_drop_oldest(void)
{
    rtw_bounded_queue_t q;
    rtw_queue_item_t it;
    RTW_CHECK(rtw_queue_init(&q, 2) == 0);
    RTW_CHECK(rtw_queue_push(&q, "a", 1) == 0);
    RTW_CHECK(rtw_queue_push(&q, "b", 1) == 0);
    RTW_CHECK(rtw_queue_push(&q, "c", 1) == 0); /* drops a */
    RTW_CHECK(q.drops == 1);
    RTW_CHECK(rtw_queue_pop(&q, &it) == 0);
    RTW_CHECK(strcmp(it.data, "b") == 0);
    free(it.data);
    RTW_CHECK(rtw_queue_pop(&q, &it) == 0);
    RTW_CHECK(strcmp(it.data, "c") == 0);
    free(it.data);
    rtw_queue_destroy(&q);
}

static void test_queue_peek_drop(void)
{
    rtw_bounded_queue_t q;
    const char *p;
    size_t len;
    RTW_CHECK(rtw_queue_init(&q, 4) == 0);
    RTW_CHECK(rtw_queue_push(&q, "hello", 5) == 0);
    RTW_CHECK(rtw_queue_peek(&q, &p, &len) == 0);
    RTW_CHECK(len == 5);
    RTW_CHECK(strcmp(p, "hello") == 0);
    RTW_CHECK(rtw_queue_size(&q) == 1);
    RTW_CHECK(rtw_queue_drop_head(&q) == 0);
    RTW_CHECK(rtw_queue_size(&q) == 0);
    RTW_CHECK(rtw_queue_peek(&q, &p, &len) != 0);
    rtw_queue_destroy(&q);
}

static void test_session_uplink_and_clear(void)
{
    rtw_session_t s;
    int16_t pcm[320];
    char *json;
    rtw_inbound_event_t ev;
    uint8_t buf[1024];
    char media_json[2048];
    uint8_t mulaw[160];
    char b64[512];
    size_t i;
    int leftover;
    for (i = 0; i < 320; i++) {
        pcm[i] = (int16_t)(i * 10);
    }
    RTW_CHECK(rtw_session_init(&s, 32, 8000) == 0);
    RTW_CHECK(rtw_session_start(&s, "MZtest", "CAcall", "ACacc", "{\"app\":\"x\"}") == 0);
    leftover = rtw_session_push_pcm16(&s, pcm, 320);
    RTW_CHECK(leftover == 0);
    RTW_CHECK(s.uplink_frames == 2);
    /* connected */
    RTW_CHECK(rtw_session_pop_outbound(&s, &json) == 0);
    RTW_CHECK(strstr(json, "connected"));
    free(json);
    /* start */
    RTW_CHECK(rtw_session_pop_outbound(&s, &json) == 0);
    RTW_CHECK(strstr(json, "start"));
    RTW_CHECK(strstr(json, "app"));
    free(json);
    /* media x2 */
    RTW_CHECK(rtw_session_pop_outbound(&s, &json) == 0);
    RTW_CHECK(strstr(json, "media"));
    free(json);
    RTW_CHECK(rtw_session_pop_outbound(&s, &json) == 0);
    RTW_CHECK(strstr(json, "media"));
    free(json);

    /* peer sends media + mark + clear */
    memset(mulaw, 0x7f, sizeof(mulaw));
    RTW_CHECK(rtw_base64_encode(mulaw, 160, b64, sizeof(b64)) > 0);
    snprintf(media_json, sizeof(media_json),
             "{\"event\":\"media\",\"streamSid\":\"MZtest\",\"media\":{\"payload\":\"%s\"}}", b64);
    RTW_CHECK(rtw_session_handle_peer_json(&s, media_json) == 0);
    RTW_CHECK(rtw_session_handle_peer_json(
                  &s, "{\"event\":\"mark\",\"streamSid\":\"MZtest\",\"mark\":{\"name\":\"utt1\"}}") == 0);
    RTW_CHECK(rtw_playout_size(&s.playout) == 160);
    RTW_CHECK(rtw_session_handle_peer_json(&s, "{\"event\":\"clear\",\"streamSid\":\"MZtest\"}") == 0);
    RTW_CHECK(rtw_playout_size(&s.playout) == 0);
    RTW_CHECK(s.clear_events == 1);
    /* mark ack after clear */
    RTW_CHECK(rtw_session_pop_outbound(&s, &json) == 0);
    RTW_CHECK(rtw_parse_inbound(json, &ev, buf, sizeof(buf)) == 0);
    RTW_CHECK(ev.type == RTW_EVT_MARK);
    RTW_CHECK(strcmp(ev.mark_name, "utt1") == 0);
    free(json);

    /* silent write samples clear latency */
    rtw_session_note_write_after_clear(&s, 0);
    RTW_CHECK(s.clear_latency_samples == 1);

    RTW_CHECK(rtw_session_rehandshake(&s) == 0);
    RTW_CHECK(rtw_session_pop_outbound(&s, &json) == 0);
    RTW_CHECK(strstr(json, "connected"));
    free(json);
    RTW_CHECK(rtw_session_pop_outbound(&s, &json) == 0);
    RTW_CHECK(strstr(json, "start"));
    RTW_CHECK(strstr(json, "app"));
    free(json);

    RTW_CHECK(rtw_session_stop(&s) == 0);
    RTW_CHECK(rtw_session_pop_outbound(&s, &json) == 0);
    RTW_CHECK(strstr(json, "stop"));
    free(json);
    rtw_session_destroy(&s);
}

static void run(void)
{
    test_playout_mark_clear();
    test_queue_drop_oldest();
    test_queue_peek_drop();
    test_session_uplink_and_clear();
}

RTW_TEST_MAIN(run)
