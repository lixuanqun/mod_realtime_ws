/*
 * rtw_sim — FreeSWITCH-less session simulator for integration/stress tests.
 * Mimics mod_realtime_ws L0 producer behavior over WebSocket.
 *
 * Usage:
 *   rtw_sim --url ws://127.0.0.1:8081/media --seconds 2 [--clear-test]
 *   rtw_sim --url ws://127.0.0.1:8081/media --stress-id N --seconds 5
 */
#include "rtw_session.h"
#include "rtw_ws_client.h"
#include "rtw_mulaw.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

typedef struct {
    rtw_session_t *session;
    rtw_ws_client_t *ws;
    pthread_mutex_t lock;
    int running;
    int got_clear_ack;
    int peer_media_frames;
    int clear_test;
} sim_ctx_t;

static void on_text(void *userdata, const char *text, size_t len)
{
    sim_ctx_t *ctx = (sim_ctx_t *)userdata;
    char *copy;
    (void)len;
    pthread_mutex_lock(&ctx->lock);
    if (rtw_session_handle_peer_json(ctx->session, text) == 0) {
        /* detect mark ack name cleartest */
        if (strstr(text, "\"event\":\"media\"")) {
            ctx->peer_media_frames++;
        }
    }
    /* Drain outbound mark ACKs immediately */
    while (rtw_session_pop_outbound(ctx->session, &copy) == 0) {
        if (strstr(copy, "\"event\":\"mark\"") && strstr(copy, "cleartest")) {
            ctx->got_clear_ack = 1;
        }
        rtw_ws_send_text(ctx->ws, copy, strlen(copy));
        free(copy);
    }
    pthread_mutex_unlock(&ctx->lock);
}

static void on_close(void *userdata, int code)
{
    sim_ctx_t *ctx = (sim_ctx_t *)userdata;
    (void)code;
    ctx->running = 0;
}

static void usage(const char *argv0)
{
    fprintf(stderr,
            "Usage: %s --url ws://host:port/path [--seconds N] [--clear-test] [--stream-sid SID]\n",
            argv0);
}

int main(int argc, char **argv)
{
    const char *url = NULL;
    const char *sid = "MZ000000000000000000000000000001";
    int seconds = 2;
    int clear_test = 0;
    int i;
    sim_ctx_t ctx;
    rtw_session_t session;
    char *json;
    time_t end;
    int16_t pcm[160];
    int rc = 0;

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--url") && i + 1 < argc) {
            url = argv[++i];
        } else if (!strcmp(argv[i], "--seconds") && i + 1 < argc) {
            seconds = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--clear-test")) {
            clear_test = 1;
        } else if (!strcmp(argv[i], "--stream-sid") && i + 1 < argc) {
            sid = argv[++i];
        } else {
            usage(argv[0]);
            return 2;
        }
    }
    if (!url) {
        usage(argv[0]);
        return 2;
    }

    memset(&ctx, 0, sizeof(ctx));
    pthread_mutex_init(&ctx.lock, NULL);
    ctx.session = &session;
    ctx.clear_test = clear_test;
    ctx.running = 1;

    if (rtw_session_init(&session, 256, 64 * 1024) != 0) {
        fprintf(stderr, "session init failed\n");
        return 1;
    }
    ctx.ws = rtw_ws_connect(url, on_text, on_close, &ctx);
    if (!ctx.ws) {
        fprintf(stderr, "ws connect failed: %s\n", url);
        rtw_session_destroy(&session);
        return 1;
    }

    pthread_mutex_lock(&ctx.lock);
    if (rtw_session_start(&session, sid, "CAsim", "ACsim", "{\"source\":\"rtw_sim\"}") != 0) {
        pthread_mutex_unlock(&ctx.lock);
        fprintf(stderr, "session start failed\n");
        rc = 1;
        goto done;
    }
    while (rtw_session_pop_outbound(&session, &json) == 0) {
        if (rtw_ws_send_text(ctx.ws, json, strlen(json)) != 0) {
            free(json);
            pthread_mutex_unlock(&ctx.lock);
            rc = 1;
            goto done;
        }
        free(json);
    }
    pthread_mutex_unlock(&ctx.lock);

    end = time(NULL) + seconds;
    while (ctx.running && time(NULL) < end) {
        for (i = 0; i < 160; i++) {
            pcm[i] = (int16_t)((i * 200) % 5000);
        }
        pthread_mutex_lock(&ctx.lock);
        rtw_session_push_pcm16(&session, pcm, 160);
        while (rtw_session_pop_outbound(&session, &json) == 0) {
            rtw_ws_send_text(ctx.ws, json, strlen(json));
            free(json);
        }
        /* consume playout as if injecting into call */
        {
            uint8_t tmp[320];
            rtw_session_read_playout(&session, tmp, sizeof(tmp));
        }
        pthread_mutex_unlock(&ctx.lock);
        if (rtw_ws_poll(ctx.ws, 5) < 0) {
            break;
        }
        usleep(20 * 1000);
    }

    if (clear_test && !ctx.got_clear_ack) {
        fprintf(stderr, "clear-test FAILED: no mark ack for cleartest\n");
        rc = 1;
    } else if (clear_test) {
        fprintf(stderr, "clear-test OK (peer_media_frames=%d)\n", ctx.peer_media_frames);
    } else {
        fprintf(stderr, "sim OK uplink_frames=%llu clear_events=%llu\n",
                (unsigned long long)session.uplink_frames,
                (unsigned long long)session.clear_events);
    }

    pthread_mutex_lock(&ctx.lock);
    rtw_session_stop(&session);
    while (rtw_session_pop_outbound(&session, &json) == 0) {
        rtw_ws_send_text(ctx.ws, json, strlen(json));
        free(json);
    }
    pthread_mutex_unlock(&ctx.lock);

done:
    rtw_ws_close(ctx.ws);
    rtw_session_destroy(&session);
    pthread_mutex_destroy(&ctx.lock);
    return rc;
}
