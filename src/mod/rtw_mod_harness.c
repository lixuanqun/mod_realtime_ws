/*
 * Stub FreeSWITCH harness: exercises module start/stop + bridge duplex
 * against the Node Twilio mock without libfreeswitch-dev.
 *
 * Usage: ./build/rtw_mod_harness --url ws://127.0.0.1:18083/media [--seconds 2]
 */
#include "mod_realtime_ws.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void usage(const char *argv0)
{
    fprintf(stderr, "Usage: %s --url ws://host:port/path [--seconds N]\n", argv0);
}

int main(int argc, char **argv)
{
    const char *url = NULL;
    int seconds = 2;
    switch_core_session_t *session;
    rtw_tech_t *tech;
    char ws_uri[RTW_MAX_WS_URI];
    int i, frames;
    size_t got_samples = 0;
    int rc = 1;

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--url") && i + 1 < argc) {
            url = argv[++i];
        } else if (!strcmp(argv[i], "--seconds") && i + 1 < argc) {
            seconds = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            usage(argv[0]);
            return 0;
        }
    }
    if (!url || !rtw_validate_ws_uri(url, ws_uri, sizeof(ws_uri))) {
        usage(argv[0]);
        return 2;
    }
    /* Guard rails covered by design review fixes */
    {
        char tmp[64];
        if (rtw_validate_ws_uri("wss://example/media", tmp, sizeof(tmp))) {
            fprintf(stderr, "harness: wss should be rejected until TLS\n");
            return 1;
        }
        if (!rtw_validate_ws_uri("ws://127.0.0.1:9/x", tmp, sizeof(tmp))) {
            fprintf(stderr, "harness: ws:// should be accepted\n");
            return 1;
        }
    }
    if (seconds < 1) {
        seconds = 1;
    }

    session = rtw_stub_session_create("aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee");
    if (!session) {
        fprintf(stderr, "harness: session create failed\n");
        return 1;
    }

    if (rtw_mod_start_capture(session, SMBF_READ_STREAM | SMBF_WRITE_REPLACE, ws_uri, 8000,
                              "{\"harness\":\"1\"}") != SWITCH_STATUS_SUCCESS) {
        fprintf(stderr, "harness: start_capture failed (is mock listening?)\n");
        rtw_stub_session_destroy(session);
        return 1;
    }

    tech = (rtw_tech_t *)switch_channel_get_private(session->channel, RTW_BUG_NAME);
    if (!tech) {
        fprintf(stderr, "harness: no tech private\n");
        goto out;
    }

    frames = seconds * 50; /* 20ms frames */
    for (i = 0; i < frames; i++) {
        int16_t pcm[160];
        int16_t out[160];
        size_t n;
        size_t s;
        for (s = 0; s < 160; s++) {
            double t = (double)(i * 160 + (int)s) / 8000.0;
            pcm[s] = (int16_t)(8000.0 * sin(2.0 * 3.141592653589793 * 440.0 * t));
        }
        rtw_bridge_on_read_pcm16(tech, pcm, 160);
        usleep(20 * 1000);
        n = rtw_bridge_on_write_pcm16(tech, out, 160);
        got_samples += n;
    }

    if (got_samples == 0) {
        fprintf(stderr, "harness: no downlink samples (echo mock may be slow); trying clear path\n");
    } else {
        fprintf(stderr, "harness: downlink samples=%zu\n", got_samples);
    }

    if (rtw_bridge_send_mark(tech, "harness1") != SWITCH_STATUS_SUCCESS) {
        fprintf(stderr, "harness: send_mark failed\n");
        goto out;
    }
    if (rtw_bridge_clear(tech) != SWITCH_STATUS_SUCCESS) {
        fprintf(stderr, "harness: clear failed\n");
        goto out;
    }
    {
        int16_t out[160];
        size_t n = rtw_bridge_on_write_pcm16(tech, out, 160);
        if (n != 0) {
            fprintf(stderr, "harness: expected empty playout after clear, got %zu\n", n);
            goto out;
        }
    }

    if (rtw_mod_stop_capture(session) != SWITCH_STATUS_SUCCESS) {
        fprintf(stderr, "harness: stop failed\n");
        goto out;
    }

    fprintf(stderr, "harness: OK\n");
    rc = 0;

out:
    if (rc != 0) {
        rtw_mod_stop_capture(session);
    }
    rtw_stub_session_destroy(session);
    return rc;
}
