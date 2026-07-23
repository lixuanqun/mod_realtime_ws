#include "rtw_protocol.h"
#include "rtw_base64.h"

#include "../../third_party/cJSON/cJSON.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void copy_str(char *dst, size_t cap, const char *src)
{
    if (!dst || cap == 0) {
        return;
    }
    if (!src) {
        dst[0] = '\0';
        return;
    }
    snprintf(dst, cap, "%s", src);
}

char *rtw_build_connected(const char *version)
{
    cJSON *root = cJSON_CreateObject();
    cJSON *ext;
    char *out;
    if (!root) {
        return NULL;
    }
    cJSON_AddStringToObject(root, "event", "connected");
    cJSON_AddStringToObject(root, "protocol", "Call");
    cJSON_AddStringToObject(root, "version", version ? version : "1.0.0");
    ext = cJSON_AddObjectToObject(root, "mod_realtime_ws");
    if (ext) {
        cJSON_AddStringToObject(ext, "compat", "twilio-media-streams");
        cJSON_AddStringToObject(ext, "level", "L0");
    }
    out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return out;
}

char *rtw_build_start(const char *stream_sid, const char *account_sid, const char *call_sid,
                      const char *custom_params_json_object_or_null, uint32_t seq)
{
    cJSON *root = cJSON_CreateObject();
    cJSON *start;
    cJSON *fmt;
    cJSON *tracks;
    cJSON *custom;
    char seqbuf[32];
    char *out;
    if (!root || !stream_sid) {
        cJSON_Delete(root);
        return NULL;
    }
    snprintf(seqbuf, sizeof(seqbuf), "%u", seq);
    cJSON_AddStringToObject(root, "event", "start");
    cJSON_AddStringToObject(root, "sequenceNumber", seqbuf);
    cJSON_AddStringToObject(root, "streamSid", stream_sid);
    start = cJSON_AddObjectToObject(root, "start");
    cJSON_AddStringToObject(start, "streamSid", stream_sid);
    cJSON_AddStringToObject(start, "accountSid", account_sid ? account_sid : "AC00000000000000000000000000000000");
    cJSON_AddStringToObject(start, "callSid", call_sid ? call_sid : "CA00000000000000000000000000000000");
    tracks = cJSON_AddArrayToObject(start, "tracks");
    cJSON_AddItemToArray(tracks, cJSON_CreateString("inbound"));
    if (custom_params_json_object_or_null && custom_params_json_object_or_null[0]) {
        custom = cJSON_Parse(custom_params_json_object_or_null);
        if (custom && cJSON_IsObject(custom)) {
            cJSON_AddItemToObject(start, "customParameters", custom);
        } else {
            cJSON_Delete(custom);
            cJSON_AddObjectToObject(start, "customParameters");
        }
    } else {
        cJSON_AddObjectToObject(start, "customParameters");
    }
    fmt = cJSON_AddObjectToObject(start, "mediaFormat");
    cJSON_AddStringToObject(fmt, "encoding", "audio/x-mulaw");
    cJSON_AddNumberToObject(fmt, "sampleRate", 8000);
    cJSON_AddNumberToObject(fmt, "channels", 1);
    out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return out;
}

char *rtw_build_media(const char *stream_sid, const char *track, uint32_t chunk,
                      uint32_t timestamp_ms, uint32_t seq,
                      const uint8_t *mulaw, size_t mulaw_len)
{
    size_t b64cap = rtw_base64_encoded_len(mulaw_len);
    char *b64 = (char *)malloc(b64cap);
    cJSON *root;
    cJSON *media;
    char seqbuf[32], chunkbuf[32], tsbuf[32];
    char *out;
    if (!b64 || !stream_sid || !mulaw) {
        free(b64);
        return NULL;
    }
    if (rtw_base64_encode(mulaw, mulaw_len, b64, b64cap) == 0) {
        free(b64);
        return NULL;
    }
    root = cJSON_CreateObject();
    if (!root) {
        free(b64);
        return NULL;
    }
    snprintf(seqbuf, sizeof(seqbuf), "%u", seq);
    snprintf(chunkbuf, sizeof(chunkbuf), "%u", chunk);
    snprintf(tsbuf, sizeof(tsbuf), "%u", timestamp_ms);
    cJSON_AddStringToObject(root, "event", "media");
    cJSON_AddStringToObject(root, "sequenceNumber", seqbuf);
    cJSON_AddStringToObject(root, "streamSid", stream_sid);
    media = cJSON_AddObjectToObject(root, "media");
    cJSON_AddStringToObject(media, "track", track ? track : "inbound");
    cJSON_AddStringToObject(media, "chunk", chunkbuf);
    cJSON_AddStringToObject(media, "timestamp", tsbuf);
    cJSON_AddStringToObject(media, "payload", b64);
    free(b64);
    out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return out;
}

char *rtw_build_mark(const char *stream_sid, const char *name, uint32_t seq)
{
    cJSON *root = cJSON_CreateObject();
    cJSON *mark;
    char seqbuf[32];
    char *out;
    if (!root || !stream_sid || !name) {
        cJSON_Delete(root);
        return NULL;
    }
    snprintf(seqbuf, sizeof(seqbuf), "%u", seq);
    cJSON_AddStringToObject(root, "event", "mark");
    cJSON_AddStringToObject(root, "sequenceNumber", seqbuf);
    cJSON_AddStringToObject(root, "streamSid", stream_sid);
    mark = cJSON_AddObjectToObject(root, "mark");
    cJSON_AddStringToObject(mark, "name", name);
    out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return out;
}

char *rtw_build_stop(const char *stream_sid, const char *account_sid, const char *call_sid,
                     uint32_t seq)
{
    cJSON *root = cJSON_CreateObject();
    cJSON *stop;
    char seqbuf[32];
    char *out;
    if (!root || !stream_sid) {
        cJSON_Delete(root);
        return NULL;
    }
    snprintf(seqbuf, sizeof(seqbuf), "%u", seq);
    cJSON_AddStringToObject(root, "event", "stop");
    cJSON_AddStringToObject(root, "sequenceNumber", seqbuf);
    cJSON_AddStringToObject(root, "streamSid", stream_sid);
    stop = cJSON_AddObjectToObject(root, "stop");
    cJSON_AddStringToObject(stop, "accountSid", account_sid ? account_sid : "AC00000000000000000000000000000000");
    cJSON_AddStringToObject(stop, "callSid", call_sid ? call_sid : "CA00000000000000000000000000000000");
    out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return out;
}

static rtw_event_type_t map_event(const char *s)
{
    if (!s) return RTW_EVT_UNKNOWN;
    if (!strcmp(s, "connected")) return RTW_EVT_CONNECTED;
    if (!strcmp(s, "start")) return RTW_EVT_START;
    if (!strcmp(s, "media")) return RTW_EVT_MEDIA;
    if (!strcmp(s, "dtmf")) return RTW_EVT_DTMF;
    if (!strcmp(s, "mark")) return RTW_EVT_MARK;
    if (!strcmp(s, "clear")) return RTW_EVT_CLEAR;
    if (!strcmp(s, "stop")) return RTW_EVT_STOP;
    return RTW_EVT_UNKNOWN;
}

int rtw_parse_inbound(const char *json, rtw_inbound_event_t *out,
                      uint8_t *payload_buf, size_t payload_cap)
{
    cJSON *root;
    cJSON *ev;
    cJSON *sid;
    cJSON *seq;
    cJSON *media;
    cJSON *mark;
    cJSON *dtmf;
    const char *payload_b64;
    size_t decoded;
    if (!json || !out) {
        return -1;
    }
    memset(out, 0, sizeof(*out));
    root = cJSON_Parse(json);
    if (!root) {
        return -1;
    }
    ev = cJSON_GetObjectItemCaseSensitive(root, "event");
    if (!cJSON_IsString(ev)) {
        cJSON_Delete(root);
        return -1;
    }
    out->type = map_event(ev->valuestring);
    sid = cJSON_GetObjectItemCaseSensitive(root, "streamSid");
    if (cJSON_IsString(sid)) {
        copy_str(out->stream_sid, sizeof(out->stream_sid), sid->valuestring);
    }
    seq = cJSON_GetObjectItemCaseSensitive(root, "sequenceNumber");
    if (cJSON_IsString(seq)) {
        out->sequence_number = (uint32_t)strtoul(seq->valuestring, NULL, 10);
    } else if (cJSON_IsNumber(seq)) {
        out->sequence_number = (uint32_t)seq->valuedouble;
    }
    if (out->type == RTW_EVT_MEDIA) {
        media = cJSON_GetObjectItemCaseSensitive(root, "media");
        if (!cJSON_IsObject(media)) {
            cJSON_Delete(root);
            return -1;
        }
        {
            cJSON *track = cJSON_GetObjectItemCaseSensitive(media, "track");
            cJSON *chunk = cJSON_GetObjectItemCaseSensitive(media, "chunk");
            cJSON *ts = cJSON_GetObjectItemCaseSensitive(media, "timestamp");
            cJSON *payload = cJSON_GetObjectItemCaseSensitive(media, "payload");
            if (cJSON_IsString(track)) {
                copy_str(out->track, sizeof(out->track), track->valuestring);
            }
            if (cJSON_IsString(chunk)) {
                out->chunk = (uint32_t)strtoul(chunk->valuestring, NULL, 10);
            }
            if (cJSON_IsString(ts)) {
                out->timestamp_ms = (uint32_t)strtoul(ts->valuestring, NULL, 10);
            }
            if (!cJSON_IsString(payload) || !payload_buf) {
                cJSON_Delete(root);
                return -1;
            }
            payload_b64 = payload->valuestring;
            decoded = rtw_base64_decode(payload_b64, strlen(payload_b64), payload_buf, payload_cap);
            if (decoded == (size_t)-1) {
                cJSON_Delete(root);
                return -1;
            }
            out->payload = payload_buf;
            out->payload_len = decoded;
        }
    } else if (out->type == RTW_EVT_MARK) {
        mark = cJSON_GetObjectItemCaseSensitive(root, "mark");
        if (cJSON_IsObject(mark)) {
            cJSON *name = cJSON_GetObjectItemCaseSensitive(mark, "name");
            if (cJSON_IsString(name)) {
                copy_str(out->mark_name, sizeof(out->mark_name), name->valuestring);
            }
        }
    } else if (out->type == RTW_EVT_DTMF) {
        dtmf = cJSON_GetObjectItemCaseSensitive(root, "dtmf");
        if (cJSON_IsObject(dtmf)) {
            cJSON *digit = cJSON_GetObjectItemCaseSensitive(dtmf, "digit");
            if (cJSON_IsString(digit)) {
                copy_str(out->digit, sizeof(out->digit), digit->valuestring);
            }
        }
    }
    cJSON_Delete(root);
    return 0;
}
