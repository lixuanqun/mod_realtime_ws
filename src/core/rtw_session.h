#ifndef RTW_SESSION_H
#define RTW_SESSION_H

#include "rtw_playout.h"
#include "rtw_protocol.h"
#include "rtw_queue.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RTW_FRAME_MULAW_BYTES 160 /* 20ms @ 8kHz */

typedef enum {
    RTW_STATE_IDLE = 0,
    RTW_STATE_STARTING,
    RTW_STATE_RUNNING,
    RTW_STATE_STOPPING,
    RTW_STATE_STOPPED
} rtw_session_state_t;

typedef struct {
    char stream_sid[RTW_SID_MAX];
    char call_sid[RTW_SID_MAX];
    char account_sid[RTW_SID_MAX];
    rtw_session_state_t state;
    uint32_t seq;
    uint32_t chunk;
    uint32_t timestamp_ms;
    rtw_bounded_queue_t outbound; /* JSON text frames to send on WS */
    rtw_playout_t playout;
    int paused;
    uint64_t uplink_frames;
    uint64_t downlink_frames;
    uint64_t clear_events;
} rtw_session_t;

int rtw_session_init(rtw_session_t *s, size_t out_queue_cap, size_t playout_cap);
void rtw_session_destroy(rtw_session_t *s);

int rtw_session_start(rtw_session_t *s, const char *stream_sid, const char *call_sid,
                      const char *account_sid, const char *custom_params_json);

/* Feed PCM16LE @ 8kHz mono. Packs 20ms frames into Twilio media JSON on outbound queue. */
int rtw_session_push_pcm16(rtw_session_t *s, const int16_t *pcm, size_t nsamples);

/* Handle inbound WS text JSON from peer. May enqueue mark ACK JSON. */
int rtw_session_handle_peer_json(rtw_session_t *s, const char *json);

/* Pop next outbound JSON frame (malloc'd). Caller frees. Returns 0 ok, -1 empty. */
int rtw_session_pop_outbound(rtw_session_t *s, char **json_out);

/* Read mulaw from playout for injection into call (simulates media bug write). */
size_t rtw_session_read_playout(rtw_session_t *s, uint8_t *out, size_t max_len);

int rtw_session_stop(rtw_session_t *s);

#ifdef __cplusplus
}
#endif

#endif
