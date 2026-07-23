/*
 * Copyright (c) 2026 mod_realtime_ws contributors
 * SPDX-License-Identifier: MIT
 *
 * FreeSWITCH module header — patterned after mod_audio_stream's private_t /
 * media-bug lifecycle, with Twilio L0 session (rtw_session) underneath.
 */
#ifndef MOD_REALTIME_WS_H
#define MOD_REALTIME_WS_H

#include <stdint.h>
#include <pthread.h>

#ifdef HAVE_FREESWITCH
#include <switch.h>
#else
#include "fs_stub/switch.h"
#endif

#include "rtw_session.h"
#include "rtw_ws_client.h"

#define RTW_BUG_NAME "realtime_ws"
#define RTW_MAX_WS_URI 4096
#define RTW_MAX_META 8192
#define RTW_MAX_SID 64

#define RTW_EVENT_CONNECT "mod_realtime_ws::connect"
#define RTW_EVENT_DISCONNECT "mod_realtime_ws::disconnect"
#define RTW_EVENT_ERROR "mod_realtime_ws::error"
#define RTW_EVENT_JSON "mod_realtime_ws::json"

typedef enum {
    RTW_INJECT_REPLACE = 0, /* replace frame when playout has data; else passthrough */
    RTW_INJECT_MIX = 1      /* soft-mix agent audio into existing write frame */
} rtw_inject_mode_t;

typedef struct rtw_tech_private {
    switch_mutex_t *mutex;
    char session_id[RTW_MAX_SID];
    char stream_sid[RTW_SID_MAX];
    char call_sid[RTW_SID_MAX];
    char ws_uri[RTW_MAX_WS_URI];
    char metadata[RTW_MAX_META];
    char *extra_headers;      /* owned; Authorization / ws_headers for handshake */
    char *peer_custom_params; /* owned; metadata minus auth keys for Twilio start */
    int sampling; /* source rate from FS; L0 wire is always 8k mulaw */
    int channels;
    int audio_paused;
    volatile int close_requested;
    volatile int cleanup_started; /* idempotent stop / CLOSE */
    volatile int ws_ready;
    rtw_session_t session;
    rtw_ws_client_t *ws;
    pthread_t worker;
    int worker_started;
    /* leftover PCM samples when frame size != 160 */
    int16_t pcm_hold[320];
    size_t pcm_hold_len;
    /* inject / reconnect / record policy */
    rtw_inject_mode_t inject_mode;
    int reconnect_enabled;
    int reconnect_max;
    int reconnect_attempts;
    uint64_t reconnect_ok;
    int record_injected; /* 1 = WRITE_REPLACE audio expected in record_session path */
} rtw_tech_t;

typedef struct {
    char stream_sid[RTW_SID_MAX];
    char call_sid[RTW_SID_MAX];
    int ws_ready;
    int reconnect_enabled;
    uint64_t reconnect_ok;
    uint64_t uplink_frames;
    uint64_t downlink_frames;
    uint64_t clear_events;
    uint64_t clear_latency_last_us;
    uint64_t clear_latency_max_us;
    uint64_t clear_latency_avg_us;
    uint64_t clear_latency_samples;
    size_t outbound_queue;
    size_t playout_bytes;
    int record_injected;
    int inject_mode;
} rtw_bridge_stats_t;

/* Bridge API used by module + harness (no proprietary audio_stream code). */
switch_status_t rtw_bridge_start(switch_core_session_t *session, const char *ws_uri, int sampling,
                                 int channels, const char *metadata, rtw_tech_t **out_tech);
switch_status_t rtw_bridge_stop(switch_core_session_t *session, rtw_tech_t *tech, int channel_closing);
switch_status_t rtw_bridge_pause(rtw_tech_t *tech, int pause);
switch_status_t rtw_bridge_clear(rtw_tech_t *tech);
switch_status_t rtw_bridge_send_mark(rtw_tech_t *tech, const char *name);
switch_status_t rtw_bridge_get_stats(rtw_tech_t *tech, rtw_bridge_stats_t *out);

/* Media-bug READ path: feed L16 PCM (mono interleaved). */
switch_bool_t rtw_bridge_on_read_pcm16(rtw_tech_t *tech, const int16_t *pcm, size_t nsamples);

/* Media-bug WRITE path: fill buffer with L16 for inject; returns samples written. */
size_t rtw_bridge_on_write_pcm16(rtw_tech_t *tech, int16_t *out, size_t max_samples);

/*
 * WRITE_REPLACE helper: mutate inout_pcm in place.
 * Returns samples taken from playout (0 = passthrough left unchanged for REPLACE mode).
 */
size_t rtw_bridge_apply_write_frame(rtw_tech_t *tech, int16_t *inout_pcm, size_t nsamples);

int rtw_validate_ws_uri(const char *url, char *out, size_t out_cap);

/* Module entry points used by harness / future ESL wrappers */
switch_status_t rtw_mod_start_capture(switch_core_session_t *session, switch_media_bug_flag_t flags,
                                      char *ws_uri, int sampling, char *metadata);
switch_status_t rtw_mod_stop_capture(switch_core_session_t *session);
rtw_tech_t *rtw_mod_get_tech(switch_core_session_t *session);

#endif
