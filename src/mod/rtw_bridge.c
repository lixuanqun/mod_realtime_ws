#include "mod_realtime_ws.h"
#include "rtw_mulaw.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/*
 * Thread contract (see docs/ARCHITECTURE.md §7):
 * - Media-bug paths (on_read / on_write): mutate session under mutex, NEVER WS I/O.
 * - WS worker: poll + flush_outbound (only place that sends).
 * - on_ws_text runs inside worker poll → may flush.
 */

static void flush_outbound(rtw_tech_t *tech)
{
    char *json;
    while (rtw_session_pop_outbound(&tech->session, &json) == 0) {
        if (tech->ws && tech->ws_ready) {
            rtw_ws_send_text(tech->ws, json, strlen(json));
        }
        free(json);
    }
}

static void on_ws_text(void *userdata, const char *text, size_t len)
{
    rtw_tech_t *tech = (rtw_tech_t *)userdata;
    (void)len;
    if (!tech || tech->close_requested || tech->cleanup_started) {
        return;
    }
    if (switch_mutex_lock(tech->mutex) != SWITCH_STATUS_SUCCESS) {
        return;
    }
    rtw_session_handle_peer_json(&tech->session, text);
    flush_outbound(tech);
    switch_mutex_unlock(tech->mutex);
}

static void on_ws_close(void *userdata, int code)
{
    rtw_tech_t *tech = (rtw_tech_t *)userdata;
    (void)code;
    if (tech) {
        tech->ws_ready = 0;
    }
}

static void *ws_worker(void *arg)
{
    rtw_tech_t *tech = (rtw_tech_t *)arg;
    while (tech && !tech->close_requested && !tech->cleanup_started) {
        if (tech->ws) {
            if (rtw_ws_poll(tech->ws, 20) < 0) {
                tech->ws_ready = 0;
                break;
            }
        } else {
            usleep(20 * 1000);
        }
        if (switch_mutex_trylock(tech->mutex) == SWITCH_STATUS_SUCCESS) {
            flush_outbound(tech);
            switch_mutex_unlock(tech->mutex);
        }
    }
    return NULL;
}

int rtw_validate_ws_uri(const char *url, char *out, size_t out_cap)
{
    if (!url || !out || out_cap < 8) {
        return 0;
    }
    /* L0 client currently supports plain ws:// only (no TLS yet). Reject wss://
     * loudly so operators do not think TLS is on. */
    if (strncmp(url, "ws://", 5) != 0) {
        return 0;
    }
    if (strlen(url) >= out_cap) {
        return 0;
    }
    snprintf(out, out_cap, "%s", url);
    return 1;
}

static void hex_compact(const char *uuid, char *out, size_t out_cap, size_t want)
{
    size_t i, j = 0;
    if (!out || out_cap == 0) {
        return;
    }
    memset(out, 0, out_cap);
    if (uuid) {
        for (i = 0; uuid[i] && j + 1 < out_cap && j < want; i++) {
            char c = uuid[i];
            if (isxdigit((unsigned char)c)) {
                out[j++] = (char)tolower((unsigned char)c);
            }
        }
    }
    while (j < want && j + 1 < out_cap) {
        out[j++] = '0';
    }
    out[j < out_cap ? j : out_cap - 1] = '\0';
}

static void make_stream_sid(char *out, size_t cap, const char *uuid)
{
    char compact[33];
    hex_compact(uuid, compact, sizeof(compact), 32);
    snprintf(out, cap, "MZ%s", compact);
}

static void make_call_sid(char *out, size_t cap, const char *uuid)
{
    char compact[33];
    hex_compact(uuid, compact, sizeof(compact), 32);
    snprintf(out, cap, "CA%s", compact);
}

static int valid_mark_name(const char *name)
{
    size_t i;
    if (!name || !name[0] || strlen(name) >= RTW_MARK_NAME_LEN) {
        return 0;
    }
    for (i = 0; name[i]; i++) {
        char c = name[i];
        if (!(isalnum((unsigned char)c) || c == '_' || c == '-' || c == '.')) {
            return 0;
        }
    }
    return 1;
}

switch_status_t rtw_bridge_start(switch_core_session_t *session, const char *ws_uri, int sampling,
                                 int channels, const char *metadata, rtw_tech_t **out_tech)
{
    rtw_tech_t *tech;
    const char *uuid;
    char sid[RTW_SID_MAX];
    char call_sid[RTW_SID_MAX];
    if (!session || !ws_uri || !out_tech) {
        return SWITCH_STATUS_FALSE;
    }
    if (sampling != 8000 && sampling != 16000) {
        if (sampling % 8000 != 0) {
            return SWITCH_STATUS_FALSE;
        }
    }
    tech = (rtw_tech_t *)calloc(1, sizeof(*tech));
    if (!tech) {
        return SWITCH_STATUS_FALSE;
    }
    if (switch_mutex_init(&tech->mutex, 0, NULL) != SWITCH_STATUS_SUCCESS) {
        free(tech);
        return SWITCH_STATUS_FALSE;
    }
    uuid = switch_core_session_get_uuid(session);
    snprintf(tech->session_id, sizeof(tech->session_id), "%s", uuid ? uuid : "unknown");
    snprintf(tech->ws_uri, sizeof(tech->ws_uri), "%s", ws_uri);
    if (metadata) {
        snprintf(tech->metadata, sizeof(tech->metadata), "%s", metadata);
    }
    tech->sampling = sampling > 0 ? sampling : 8000;
    tech->channels = channels > 0 ? channels : 1;
    make_stream_sid(sid, sizeof(sid), tech->session_id);
    make_call_sid(call_sid, sizeof(call_sid), tech->session_id);
    snprintf(tech->stream_sid, sizeof(tech->stream_sid), "%s", sid);
    snprintf(tech->call_sid, sizeof(tech->call_sid), "%s", call_sid);

    if (rtw_session_init(&tech->session, 256, 64 * 1024) != 0) {
        switch_mutex_destroy(tech->mutex);
        free(tech);
        return SWITCH_STATUS_FALSE;
    }
    if (rtw_session_start(&tech->session, tech->stream_sid, tech->call_sid, "ACmodrealtimews000000000000000000",
                          tech->metadata[0] ? tech->metadata : NULL) != 0) {
        rtw_session_destroy(&tech->session);
        switch_mutex_destroy(tech->mutex);
        free(tech);
        return SWITCH_STATUS_FALSE;
    }

    tech->ws = rtw_ws_connect(ws_uri, on_ws_text, on_ws_close, tech);
    if (!tech->ws) {
        rtw_session_destroy(&tech->session);
        switch_mutex_destroy(tech->mutex);
        free(tech);
        return SWITCH_STATUS_FALSE;
    }
    tech->ws_ready = 1;
    /* Startup handshake flush is allowed on the start thread before the worker runs. */
    flush_outbound(tech);

    if (pthread_create(&tech->worker, NULL, ws_worker, tech) != 0) {
        rtw_ws_close(tech->ws);
        rtw_session_destroy(&tech->session);
        switch_mutex_destroy(tech->mutex);
        free(tech);
        return SWITCH_STATUS_FALSE;
    }
    tech->worker_started = 1;
    *out_tech = tech;
    return SWITCH_STATUS_SUCCESS;
}

switch_status_t rtw_bridge_stop(switch_core_session_t *session, rtw_tech_t *tech, int channel_closing)
{
    (void)session;
    (void)channel_closing;
    if (!tech) {
        return SWITCH_STATUS_FALSE;
    }
    /* Idempotent: media-bug CLOSE and explicit stop must both be safe. */
    if (tech->cleanup_started) {
        return SWITCH_STATUS_SUCCESS;
    }
    tech->cleanup_started = 1;
    tech->close_requested = 1;
    if (tech->worker_started) {
        pthread_join(tech->worker, NULL);
        tech->worker_started = 0;
    }
    if (tech->mutex && switch_mutex_lock(tech->mutex) == SWITCH_STATUS_SUCCESS) {
        rtw_session_stop(&tech->session);
        flush_outbound(tech);
        switch_mutex_unlock(tech->mutex);
    }
    if (tech->ws) {
        rtw_ws_close(tech->ws);
        tech->ws = NULL;
        tech->ws_ready = 0;
    }
    rtw_session_destroy(&tech->session);
    if (tech->mutex) {
        switch_mutex_destroy(tech->mutex);
        tech->mutex = NULL;
    }
    free(tech);
    return SWITCH_STATUS_SUCCESS;
}

switch_status_t rtw_bridge_pause(rtw_tech_t *tech, int pause)
{
    if (!tech || tech->cleanup_started) {
        return SWITCH_STATUS_FALSE;
    }
    if (switch_mutex_lock(tech->mutex) != SWITCH_STATUS_SUCCESS) {
        return SWITCH_STATUS_FALSE;
    }
    tech->audio_paused = pause ? 1 : 0;
    tech->session.paused = tech->audio_paused;
    switch_mutex_unlock(tech->mutex);
    return SWITCH_STATUS_SUCCESS;
}

switch_status_t rtw_bridge_clear(rtw_tech_t *tech)
{
    char json[128];
    if (!tech || tech->cleanup_started) {
        return SWITCH_STATUS_FALSE;
    }
    snprintf(json, sizeof(json), "{\"event\":\"clear\",\"streamSid\":\"%s\"}", tech->stream_sid);
    if (switch_mutex_lock(tech->mutex) != SWITCH_STATUS_SUCCESS) {
        return SWITCH_STATUS_FALSE;
    }
    rtw_session_handle_peer_json(&tech->session, json);
    /* Local clear may enqueue mark ACKs; flush from this control path is OK
     * (API/ESL thread, not media bug). */
    flush_outbound(tech);
    switch_mutex_unlock(tech->mutex);
    return SWITCH_STATUS_SUCCESS;
}

switch_status_t rtw_bridge_send_mark(rtw_tech_t *tech, const char *name)
{
    char json[256];
    if (!tech || !name || tech->cleanup_started || !valid_mark_name(name)) {
        return SWITCH_STATUS_FALSE;
    }
    /* Operator/API mark: treat as peer mark (queued against playout) for testing
     * barge-in / ACK paths. Not a Twilio wire event from FS→peer. */
    snprintf(json, sizeof(json),
             "{\"event\":\"mark\",\"streamSid\":\"%s\",\"mark\":{\"name\":\"%s\"}}", tech->stream_sid, name);
    if (switch_mutex_lock(tech->mutex) != SWITCH_STATUS_SUCCESS) {
        return SWITCH_STATUS_FALSE;
    }
    rtw_session_handle_peer_json(&tech->session, json);
    flush_outbound(tech);
    switch_mutex_unlock(tech->mutex);
    return SWITCH_STATUS_SUCCESS;
}

switch_bool_t rtw_bridge_on_read_pcm16(rtw_tech_t *tech, const int16_t *pcm, size_t nsamples)
{
    size_t i = 0;
    if (!tech || !pcm || tech->close_requested || tech->cleanup_started || tech->audio_paused) {
        return SWITCH_TRUE;
    }
    if (switch_mutex_trylock(tech->mutex) != SWITCH_STATUS_SUCCESS) {
        return SWITCH_TRUE;
    }
    /* Enqueue only — worker flushes WS. */
    while (i < nsamples) {
        int16_t sample = pcm[i++];
        if (tech->sampling == 16000) {
            if (i < nsamples) {
                sample = (int16_t)(((int)sample + (int)pcm[i++]) / 2);
            }
        }
        if (tech->pcm_hold_len < sizeof(tech->pcm_hold) / sizeof(tech->pcm_hold[0])) {
            tech->pcm_hold[tech->pcm_hold_len++] = sample;
        }
        if (tech->pcm_hold_len >= 160) {
            rtw_session_push_pcm16(&tech->session, tech->pcm_hold, 160);
            tech->pcm_hold_len = 0;
        }
    }
    switch_mutex_unlock(tech->mutex);
    return SWITCH_TRUE;
}

size_t rtw_bridge_on_write_pcm16(rtw_tech_t *tech, int16_t *out, size_t max_samples)
{
    uint8_t mulaw[640];
    size_t n;
    size_t want;
    if (!tech || !out || max_samples == 0 || tech->cleanup_started) {
        return 0;
    }
    if (switch_mutex_trylock(tech->mutex) != SWITCH_STATUS_SUCCESS) {
        return 0;
    }
    want = max_samples < sizeof(mulaw) ? max_samples : sizeof(mulaw);
    n = rtw_session_read_playout(&tech->session, mulaw, want);
    if (n > 0) {
        rtw_mulaw_to_pcm16(mulaw, n, out);
    }
    /* Mark ACKs stay queued until the WS worker flushes — no I/O here. */
    switch_mutex_unlock(tech->mutex);
    return n;
}
