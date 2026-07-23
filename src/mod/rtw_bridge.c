#include "mod_realtime_ws.h"
#include "rtw_mulaw.h"
#include "cJSON.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/*
 * Thread contract (see docs/ARCHITECTURE.md §7):
 * - Media-bug paths (on_read / on_write): mutate session under mutex, NEVER WS I/O.
 * - WS worker: poll + flush_outbound (+ reconnect).
 * - on_ws_text runs inside worker poll → may flush.
 */

static int stopping(const rtw_tech_t *tech)
{
    return !tech || tech->close_requested || tech->cleanup_started;
}

/* Send queued JSON without dropping when socket is down (peek → send → drop). */
static void flush_outbound(rtw_tech_t *tech)
{
    const char *json;
    size_t len;
    if (!tech->ws || !tech->ws_ready) {
        return;
    }
    while (rtw_session_peek_outbound(&tech->session, &json, &len) == 0) {
        if (rtw_ws_send_text(tech->ws, json, len) != 0) {
            tech->ws_ready = 0;
            return;
        }
        rtw_session_drop_outbound_head(&tech->session);
    }
}

static void on_ws_text(void *userdata, const char *text, size_t len)
{
    rtw_tech_t *tech = (rtw_tech_t *)userdata;
    (void)len;
    if (stopping(tech)) {
        return;
    }
    if (switch_mutex_lock(tech->mutex) != SWITCH_STATUS_SUCCESS) {
        return;
    }
    if (!stopping(tech)) {
        rtw_session_handle_peer_json(&tech->session, text);
        flush_outbound(tech);
    }
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

static int sleep_interruptible_ms(rtw_tech_t *tech, int total_ms)
{
    int left = total_ms;
    while (left > 0) {
        int slice = left > 50 ? 50 : left;
        if (stopping(tech) || !tech->reconnect_enabled) {
            return -1;
        }
        usleep((useconds_t)slice * 1000);
        left -= slice;
    }
    return stopping(tech) ? -1 : 0;
}

static int try_reconnect(rtw_tech_t *tech)
{
    int backoff_ms;
    int shift;
    if (!tech->reconnect_enabled || stopping(tech)) {
        return -1;
    }
    if (tech->reconnect_max > 0 && tech->reconnect_attempts >= tech->reconnect_max) {
        return -1;
    }
    tech->reconnect_attempts++;
    shift = tech->reconnect_attempts > 5 ? 5 : tech->reconnect_attempts;
    backoff_ms = 100 << shift;
    if (backoff_ms > 3000) {
        backoff_ms = 3000;
    }
    if (sleep_interruptible_ms(tech, backoff_ms) != 0) {
        return -1;
    }

    if (tech->ws) {
        rtw_ws_close(tech->ws);
        tech->ws = NULL;
    }
    tech->ws = rtw_ws_connect_ex(tech->ws_uri, tech->extra_headers, on_ws_text, on_ws_close, tech);
    if (!tech->ws) {
        return -1;
    }
    if (stopping(tech)) {
        rtw_ws_close(tech->ws);
        tech->ws = NULL;
        return -1;
    }
    tech->ws_ready = 1;
    if (switch_mutex_lock(tech->mutex) == SWITCH_STATUS_SUCCESS) {
        if (!stopping(tech)) {
            rtw_session_rehandshake(&tech->session);
            flush_outbound(tech);
        }
        switch_mutex_unlock(tech->mutex);
    }
    tech->reconnect_ok++;
    tech->reconnect_attempts = 0;
    return 0;
}

static void *ws_worker(void *arg)
{
    rtw_tech_t *tech = (rtw_tech_t *)arg;
    while (tech && !stopping(tech)) {
        if (tech->ws && tech->ws_ready) {
            if (rtw_ws_poll(tech->ws, 20) < 0) {
                tech->ws_ready = 0;
                if (try_reconnect(tech) != 0) {
                    break;
                }
                continue;
            }
        } else if (tech->ws && !tech->ws_ready) {
            if (try_reconnect(tech) != 0) {
                break;
            }
            continue;
        } else {
            usleep(20 * 1000);
        }
        if (stopping(tech)) {
            break;
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
    if (strncmp(url, "ws://", 5) == 0) {
        /* ok */
    } else if (strncmp(url, "wss://", 6) == 0) {
#ifdef RTW_HAS_OPENSSL
        /* ok */
#else
        return 0;
#endif
    } else {
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

static int16_t clamp_s16(int v)
{
    if (v > 32767) {
        return 32767;
    }
    if (v < -32768) {
        return -32768;
    }
    return (int16_t)v;
}

/*
 * Split start metadata JSON into:
 *  - extra_headers for WS handshake (authorization / ws_headers)
 *  - peer customParameters (remaining object, auth keys stripped)
 * Both outputs are malloc'd (or NULL). Returns 0 ok.
 */
static int split_metadata(const char *metadata, char **extra_headers_out, char **peer_params_out)
{
    cJSON *root;
    cJSON *auth;
    cJSON *headers;
    cJSON *item;
    char *hdr = NULL;
    size_t hdr_cap = 0;
    size_t hdr_len = 0;
    *extra_headers_out = NULL;
    *peer_params_out = NULL;
    if (!metadata || !metadata[0]) {
        return 0;
    }
    root = cJSON_Parse(metadata);
    if (!root || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        /* Non-JSON metadata: treat whole string as opaque custom param object wrapper */
        *peer_params_out = strdup(metadata);
        return *peer_params_out ? 0 : -1;
    }

    auth = cJSON_GetObjectItemCaseSensitive(root, "authorization");
    if (cJSON_IsString(auth) && auth->valuestring && auth->valuestring[0]) {
        size_t need = strlen(auth->valuestring) + 32;
        char *line = (char *)malloc(need);
        if (!line) {
            cJSON_Delete(root);
            return -1;
        }
        snprintf(line, need, "Authorization: %s\r\n", auth->valuestring);
        hdr = line;
        hdr_len = strlen(hdr);
        hdr_cap = need;
        cJSON_DeleteItemFromObject(root, "authorization");
    }

    headers = cJSON_GetObjectItemCaseSensitive(root, "ws_headers");
    if (cJSON_IsObject(headers)) {
        cJSON_ArrayForEach(item, headers)
        {
            const char *key = item->string;
            const char *val = cJSON_IsString(item) ? item->valuestring : NULL;
            size_t need;
            char *nbuf;
            if (!key || !val) {
                continue;
            }
            need = hdr_len + strlen(key) + strlen(val) + 8;
            nbuf = (char *)realloc(hdr, need);
            if (!nbuf) {
                free(hdr);
                cJSON_Delete(root);
                return -1;
            }
            hdr = nbuf;
            hdr_cap = need;
            (void)hdr_cap;
            snprintf(hdr + hdr_len, need - hdr_len, "%s: %s\r\n", key, val);
            hdr_len = strlen(hdr);
        }
        cJSON_DeleteItemFromObject(root, "ws_headers");
    }

    if (hdr && hdr[0]) {
        *extra_headers_out = hdr;
    } else {
        free(hdr);
    }

    if (cJSON_GetArraySize(root) > 0 || root->child) {
        *peer_params_out = cJSON_PrintUnformatted(root);
    }
    cJSON_Delete(root);
    return 0;
}

static void tech_free_meta(rtw_tech_t *tech)
{
    free(tech->extra_headers);
    tech->extra_headers = NULL;
    free(tech->peer_custom_params);
    tech->peer_custom_params = NULL;
}

switch_status_t rtw_bridge_start(switch_core_session_t *session, const char *ws_uri, int sampling,
                                 int channels, const char *metadata, rtw_tech_t **out_tech)
{
    rtw_tech_t *tech;
    const char *uuid;
    char sid[RTW_SID_MAX];
    char call_sid[RTW_SID_MAX];
    const char *env;
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
    tech->inject_mode = RTW_INJECT_REPLACE;
    tech->reconnect_enabled = 1;
    tech->reconnect_max = 10;
    tech->record_injected = 1;
    env = getenv("RTW_RECONNECT");
    if (env && env[0] == '0') {
        tech->reconnect_enabled = 0;
    }
    env = getenv("RTW_INJECT_MIX");
    if (env && env[0] == '1') {
        tech->inject_mode = RTW_INJECT_MIX;
    }
    make_stream_sid(sid, sizeof(sid), tech->session_id);
    make_call_sid(call_sid, sizeof(call_sid), tech->session_id);
    snprintf(tech->stream_sid, sizeof(tech->stream_sid), "%s", sid);
    snprintf(tech->call_sid, sizeof(tech->call_sid), "%s", call_sid);

    if (split_metadata(tech->metadata[0] ? tech->metadata : NULL, &tech->extra_headers,
                       &tech->peer_custom_params) != 0) {
        switch_mutex_destroy(tech->mutex);
        free(tech);
        return SWITCH_STATUS_FALSE;
    }

    if (rtw_session_init(&tech->session, 256, 64 * 1024) != 0) {
        tech_free_meta(tech);
        switch_mutex_destroy(tech->mutex);
        free(tech);
        return SWITCH_STATUS_FALSE;
    }
    if (rtw_session_start(&tech->session, tech->stream_sid, tech->call_sid, "ACmodrealtimews0000000000000000000",
                          tech->peer_custom_params) != 0) {
        rtw_session_destroy(&tech->session);
        tech_free_meta(tech);
        switch_mutex_destroy(tech->mutex);
        free(tech);
        return SWITCH_STATUS_FALSE;
    }

    tech->ws = rtw_ws_connect_ex(ws_uri, tech->extra_headers, on_ws_text, on_ws_close, tech);
    if (!tech->ws) {
        rtw_session_destroy(&tech->session);
        tech_free_meta(tech);
        switch_mutex_destroy(tech->mutex);
        free(tech);
        return SWITCH_STATUS_FALSE;
    }
    tech->ws_ready = 1;
    flush_outbound(tech);

    if (pthread_create(&tech->worker, NULL, ws_worker, tech) != 0) {
        rtw_ws_close(tech->ws);
        rtw_session_destroy(&tech->session);
        tech_free_meta(tech);
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
    if (tech->cleanup_started) {
        return SWITCH_STATUS_SUCCESS;
    }
    tech->cleanup_started = 1;
    tech->close_requested = 1;
    tech->reconnect_enabled = 0;
    if (tech->worker_started) {
        pthread_join(tech->worker, NULL);
        tech->worker_started = 0;
    }
    if (tech->mutex && switch_mutex_lock(tech->mutex) == SWITCH_STATUS_SUCCESS) {
        rtw_session_stop(&tech->session);
        /* Best-effort: try to flush stop even if worker marked ws not ready. */
        if (tech->ws) {
            tech->ws_ready = 1;
        }
        flush_outbound(tech);
        switch_mutex_unlock(tech->mutex);
    }
    if (tech->ws) {
        rtw_ws_close(tech->ws);
        tech->ws = NULL;
        tech->ws_ready = 0;
    }
    rtw_session_destroy(&tech->session);
    tech_free_meta(tech);
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
    if (!tech || !pcm || stopping(tech) || tech->audio_paused) {
        return SWITCH_TRUE;
    }
    if (switch_mutex_trylock(tech->mutex) != SWITCH_STATUS_SUCCESS) {
        return SWITCH_TRUE;
    }
    /* Stereo L0: take left channel only (Twilio inbound is mono). */
    if (tech->channels >= 2) {
        while (i + 1 < nsamples) {
            int16_t sample = pcm[i];
            i += 2;
            if (tech->sampling == 16000) {
                /* Already at wire target after stereo fold; if source is 16k stereo,
                 * pair-average successive left samples when available. */
                if (i + 1 < nsamples) {
                    sample = (int16_t)(((int)sample + (int)pcm[i]) / 2);
                    i += 2;
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
    } else {
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
    } else {
        rtw_session_note_write_after_clear(&tech->session, 0);
    }
    switch_mutex_unlock(tech->mutex);
    return n;
}

size_t rtw_bridge_apply_write_frame(rtw_tech_t *tech, int16_t *inout_pcm, size_t nsamples)
{
    int16_t agent[640];
    uint8_t mulaw[640];
    size_t n;
    size_t i;
    size_t want;
    rtw_inject_mode_t mode;
    if (!tech || !inout_pcm || nsamples == 0 || tech->cleanup_started) {
        return 0;
    }
    want = nsamples < (sizeof(agent) / sizeof(agent[0])) ? nsamples : (sizeof(agent) / sizeof(agent[0]));
    if (switch_mutex_trylock(tech->mutex) != SWITCH_STATUS_SUCCESS) {
        return 0;
    }
    mode = tech->inject_mode;
    n = rtw_session_read_playout(&tech->session, mulaw, want);
    if (n > 0) {
        rtw_mulaw_to_pcm16(mulaw, n, agent);
    } else {
        rtw_session_note_write_after_clear(&tech->session, 0);
    }
    switch_mutex_unlock(tech->mutex);
    if (n == 0) {
        return 0;
    }
    if (mode == RTW_INJECT_MIX) {
        for (i = 0; i < n; i++) {
            inout_pcm[i] = clamp_s16((int)inout_pcm[i] + (int)agent[i]);
        }
    } else {
        memcpy(inout_pcm, agent, n * sizeof(int16_t));
        if (n < nsamples) {
            memset(inout_pcm + n, 0, (nsamples - n) * sizeof(int16_t));
        }
    }
    return n;
}

switch_status_t rtw_bridge_get_stats(rtw_tech_t *tech, rtw_bridge_stats_t *out)
{
    if (!tech || !out || tech->cleanup_started) {
        return SWITCH_STATUS_FALSE;
    }
    memset(out, 0, sizeof(*out));
    if (switch_mutex_lock(tech->mutex) != SWITCH_STATUS_SUCCESS) {
        return SWITCH_STATUS_FALSE;
    }
    snprintf(out->stream_sid, sizeof(out->stream_sid), "%s", tech->stream_sid);
    snprintf(out->call_sid, sizeof(out->call_sid), "%s", tech->call_sid);
    out->ws_ready = tech->ws_ready;
    out->reconnect_enabled = tech->reconnect_enabled;
    out->reconnect_ok = tech->reconnect_ok;
    out->uplink_frames = tech->session.uplink_frames;
    out->downlink_frames = tech->session.downlink_frames;
    out->clear_events = tech->session.clear_events;
    out->clear_latency_last_us = tech->session.clear_latency_last_us;
    out->clear_latency_max_us = tech->session.clear_latency_max_us;
    out->clear_latency_samples = tech->session.clear_latency_samples;
    if (tech->session.clear_latency_samples) {
        out->clear_latency_avg_us =
            tech->session.clear_latency_sum_us / tech->session.clear_latency_samples;
    }
    out->outbound_queue = rtw_queue_size(&tech->session.outbound);
    out->playout_bytes = rtw_playout_size(&tech->session.playout);
    out->record_injected = tech->record_injected;
    out->inject_mode = (int)tech->inject_mode;
    switch_mutex_unlock(tech->mutex);
    return SWITCH_STATUS_SUCCESS;
}
