/*
 * mod_realtime_ws — FreeSWITCH loadable module (skeleton).
 *
 * Build with -DHAVE_FREESWITCH and FreeSWITCH headers to produce mod_realtime_ws.so.
 * Without FreeSWITCH, this file still documents the API surface and can be
 * compile-checked against src/mod/fs_stub/switch.h (see make mod-stub).
 *
 * Wire protocol and media logic live in src/core (rtw_*); this file only
 * adapts FreeSWITCH media bugs / APIs to rtw_session_t.
 */
#include "rtw_session.h"

#ifdef HAVE_FREESWITCH
#include <switch.h>
#else
#include "fs_stub/switch.h"
#endif

SWITCH_MODULE_LOAD_FUNCTION(mod_realtime_ws_load);
SWITCH_MODULE_SHUTDOWN_FUNCTION(mod_realtime_ws_shutdown);
SWITCH_MODULE_DEFINITION(mod_realtime_ws, mod_realtime_ws_load, mod_realtime_ws_shutdown, NULL);

#ifndef HAVE_FREESWITCH
/* Ensure load/shutdown symbols exist for stub link smoke. */
#endif

/*
 * Draft API:
 *   uuid_realtime_ws <uuid> start <wss-url> <mix-type> <rate> [metadata-json]
 *   uuid_realtime_ws <uuid> stop
 *   uuid_realtime_ws <uuid> clear
 *   uuid_realtime_ws <uuid> send_mark <name>
 *
 * Full media-bug + WS worker wiring lands when linked against real FreeSWITCH.
 * Until then, core behavior is validated via rtw_sim + unit/smoke/stress tests.
 */

SWITCH_STANDARD_API(uuid_realtime_ws_function)
{
    char *cmd_dup;
    char *argv[8] = {0};
    int argc;
    if (zstr(cmd)) {
        stream->write_function(stream, "-ERR usage: uuid_realtime_ws <uuid> <start|stop|clear|send_mark> ...\n");
        return SWITCH_STATUS_SUCCESS;
    }
    cmd_dup = strdup(cmd);
    if (!cmd_dup) {
        stream->write_function(stream, "-ERR oom\n");
        return SWITCH_STATUS_SUCCESS;
    }
    argc = switch_separate_string(cmd_dup, ' ', argv, (int)(sizeof(argv) / sizeof(argv[0])));
    if (argc < 2) {
        stream->write_function(stream, "-ERR missing args\n");
        free(cmd_dup);
        return SWITCH_STATUS_SUCCESS;
    }
    /* Skeleton: acknowledge commands without channel attach when stubbed. */
    if (!strcasecmp(argv[1], "start")) {
        stream->write_function(stream, "+OK start queued (skeleton; use rtw_sim until FS build)\n");
    } else if (!strcasecmp(argv[1], "stop")) {
        stream->write_function(stream, "+OK stop\n");
    } else if (!strcasecmp(argv[1], "clear")) {
        stream->write_function(stream, "+OK clear\n");
    } else if (!strcasecmp(argv[1], "send_mark")) {
        stream->write_function(stream, "+OK mark\n");
    } else {
        stream->write_function(stream, "-ERR unknown subcommand\n");
    }
    free(cmd_dup);
    return SWITCH_STATUS_SUCCESS;
}

SWITCH_MODULE_LOAD_FUNCTION(mod_realtime_ws_load)
{
    switch_api_interface_t *api_interface = NULL;
    *module_interface = switch_loadable_module_create_module_interface(pool, modname);
    SWITCH_ADD_API(api_interface, "uuid_realtime_ws", "Realtime WS media bridge",
                   uuid_realtime_ws_function, "<uuid> <start|stop|clear|send_mark> [args]");
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE,
                      "mod_realtime_ws loaded (core validated via rtw_sim; FS media-bug pending)\n");
    return SWITCH_STATUS_SUCCESS;
}

SWITCH_MODULE_SHUTDOWN_FUNCTION(mod_realtime_ws_shutdown)
{
    return SWITCH_STATUS_SUCCESS;
}
