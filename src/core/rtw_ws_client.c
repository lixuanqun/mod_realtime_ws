#include "rtw_ws_client.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#ifdef RTW_HAS_OPENSSL
#include <openssl/err.h>
#include <openssl/ssl.h>
#endif

struct rtw_ws_client {
    int fd;
    rtw_ws_on_text_fn on_text;
    rtw_ws_on_close_fn on_close;
    void *userdata;
    uint8_t *rx;
    size_t rx_len;
    size_t rx_cap;
    int closed;
    int use_tls;
    char *extra_headers; /* owned copy, may be NULL */
#ifdef RTW_HAS_OPENSSL
    SSL_CTX *ssl_ctx;
    SSL *ssl;
#endif
};

static int g_tls_insecure = -1; /* -1 = unset (read env), 0/1 = forced */

void rtw_ws_set_tls_insecure(int insecure)
{
    g_tls_insecure = insecure ? 1 : 0;
}

static int tls_insecure_enabled(void)
{
    const char *e;
    if (g_tls_insecure >= 0) {
        return g_tls_insecure;
    }
    e = getenv("RTW_TLS_INSECURE");
    return (e && e[0] == '1') ? 1 : 0;
}

static int parse_url(const char *url, char *host, size_t host_cap, int *port, char *path,
                     size_t path_cap, int *use_tls)
{
    const char *p;
    const char *host_start;
    const char *path_start;
    int tls = 0;
    int default_port = 80;

    if (strncmp(url, "ws://", 5) == 0) {
        p = url + 5;
        tls = 0;
        default_port = 80;
    } else if (strncmp(url, "wss://", 6) == 0) {
#ifndef RTW_HAS_OPENSSL
        return -1;
#else
        p = url + 6;
        tls = 1;
        default_port = 443;
#endif
    } else {
        return -1;
    }

    host_start = p;
    path_start = strchr(p, '/');
    if (!path_start) {
        path_start = p + strlen(p);
        snprintf(path, path_cap, "/");
    } else {
        snprintf(path, path_cap, "%s", path_start);
    }
    {
        size_t hostport_len = (size_t)(path_start - host_start);
        char hostport[256];
        if (hostport_len >= sizeof(hostport) || hostport_len == 0) {
            return -1;
        }
        memcpy(hostport, host_start, hostport_len);
        hostport[hostport_len] = '\0';
        /* IPv6: [::1]:443 or [::1] */
        if (hostport[0] == '[') {
            char *rbr = strchr(hostport, ']');
            if (!rbr) {
                return -1;
            }
            {
                size_t hlen = (size_t)(rbr - hostport - 1);
                if (hlen >= host_cap) {
                    return -1;
                }
                memcpy(host, hostport + 1, hlen);
                host[hlen] = '\0';
            }
            if (rbr[1] == ':') {
                *port = atoi(rbr + 2);
            } else if (rbr[1] == '\0') {
                *port = default_port;
            } else {
                return -1;
            }
        } else {
            const char *colon = strrchr(hostport, ':');
            if (colon) {
                size_t hlen = (size_t)(colon - hostport);
                if (hlen >= host_cap) {
                    return -1;
                }
                memcpy(host, hostport, hlen);
                host[hlen] = '\0';
                *port = atoi(colon + 1);
            } else {
                snprintf(host, host_cap, "%s", hostport);
                *port = default_port;
            }
        }
    }
    *use_tls = tls;
    return 0;
}

static ssize_t io_write_all(rtw_ws_client_t *c, const void *buf, size_t len)
{
    const uint8_t *p = (const uint8_t *)buf;
    size_t off = 0;
    while (off < len) {
        ssize_t n;
#ifdef RTW_HAS_OPENSSL
        if (c->use_tls && c->ssl) {
            n = SSL_write(c->ssl, p + off, (int)(len - off));
            if (n <= 0) {
                int err = SSL_get_error(c->ssl, (int)n);
                if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) {
                    continue;
                }
                return -1;
            }
        } else
#endif
        {
            n = send(c->fd, p + off, len - off, 0);
            if (n < 0) {
                if (errno == EINTR) {
                    continue;
                }
                return -1;
            }
            if (n == 0) {
                return -1;
            }
        }
        off += (size_t)n;
    }
    return (ssize_t)off;
}

static ssize_t io_read(rtw_ws_client_t *c, void *buf, size_t len)
{
#ifdef RTW_HAS_OPENSSL
    if (c->use_tls && c->ssl) {
        int n = SSL_read(c->ssl, buf, (int)len);
        return n;
    }
#endif
    return recv(c->fd, buf, len, 0);
}

static int http_handshake(rtw_ws_client_t *c, const char *host, int port, const char *path)
{
    char *req = NULL;
    char buf[2048];
    char host_hdr[300];
    size_t nread = 0;
    size_t need;
    int n;
    const char *extra = c->extra_headers ? c->extra_headers : "";
    /* Bracket IPv6 literals in Host header. */
    if (strchr(host, ':')) {
        snprintf(host_hdr, sizeof(host_hdr), "[%s]:%d", host, port);
    } else if ((c->use_tls && port == 443) || (!c->use_tls && port == 80)) {
        snprintf(host_hdr, sizeof(host_hdr), "%s", host);
    } else {
        snprintf(host_hdr, sizeof(host_hdr), "%s:%d", host, port);
    }
    need = strlen(path) + strlen(host_hdr) + strlen(extra) + 256;
    req = (char *)malloc(need);
    if (!req) {
        return -1;
    }
    snprintf(req, need,
             "GET %s HTTP/1.1\r\n"
             "Host: %s\r\n"
             "Upgrade: websocket\r\n"
             "Connection: Upgrade\r\n"
             "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
             "Sec-WebSocket-Version: 13\r\n"
             "%s"
             "\r\n",
             path, host_hdr, extra);
    if (io_write_all(c, req, strlen(req)) < 0) {
        free(req);
        return -1;
    }
    free(req);
    while (nread < sizeof(buf) - 1) {
        n = (int)io_read(c, buf + nread, sizeof(buf) - 1 - nread);
        if (n <= 0) {
            return -1;
        }
        nread += (size_t)n;
        buf[nread] = '\0';
        if (strstr(buf, "\r\n\r\n")) {
            break;
        }
    }
    if (!strstr(buf, "101")) {
        return -1;
    }
    return 0;
}

static void mask_key(uint8_t key[4])
{
    FILE *f = fopen("/dev/urandom", "rb");
    if (f) {
        if (fread(key, 1, 4, f) != 4) {
            key[0] = 0x12;
            key[1] = 0x34;
            key[2] = 0x56;
            key[3] = 0x78;
        }
        fclose(f);
    } else {
        key[0] = 0x12;
        key[1] = 0x34;
        key[2] = 0x56;
        key[3] = 0x78;
    }
}

#ifdef RTW_HAS_OPENSSL
static int tls_setup(rtw_ws_client_t *c, const char *host)
{
    const SSL_METHOD *method;
    SSL_library_init();
    SSL_load_error_strings();
    OpenSSL_add_all_algorithms();
    method = TLS_client_method();
    c->ssl_ctx = SSL_CTX_new(method);
    if (!c->ssl_ctx) {
        return -1;
    }
    if (!tls_insecure_enabled()) {
        SSL_CTX_set_verify(c->ssl_ctx, SSL_VERIFY_PEER, NULL);
        SSL_CTX_set_default_verify_paths(c->ssl_ctx);
    } else {
        SSL_CTX_set_verify(c->ssl_ctx, SSL_VERIFY_NONE, NULL);
    }
    c->ssl = SSL_new(c->ssl_ctx);
    if (!c->ssl) {
        return -1;
    }
    SSL_set_fd(c->ssl, c->fd);
    SSL_set_tlsext_host_name(c->ssl, host);
    if (SSL_connect(c->ssl) != 1) {
        return -1;
    }
    return 0;
}
#endif

rtw_ws_client_t *rtw_ws_connect(const char *url, rtw_ws_on_text_fn on_text,
                                rtw_ws_on_close_fn on_close, void *userdata)
{
    return rtw_ws_connect_ex(url, NULL, on_text, on_close, userdata);
}

rtw_ws_client_t *rtw_ws_connect_ex(const char *url, const char *extra_headers,
                                   rtw_ws_on_text_fn on_text, rtw_ws_on_close_fn on_close,
                                   void *userdata)
{
    char host[256], path[512];
    int port = 80;
    int use_tls = 0;
    struct addrinfo hints, *res = NULL, *rp;
    char portstr[16];
    int fd = -1;
    rtw_ws_client_t *c;

    if (parse_url(url, host, sizeof(host), &port, path, sizeof(path), &use_tls) != 0) {
        return NULL;
    }
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    snprintf(portstr, sizeof(portstr), "%d", port);
    if (getaddrinfo(host, portstr, &hints, &res) != 0) {
        return NULL;
    }
    for (rp = res; rp; rp = rp->ai_next) {
        fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd < 0) {
            continue;
        }
        if (connect(fd, rp->ai_addr, rp->ai_addrlen) == 0) {
            break;
        }
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    if (fd < 0) {
        return NULL;
    }

    c = (rtw_ws_client_t *)calloc(1, sizeof(*c));
    if (!c) {
        close(fd);
        return NULL;
    }
    c->fd = fd;
    c->use_tls = use_tls;
    c->on_text = on_text;
    c->on_close = on_close;
    c->userdata = userdata;
    if (extra_headers && extra_headers[0]) {
        c->extra_headers = strdup(extra_headers);
        if (!c->extra_headers) {
            close(fd);
            free(c);
            return NULL;
        }
    }
    c->rx_cap = 65536;
    c->rx = (uint8_t *)malloc(c->rx_cap);
    if (!c->rx) {
        close(fd);
        free(c->extra_headers);
        free(c);
        return NULL;
    }

#ifdef RTW_HAS_OPENSSL
    if (use_tls) {
        if (tls_setup(c, host) != 0) {
            rtw_ws_close(c);
            return NULL;
        }
    }
#else
    (void)use_tls;
#endif

    if (http_handshake(c, host, port, path) != 0) {
        rtw_ws_close(c);
        return NULL;
    }
    return c;
}

void rtw_ws_close(rtw_ws_client_t *c)
{
    if (!c) {
        return;
    }
#ifdef RTW_HAS_OPENSSL
    if (c->ssl) {
        SSL_shutdown(c->ssl);
        SSL_free(c->ssl);
        c->ssl = NULL;
    }
    if (c->ssl_ctx) {
        SSL_CTX_free(c->ssl_ctx);
        c->ssl_ctx = NULL;
    }
#endif
    if (c->fd >= 0) {
        close(c->fd);
        c->fd = -1;
    }
    free(c->extra_headers);
    free(c->rx);
    free(c);
}

int rtw_ws_send_text(rtw_ws_client_t *c, const char *text, size_t len)
{
    uint8_t hdr[14];
    uint8_t key[4];
    uint8_t *frame;
    size_t hdr_len;
    size_t i;
    ssize_t n;
    if (!c || c->fd < 0 || !text) {
        return -1;
    }
    mask_key(key);
    hdr[0] = 0x81;
    if (len < 126) {
        hdr[1] = (uint8_t)(0x80 | len);
        hdr_len = 2;
    } else if (len < 65536) {
        hdr[1] = 0x80 | 126;
        hdr[2] = (uint8_t)((len >> 8) & 0xFF);
        hdr[3] = (uint8_t)(len & 0xFF);
        hdr_len = 4;
    } else {
        return -1;
    }
    memcpy(hdr + hdr_len, key, 4);
    hdr_len += 4;
    frame = (uint8_t *)malloc(hdr_len + len);
    if (!frame) {
        return -1;
    }
    memcpy(frame, hdr, hdr_len);
    for (i = 0; i < len; i++) {
        frame[hdr_len + i] = (uint8_t)text[i] ^ key[i % 4];
    }
    n = io_write_all(c, frame, hdr_len + len);
    free(frame);
    return n < 0 ? -1 : 0;
}

static int ensure_rx_cap(rtw_ws_client_t *c, size_t need)
{
    uint8_t *nbuf;
    if (need <= c->rx_cap) {
        return 0;
    }
    nbuf = (uint8_t *)realloc(c->rx, need);
    if (!nbuf) {
        return -1;
    }
    c->rx = nbuf;
    c->rx_cap = need;
    return 0;
}

static int process_frames(rtw_ws_client_t *c)
{
    while (c->rx_len >= 2) {
        uint8_t b0 = c->rx[0];
        uint8_t b1 = c->rx[1];
        int opcode = b0 & 0x0F;
        int masked = (b1 & 0x80) != 0;
        uint64_t plen = b1 & 0x7F;
        size_t hdr = 2;
        uint8_t mask[4] = {0};
        if (plen == 126) {
            if (c->rx_len < 4) {
                return 0;
            }
            plen = ((uint64_t)c->rx[2] << 8) | c->rx[3];
            hdr = 4;
        } else if (plen == 127) {
            return -1;
        }
        if (masked) {
            if (c->rx_len < hdr + 4) {
                return 0;
            }
            memcpy(mask, c->rx + hdr, 4);
            hdr += 4;
        }
        if (c->rx_len < hdr + plen) {
            return 0;
        }
        {
            uint8_t *payload = c->rx + hdr;
            size_t j;
            if (masked) {
                for (j = 0; j < plen; j++) {
                    payload[j] ^= mask[j % 4];
                }
            }
            if (opcode == 0x1) {
                char *text = (char *)malloc(plen + 1);
                if (text) {
                    memcpy(text, payload, plen);
                    text[plen] = '\0';
                    if (c->on_text) {
                        c->on_text(c->userdata, text, (size_t)plen);
                    }
                    free(text);
                }
            } else if (opcode == 0x8) {
                c->closed = 1;
                if (c->on_close) {
                    c->on_close(c->userdata, 1000);
                }
                return -1;
            }
            memmove(c->rx, c->rx + hdr + plen, c->rx_len - hdr - plen);
            c->rx_len -= hdr + plen;
        }
    }
    return 0;
}

int rtw_ws_poll(rtw_ws_client_t *c, int timeout_ms)
{
    struct pollfd pfd;
    int pr;
    ssize_t n;
    if (!c || c->fd < 0 || c->closed) {
        return -1;
    }
    pfd.fd = c->fd;
    pfd.events = POLLIN;
    pr = poll(&pfd, 1, timeout_ms);
    if (pr < 0) {
        return -1;
    }
    if (pr == 0) {
        return 0;
    }
    if (ensure_rx_cap(c, c->rx_len + 8192) != 0) {
        return -1;
    }
    n = io_read(c, c->rx + c->rx_len, c->rx_cap - c->rx_len);
    if (n <= 0) {
        c->closed = 1;
        if (c->on_close) {
            c->on_close(c->userdata, 1006);
        }
        return -1;
    }
    c->rx_len += (size_t)n;
    return process_frames(c);
}

int rtw_ws_fd(const rtw_ws_client_t *c)
{
    return c ? c->fd : -1;
}

int rtw_ws_is_tls(const rtw_ws_client_t *c)
{
    return c ? c->use_tls : 0;
}
