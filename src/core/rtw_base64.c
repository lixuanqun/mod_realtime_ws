#include "rtw_base64.h"

static const char kEnc[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static int dec_val(char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

size_t rtw_base64_encoded_len(size_t in_len)
{
    return 4 * ((in_len + 2) / 3) + 1;
}

size_t rtw_base64_encode(const uint8_t *in, size_t in_len, char *out, size_t out_cap)
{
    size_t need = rtw_base64_encoded_len(in_len);
    size_t i = 0, o = 0;
    if (!out || out_cap < need) {
        return 0;
    }
    while (i + 2 < in_len) {
        uint32_t n = ((uint32_t)in[i] << 16) | ((uint32_t)in[i + 1] << 8) | in[i + 2];
        out[o++] = kEnc[(n >> 18) & 63];
        out[o++] = kEnc[(n >> 12) & 63];
        out[o++] = kEnc[(n >> 6) & 63];
        out[o++] = kEnc[n & 63];
        i += 3;
    }
    if (i < in_len) {
        uint32_t n = ((uint32_t)in[i] << 16);
        out[o++] = kEnc[(n >> 18) & 63];
        if (i + 1 < in_len) {
            n |= ((uint32_t)in[i + 1] << 8);
            out[o++] = kEnc[(n >> 12) & 63];
            out[o++] = kEnc[(n >> 6) & 63];
            out[o++] = '=';
        } else {
            out[o++] = kEnc[(n >> 12) & 63];
            out[o++] = '=';
            out[o++] = '=';
        }
    }
    out[o] = '\0';
    return o;
}

size_t rtw_base64_decode(const char *in, size_t in_len, uint8_t *out, size_t out_cap)
{
    size_t i = 0, o = 0;
    int val[4];
    if (!in || !out) {
        return (size_t)-1;
    }
    while (i < in_len) {
        int j, pad = 0;
        for (j = 0; j < 4 && i < in_len; ) {
            char c = in[i++];
            if (c == ' ' || c == '\n' || c == '\r' || c == '\t') {
                continue;
            }
            if (c == '=') {
                val[j++] = 0;
                pad++;
                continue;
            }
            int d = dec_val(c);
            if (d < 0) {
                return (size_t)-1;
            }
            val[j++] = d;
        }
        if (j == 0) {
            break;
        }
        if (j != 4) {
            return (size_t)-1;
        }
        if (o + (size_t)(3 - pad) > out_cap) {
            return (size_t)-1;
        }
        uint32_t n = ((uint32_t)val[0] << 18) | ((uint32_t)val[1] << 12) |
                     ((uint32_t)val[2] << 6) | (uint32_t)val[3];
        out[o++] = (uint8_t)((n >> 16) & 0xFF);
        if (pad < 2) {
            out[o++] = (uint8_t)((n >> 8) & 0xFF);
        }
        if (pad < 1) {
            out[o++] = (uint8_t)(n & 0xFF);
        }
    }
    return o;
}
