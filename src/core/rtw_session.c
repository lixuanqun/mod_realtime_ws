#include "rtw_session.h"
#include "rtw_mulaw.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int rtw_session_init(rtw_session_t *s, size_t out_queue_cap, size_t playout_cap)
{
    if (!s) {
        return -1;
    }
    memset(s, 0, sizeof(*s));
    if (rtw_queue_init(&s->outbound, out_queue_cap ? out_queue_cap : 64) != 0) {
        return -1;
    }
    if (rtw_playout_init(&s->playout, playout_cap ? playout_cap : (8 * 8000)) != 0) {
        rtw_queue_destroy(&s->outbound);
        return -1;
    }
    s->state = RTW_STATE_IDLE;
    return 0;
}

void rtw_session_destroy(rtw_session_t *s)
{
    if (!s) {
        return;
    }
    rtw_queue_destroy(&s->outbound);
    rtw_playout_destroy(&s->playout);
    memset(s, 0, sizeof(*s));
}

static int enqueue_json(rtw_session_t *s, char *json)
{
    int rc;
    if (!json) {
        return -1;
    }
    rc = rtw_queue_push(&s->outbound, json, strlen(json));
    free(json);
    return rc;
}

int rtw_session_start(rtw_session_t *s, const char *stream_sid, const char *call_sid,
                      const char *account_sid, const char *custom_params_json)
{
    char *connected;
    char *start;
    if (!s || !stream_sid) {
        return -1;
    }
    snprintf(s->stream_sid, sizeof(s->stream_sid), "%s", stream_sid);
    snprintf(s->call_sid, sizeof(s->call_sid), "%s", call_sid ? call_sid : stream_sid);
    snprintf(s->account_sid, sizeof(s->account_sid), "%s",
             account_sid ? account_sid : "AC00000000000000000000000000000000");
    s->seq = 1;
    s->chunk = 1;
    s->timestamp_ms = 0;
    s->state = RTW_STATE_STARTING;
    connected = rtw_build_connected("1.0.0");
    if (enqueue_json(s, connected) != 0) {
        return -1;
    }
    start = rtw_build_start(s->stream_sid, s->account_sid, s->call_sid, custom_params_json, s->seq++);
    if (enqueue_json(s, start) != 0) {
        return -1;
    }
    s->state = RTW_STATE_RUNNING;
    return 0;
}

int rtw_session_push_pcm16(rtw_session_t *s, const int16_t *pcm, size_t nsamples)
{
    size_t offset = 0;
    if (!s || s->state != RTW_STATE_RUNNING || s->paused) {
        return -1;
    }
    if (!pcm && nsamples) {
        return -1;
    }
    while (offset + RTW_FRAME_MULAW_BYTES <= nsamples) {
        uint8_t mulaw[RTW_FRAME_MULAW_BYTES];
        char *json;
        rtw_pcm16_to_mulaw(pcm + offset, RTW_FRAME_MULAW_BYTES, mulaw);
        json = rtw_build_media(s->stream_sid, "inbound", s->chunk, s->timestamp_ms, s->seq++,
                               mulaw, RTW_FRAME_MULAW_BYTES);
        if (enqueue_json(s, json) != 0) {
            return -1;
        }
        s->chunk++;
        s->timestamp_ms += 20;
        s->uplink_frames++;
        offset += RTW_FRAME_MULAW_BYTES;
    }
    return (int)(nsamples - offset); /* leftover samples not consumed */
}

int rtw_session_handle_peer_json(rtw_session_t *s, const char *json)
{
    rtw_inbound_event_t ev;
    uint8_t *payload;
    const size_t payload_cap = 64 * 1024;
    char flushed[RTW_MARK_MAX][RTW_MARK_NAME_LEN];
    size_t nflush;
    size_t i;
    int rc = -1;
    if (!s || !json || s->state != RTW_STATE_RUNNING) {
        return -1;
    }
    payload = (uint8_t *)malloc(payload_cap);
    if (!payload) {
        return -1;
    }
    if (rtw_parse_inbound(json, &ev, payload, payload_cap) != 0) {
        free(payload);
        return -1;
    }
    if (ev.stream_sid[0] && strcmp(ev.stream_sid, s->stream_sid) != 0) {
        free(payload);
        return 1;
    }
    switch (ev.type) {
    case RTW_EVT_MEDIA:
        if (rtw_playout_write(&s->playout, ev.payload, ev.payload_len) != 0) {
            rc = -2;
            break;
        }
        s->downlink_frames++;
        rc = 0;
        break;
    case RTW_EVT_MARK:
        rc = rtw_playout_add_mark(&s->playout, ev.mark_name) == 0 ? 0 : -1;
        break;
    case RTW_EVT_CLEAR:
        nflush = rtw_playout_clear(&s->playout, flushed, RTW_MARK_MAX);
        s->clear_events++;
        rc = 0;
        for (i = 0; i < nflush; i++) {
            char *ack = rtw_build_mark(s->stream_sid, flushed[i], s->seq++);
            if (enqueue_json(s, ack) != 0) {
                rc = -1;
                break;
            }
        }
        break;
    default:
        rc = 0;
        break;
    }
    free(payload);
    return rc;
}

int rtw_session_pop_outbound(rtw_session_t *s, char **json_out)
{
    rtw_queue_item_t item;
    char completed[8][RTW_MARK_NAME_LEN];
    size_t n;
    size_t i;
    if (!s || !json_out) {
        return -1;
    }
    /* Harvest naturally completed marks into outbound ACKs before pop. */
    n = rtw_playout_pop_completed_marks(&s->playout, completed, 8);
    for (i = 0; i < n; i++) {
        char *ack = rtw_build_mark(s->stream_sid, completed[i], s->seq++);
        enqueue_json(s, ack);
    }
    if (rtw_queue_pop(&s->outbound, &item) != 0) {
        return -1;
    }
    *json_out = item.data;
    return 0;
}

/* Harvest mark ACKs then peek/send-friendly accessors used by bridge flush. */
void rtw_session_harvest_marks(rtw_session_t *s)
{
    char completed[8][RTW_MARK_NAME_LEN];
    size_t n;
    size_t i;
    if (!s) {
        return;
    }
    n = rtw_playout_pop_completed_marks(&s->playout, completed, 8);
    for (i = 0; i < n; i++) {
        char *ack = rtw_build_mark(s->stream_sid, completed[i], s->seq++);
        enqueue_json(s, ack);
    }
}

int rtw_session_peek_outbound(rtw_session_t *s, const char **json_out, size_t *len_out)
{
    if (!s) {
        return -1;
    }
    rtw_session_harvest_marks(s);
    return rtw_queue_peek(&s->outbound, json_out, len_out);
}

int rtw_session_drop_outbound_head(rtw_session_t *s)
{
    if (!s) {
        return -1;
    }
    return rtw_queue_drop_head(&s->outbound);
}

size_t rtw_session_read_playout(rtw_session_t *s, uint8_t *out, size_t max_len)
{
    if (!s) {
        return 0;
    }
    return rtw_playout_read(&s->playout, out, max_len);
}

int rtw_session_stop(rtw_session_t *s)
{
    char *stop;
    if (!s || s->state == RTW_STATE_STOPPED || s->state == RTW_STATE_IDLE) {
        return -1;
    }
    s->state = RTW_STATE_STOPPING;
    stop = rtw_build_stop(s->stream_sid, s->account_sid, s->call_sid, s->seq++);
    if (enqueue_json(s, stop) != 0) {
        return -1;
    }
    s->state = RTW_STATE_STOPPED;
    return 0;
}

int rtw_session_rehandshake(rtw_session_t *s)
{
    char *connected;
    char *start;
    if (!s || s->state != RTW_STATE_RUNNING) {
        return -1;
    }
    connected = rtw_build_connected("1.0.0");
    if (enqueue_json(s, connected) != 0) {
        return -1;
    }
    start = rtw_build_start(s->stream_sid, s->account_sid, s->call_sid, NULL, s->seq++);
    if (enqueue_json(s, start) != 0) {
        return -1;
    }
    return 0;
}
