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
    fprintf(stderr, "Usage: %s --url ws[s]://host:port/path [--seconds N]\n", argv0);
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
    size_t replace_hits = 0;
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
    {
        char tmp[64];
#ifdef RTW_HAS_OPENSSL
        if (!rtw_validate_ws_uri("wss://example/media", tmp, sizeof(tmp))) {
            fprintf(stderr, "harness: wss:// should be accepted with OpenSSL\n");
            return 1;
        }
#else
        if (rtw_validate_ws_uri("wss://example/media", tmp, sizeof(tmp))) {
            fprintf(stderr, "harness: wss:// should be rejected without OpenSSL\n");
            return 1;
        }
#endif
        if (!rtw_validate_ws_uri("ws://127.0.0.1:9/x", tmp, sizeof(tmp))) {
            fprintf(stderr, "harness: ws:// should be accepted\n");
            return 1;
        }
    }
    if (seconds < 1) {
        seconds = 1;
    }

    /* Avoid reconnect noise in short smoke runs */
    setenv("RTW_RECONNECT", "0", 1);

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

    frames = seconds * 50;
    for (i = 0; i < frames; i++) {
        int16_t pcm[160];
        int16_t frame[160];
        size_t n;
        size_t s;
        for (s = 0; s < 160; s++) {
            double t = (double)(i * 160 + (int)s) / 8000.0;
            pcm[s] = (int16_t)(8000.0 * sin(2.0 * 3.141592653589793 * 440.0 * t));
            frame[s] = 1000; /* stand-in for other-leg audio */
        }
        rtw_bridge_on_read_pcm16(tech, pcm, 160);
        usleep(20 * 1000);
        n = rtw_bridge_apply_write_frame(tech, frame, 160);
        got_samples += n;
        if (n > 0) {
            replace_hits++;
            if (frame[0] == 1000) {
                fprintf(stderr, "harness: WRITE_REPLACE did not mutate frame\n");
                goto out;
            }
        } else if (frame[0] != 1000) {
            fprintf(stderr, "harness: passthrough mutated empty frame\n");
            goto out;
        }
    }

    if (got_samples == 0) {
        fprintf(stderr, "harness: no downlink samples (echo mock may be slow); trying clear path\n");
    } else {
        fprintf(stderr, "harness: downlink samples=%zu replace_hits=%zu\n", got_samples, replace_hits);
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
        int16_t frame[160];
        size_t s, n;
        rtw_bridge_stats_t st;
        for (s = 0; s < 160; s++) {
            frame[s] = 42;
        }
        n = rtw_bridge_apply_write_frame(tech, frame, 160);
        if (n != 0 || frame[0] != 42) {
            fprintf(stderr, "harness: expected passthrough after clear\n");
            goto out;
        }
        if (rtw_bridge_get_stats(tech, &st) != SWITCH_STATUS_SUCCESS) {
            fprintf(stderr, "harness: stats failed\n");
            goto out;
        }
        if (st.clear_events < 1) {
            fprintf(stderr, "harness: expected clear_events>=1\n");
            goto out;
        }
        if (st.clear_latency_samples < 1) {
            fprintf(stderr, "harness: clear latency not sampled\n");
            goto out;
        }
        fprintf(stderr, "harness: clear_last_us=%llu clear_max_us=%llu samples=%llu\n",
                (unsigned long long)st.clear_latency_last_us,
                (unsigned long long)st.clear_latency_max_us,
                (unsigned long long)st.clear_latency_samples);
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
