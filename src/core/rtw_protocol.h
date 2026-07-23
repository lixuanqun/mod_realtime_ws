#ifndef RTW_PROTOCOL_H
#define RTW_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RTW_SID_MAX 64
#define RTW_MARK_NAME_MAX 128

typedef enum {
    RTW_EVT_UNKNOWN = 0,
    RTW_EVT_CONNECTED,
    RTW_EVT_START,
    RTW_EVT_MEDIA,
    RTW_EVT_DTMF,
    RTW_EVT_MARK,
    RTW_EVT_CLEAR,
    RTW_EVT_STOP
} rtw_event_type_t;

typedef struct {
    rtw_event_type_t type;
    char stream_sid[RTW_SID_MAX];
    char mark_name[RTW_MARK_NAME_MAX];
    char track[32];
    char digit[8];
    /* Decoded media payload (mulaw bytes). Owned by caller buffer passed to parse. */
    uint8_t *payload;
    size_t payload_len;
    uint32_t sequence_number;
    uint32_t chunk;
    uint32_t timestamp_ms;
} rtw_inbound_event_t;

/* Builders return malloc'd NUL-terminated JSON; caller frees with free(). NULL on error. */
char *rtw_build_connected(const char *version);
char *rtw_build_start(const char *stream_sid, const char *account_sid, const char *call_sid,
                      const char *custom_params_json_object_or_null, uint32_t seq);
char *rtw_build_media(const char *stream_sid, const char *track, uint32_t chunk,
                      uint32_t timestamp_ms, uint32_t seq,
                      const uint8_t *mulaw, size_t mulaw_len);
char *rtw_build_mark(const char *stream_sid, const char *name, uint32_t seq);
char *rtw_build_stop(const char *stream_sid, const char *account_sid, const char *call_sid,
                     uint32_t seq);

/*
 * Parse inbound JSON from peer.
 * If event is media, decoded mulaw is written into payload_buf (capacity payload_cap).
 * payload pointer in out references payload_buf.
 */
int rtw_parse_inbound(const char *json, rtw_inbound_event_t *out,
                      uint8_t *payload_buf, size_t payload_cap);

#ifdef __cplusplus
}
#endif

#endif
