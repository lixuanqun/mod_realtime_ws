#ifndef RTW_WS_CLIENT_H
#define RTW_WS_CLIENT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct rtw_ws_client rtw_ws_client_t;

typedef void (*rtw_ws_on_text_fn)(void *userdata, const char *text, size_t len);
typedef void (*rtw_ws_on_close_fn)(void *userdata, int code);

/*
 * RFC6455 client: ws:// always; wss:// when built with RTW_HAS_OPENSSL.
 *
 * TLS verify: enabled by default for wss://. Set env RTW_TLS_INSECURE=1
 * (or call rtw_ws_set_tls_insecure(1)) to skip certificate verification — lab only.
 *
 * extra_headers: optional raw header block, each line ending with \\r\\n,
 * e.g. "Authorization: Bearer tok\\r\\nX-Api-Key: abc\\r\\n"
 */
void rtw_ws_set_tls_insecure(int insecure);

rtw_ws_client_t *rtw_ws_connect(const char *url, rtw_ws_on_text_fn on_text,
                                rtw_ws_on_close_fn on_close, void *userdata);

rtw_ws_client_t *rtw_ws_connect_ex(const char *url, const char *extra_headers,
                                   rtw_ws_on_text_fn on_text, rtw_ws_on_close_fn on_close,
                                   void *userdata);

void rtw_ws_close(rtw_ws_client_t *c);

int rtw_ws_send_text(rtw_ws_client_t *c, const char *text, size_t len);

/* Poll socket; process incoming frames. timeout_ms may be 0. Returns 0 ok, -1 closed/error. */
int rtw_ws_poll(rtw_ws_client_t *c, int timeout_ms);

int rtw_ws_fd(const rtw_ws_client_t *c);
int rtw_ws_is_tls(const rtw_ws_client_t *c);

#ifdef __cplusplus
}
#endif

#endif
