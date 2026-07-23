/*
 * Copyright (c) 2026 mod_realtime_ws contributors
 * SPDX-License-Identifier: MIT
 *
 * FreeSWITCH loadable module — API + media-bug wiring patterned after
 * amigniter/mod_audio_stream (public community layout), with Twilio L0
 * protocol via rtw_bridge / rtw_session (MIT, open duplex + mark/clear).
 *
 * Build:
 *   - Without FreeSWITCH: default stub headers (make mod-stub / harness)
 *   - With FreeSWITCH: -DHAVE_FREESWITCH $(pkg-config --cflags --libs freeswitch)
 */
#include "mod_realtime_ws.h"

SWITCH_MODULE_LOAD_FUNCTION(mod_realtime_ws_load);
SWITCH_MODULE_SHUTDOWN_FUNCTION(mod_realtime_ws_shutdown);
SWITCH_MODULE_DEFINITION(mod_realtime_ws, mod_realtime_ws_load, mod_realtime_ws_shutdown, NULL);

static switch_bool_t capture_callback(switch_media_bug_t *bug, void *user_data, switch_abc_type_t type)
{
#ifdef HAVE_FREESWITCH
    switch_core_session_t *session = switch_core_media_bug_get_session(bug);
    rtw_tech_t *tech = (rtw_tech_t *)user_data;
    switch_frame_t frame = {0};
    uint8_t data[SWITCH_RECOMMENDED_BUFFER_SIZE];
    int channel_closing;

    switch (type) {
    case SWITCH_ABC_TYPE_INIT:
        break;
    case SWITCH_ABC_TYPE_CLOSE:
        /* Sole owner of teardown when bug is removed / channel hangs up. */
        channel_closing = tech && tech->close_requested ? 0 : 1;
        if (tech) {
            switch_channel_t *channel = switch_core_session_get_channel(session);
            if (channel) {
                switch_channel_set_private(channel, RTW_BUG_NAME, NULL);
            }
            rtw_bridge_stop(session, tech, channel_closing);
        }
        break;
    case SWITCH_ABC_TYPE_READ:
        if (!tech || tech->close_requested || tech->cleanup_started) {
            return SWITCH_FALSE;
        }
        frame.data = data;
        frame.buflen = sizeof(data);
        while (switch_core_media_bug_read(bug, &frame, SWITCH_TRUE) == SWITCH_STATUS_SUCCESS) {
            if (frame.datalen >= sizeof(int16_t)) {
                rtw_bridge_on_read_pcm16(tech, (const int16_t *)frame.data,
                                         frame.datalen / sizeof(int16_t));
            }
        }
        break;
    case SWITCH_ABC_TYPE_WRITE:
    case SWITCH_ABC_TYPE_WRITE_REPLACE:
        if (tech && !tech->close_requested && !tech->cleanup_started) {
            int16_t out[320];
            size_t n = rtw_bridge_on_write_pcm16(tech, out, 160);
            (void)n;
            /* TODO(HAVE_FREESWITCH): publish `out` into write-replace frame. */
        }
        break;
    default:
        break;
    }
    return SWITCH_TRUE;
#else
    (void)bug;
    (void)user_data;
    (void)type;
    return SWITCH_TRUE;
#endif
}

static switch_status_t start_capture(switch_core_session_t *session, switch_media_bug_flag_t flags,
                                     char *ws_uri, int sampling, char *metadata)
{
    switch_channel_t *channel = switch_core_session_get_channel(session);
    rtw_tech_t *tech = NULL;
    int channels = (flags & SMBF_STEREO) ? 2 : 1;

    if (!channel) {
        return SWITCH_STATUS_FALSE;
    }
    if (switch_channel_get_private(channel, RTW_BUG_NAME)) {
        switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR,
                          "mod_realtime_ws: bug already attached\n");
        return SWITCH_STATUS_FALSE;
    }
    if (switch_channel_pre_answer(channel) != SWITCH_STATUS_SUCCESS) {
        switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR,
                          "mod_realtime_ws: channel must reach pre-answer before start\n");
        return SWITCH_STATUS_FALSE;
    }
    if (rtw_bridge_start(session, ws_uri, sampling, channels, metadata, &tech) != SWITCH_STATUS_SUCCESS) {
        return SWITCH_STATUS_FALSE;
    }

#ifdef HAVE_FREESWITCH
    {
        switch_media_bug_t *bug = NULL;
        switch_status_t st;
        st = switch_core_media_bug_add(session, RTW_BUG_NAME, NULL, capture_callback, tech, 0, flags, &bug);
        if (st != SWITCH_STATUS_SUCCESS) {
            rtw_bridge_stop(session, tech, 0);
            return st;
        }
        switch_channel_set_private(channel, RTW_BUG_NAME, bug);
    }
#else
    switch_channel_set_private(channel, RTW_BUG_NAME, tech);
    (void)capture_callback;
    (void)flags;
#endif
    return SWITCH_STATUS_SUCCESS;
}

static switch_status_t do_stop(switch_core_session_t *session)
{
    switch_channel_t *channel = switch_core_session_get_channel(session);
    void *priv;
    if (!channel) {
        return SWITCH_STATUS_FALSE;
    }
    priv = switch_channel_get_private(channel, RTW_BUG_NAME);
    if (!priv) {
        return SWITCH_STATUS_FALSE;
    }
#ifdef HAVE_FREESWITCH
    {
        switch_media_bug_t *bug = (switch_media_bug_t *)priv;
        rtw_tech_t *tech = (rtw_tech_t *)switch_core_media_bug_get_user_data(bug);
        if (tech) {
            tech->close_requested = 1;
        }
        switch_channel_set_private(channel, RTW_BUG_NAME, NULL);
        /* remove → CLOSE callback owns rtw_bridge_stop (idempotent). */
        switch_core_media_bug_remove(session, &bug);
    }
#else
    {
        rtw_tech_t *tech = (rtw_tech_t *)priv;
        switch_channel_set_private(channel, RTW_BUG_NAME, NULL);
        rtw_bridge_stop(session, tech, 0);
    }
#endif
    return SWITCH_STATUS_SUCCESS;
}

/* Extract optional metadata JSON object starting at first '{', so spaces inside JSON work. */
static char *extract_metadata_json(char *cmd)
{
    char *brace;
    if (!cmd) {
        return NULL;
    }
    brace = strchr(cmd, '{');
    return brace;
}

#define RTW_API_SYNTAX \
    "<uuid> [start|stop|pause|resume|clear|send_mark] [ws-url] [mono|mixed|stereo] [8k|16k] [{metadata-json}]"

SWITCH_STANDARD_API(uuid_realtime_ws_function)
{
    char *mycmd = NULL;
    char *argv[6] = {0};
    int argc = 0;
    switch_status_t status = SWITCH_STATUS_FALSE;
    char *metadata = NULL;

    if (zstr(cmd)) {
        stream->write_function(stream, "-USAGE: %s\n", RTW_API_SYNTAX);
        return SWITCH_STATUS_SUCCESS;
    }
    mycmd = strdup(cmd);
    if (!mycmd) {
        stream->write_function(stream, "-ERR oom\n");
        return SWITCH_STATUS_SUCCESS;
    }
    metadata = extract_metadata_json(mycmd);
    if (metadata && metadata > mycmd && *(metadata - 1) == ' ') {
        /* Cut token stream at whitespace before JSON so spaces inside JSON are preserved. */
        char *cut = metadata;
        while (cut > mycmd && *(cut - 1) == ' ') {
            cut--;
        }
        *cut = '\0';
    }
    argc = switch_separate_string(mycmd, ' ', argv, (int)(sizeof(argv) / sizeof(argv[0])));
    if (argc < 2) {
        stream->write_function(stream, "-USAGE: %s\n", RTW_API_SYNTAX);
        goto done;
    }

#ifdef HAVE_FREESWITCH
    {
        switch_core_session_t *lsession = switch_core_session_locate(argv[0]);
        if (!lsession) {
            stream->write_function(stream, "-ERR cannot locate session\n");
            goto done;
        }
        if (!strcasecmp(argv[1], "stop")) {
            status = do_stop(lsession);
        } else if (!strcasecmp(argv[1], "pause") || !strcasecmp(argv[1], "resume")) {
            switch_channel_t *ch = switch_core_session_get_channel(lsession);
            switch_media_bug_t *bug = switch_channel_get_private(ch, RTW_BUG_NAME);
            rtw_tech_t *tech = bug ? (rtw_tech_t *)switch_core_media_bug_get_user_data(bug) : NULL;
            status = tech ? rtw_bridge_pause(tech, !strcasecmp(argv[1], "pause")) : SWITCH_STATUS_FALSE;
        } else if (!strcasecmp(argv[1], "clear")) {
            switch_channel_t *ch = switch_core_session_get_channel(lsession);
            switch_media_bug_t *bug = switch_channel_get_private(ch, RTW_BUG_NAME);
            rtw_tech_t *tech = bug ? (rtw_tech_t *)switch_core_media_bug_get_user_data(bug) : NULL;
            status = tech ? rtw_bridge_clear(tech) : SWITCH_STATUS_FALSE;
        } else if (!strcasecmp(argv[1], "send_mark")) {
            switch_channel_t *ch = switch_core_session_get_channel(lsession);
            switch_media_bug_t *bug = switch_channel_get_private(ch, RTW_BUG_NAME);
            rtw_tech_t *tech = bug ? (rtw_tech_t *)switch_core_media_bug_get_user_data(bug) : NULL;
            status = (tech && argc > 2) ? rtw_bridge_send_mark(tech, argv[2]) : SWITCH_STATUS_FALSE;
        } else if (!strcasecmp(argv[1], "start")) {
            char wsUri[RTW_MAX_WS_URI];
            int sampling = 8000;
            switch_media_bug_flag_t flags = SMBF_READ_STREAM | SMBF_WRITE_REPLACE;
            if (argc < 4) {
                stream->write_function(stream, "-USAGE: %s\n", RTW_API_SYNTAX);
                switch_core_session_rwunlock(lsession);
                goto done;
            }
            if (!strcmp(argv[3], "mixed")) {
                flags |= SMBF_WRITE_STREAM;
            } else if (!strcmp(argv[3], "stereo")) {
                flags |= SMBF_WRITE_STREAM | SMBF_STEREO;
            } else if (strcmp(argv[3], "mono") != 0) {
                stream->write_function(stream, "-ERR invalid mix type\n");
                switch_core_session_rwunlock(lsession);
                goto done;
            }
            if (argc > 4) {
                if (!strcmp(argv[4], "16k") || !strcmp(argv[4], "16000")) {
                    sampling = 16000;
                } else if (!strcmp(argv[4], "8k") || !strcmp(argv[4], "8000")) {
                    sampling = 8000;
                } else {
                    sampling = atoi(argv[4]);
                }
            }
            if (sampling % 8000 != 0) {
                stream->write_function(stream, "-ERR invalid sample rate\n");
            } else if (!rtw_validate_ws_uri(argv[2], wsUri, sizeof(wsUri))) {
                stream->write_function(stream, "-ERR invalid ws uri (ws:// only until TLS lands)\n");
            } else {
                status = start_capture(lsession, flags, wsUri, sampling, metadata);
            }
        } else {
            stream->write_function(stream, "-ERR unknown subcommand\n");
        }
        switch_core_session_rwunlock(lsession);
    }
#else
    (void)session;
    if (!strcasecmp(argv[1], "start") && argc >= 4) {
        char wsUri[RTW_MAX_WS_URI];
        if (!rtw_validate_ws_uri(argv[2], wsUri, sizeof(wsUri))) {
            stream->write_function(stream, "-ERR invalid ws uri (ws:// only until TLS lands)\n");
            status = SWITCH_STATUS_FALSE;
        } else {
            status = SWITCH_STATUS_SUCCESS;
            stream->write_function(stream, "+OK stub parse ok\n");
        }
    } else {
        stream->write_function(stream, "+OK stub\n");
        status = SWITCH_STATUS_SUCCESS;
    }
#endif

    if (status == SWITCH_STATUS_SUCCESS) {
        stream->write_function(stream, "+OK Success\n");
    } else {
        stream->write_function(stream, "-ERR Operation Failed\n");
    }

done:
    switch_safe_free(mycmd);
    return SWITCH_STATUS_SUCCESS;
}

switch_status_t rtw_mod_start_capture(switch_core_session_t *session, switch_media_bug_flag_t flags,
                                      char *ws_uri, int sampling, char *metadata)
{
    return start_capture(session, flags, ws_uri, sampling, metadata);
}

switch_status_t rtw_mod_stop_capture(switch_core_session_t *session)
{
    return do_stop(session);
}

SWITCH_MODULE_LOAD_FUNCTION(mod_realtime_ws_load)
{
    switch_api_interface_t *api_interface = NULL;
    *module_interface = switch_loadable_module_create_module_interface(pool, modname);
    switch_event_reserve_subclass(RTW_EVENT_CONNECT);
    switch_event_reserve_subclass(RTW_EVENT_DISCONNECT);
    switch_event_reserve_subclass(RTW_EVENT_ERROR);
    switch_event_reserve_subclass(RTW_EVENT_JSON);
    SWITCH_ADD_API(api_interface, "uuid_realtime_ws", "Realtime WS Twilio-compatible media bridge",
                   uuid_realtime_ws_function, RTW_API_SYNTAX);
    switch_console_set_complete("add uuid_realtime_ws ::console::list_uuid start");
    switch_console_set_complete("add uuid_realtime_ws ::console::list_uuid stop");
    switch_console_set_complete("add uuid_realtime_ws ::console::list_uuid clear");
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "mod_realtime_ws loaded\n");
    return SWITCH_STATUS_SUCCESS;
}

SWITCH_MODULE_SHUTDOWN_FUNCTION(mod_realtime_ws_shutdown)
{
    switch_event_free_subclass(RTW_EVENT_CONNECT);
    switch_event_free_subclass(RTW_EVENT_DISCONNECT);
    switch_event_free_subclass(RTW_EVENT_ERROR);
    switch_event_free_subclass(RTW_EVENT_JSON);
    return SWITCH_STATUS_SUCCESS;
}
