#include "rtw_ws_client.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <poll.h>
#include <stdint.h>

struct rtw_ws_client {
    int fd;
    rtw_ws_on_text_fn on_text;
    rtw_ws_on_close_fn on_close;
    void *userdata;
    uint8_t *rx;
    size_t rx_len;
    size_t rx_cap;
    int closed;
};

static int parse_url(const char *url, char *host, size_t host_cap, int *port, char *path,
                     size_t path_cap)
{
    const char *p;
    const char *host_start;
    const char *path_start;
    char portbuf[16];
    if (strncmp(url, "ws://", 5) != 0) {
        return -1; /* only ws:// */
    }
    p = url + 5;
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
        const char *colon;
        if (hostport_len >= sizeof(hostport)) {
            return -1;
        }
        memcpy(hostport, host_start, hostport_len);
        hostport[hostport_len] = '\0';
        colon = strrchr(hostport, ':');
        if (colon) {
            size_t hlen = (size_t)(colon - hostport);
            if (hlen >= host_cap) return -1;
            memcpy(host, hostport, hlen);
            host[hlen] = '\0';
            snprintf(portbuf, sizeof(portbuf), "%s", colon + 1);
            *port = atoi(portbuf);
        } else {
            snprintf(host, host_cap, "%s", hostport);
            *port = 80;
        }
    }
    return 0;
}

static int http_handshake(int fd, const char *host, int port, const char *path)
{
    char req[1024];
    char buf[2048];
    size_t nread = 0;
    int n;
    snprintf(req, sizeof(req),
             "GET %s HTTP/1.1\r\n"
             "Host: %s:%d\r\n"
             "Upgrade: websocket\r\n"
             "Connection: Upgrade\r\n"
             "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
             "Sec-WebSocket-Version: 13\r\n"
             "\r\n",
             path, host, port);
    if (send(fd, req, strlen(req), 0) < 0) {
        return -1;
    }
    while (nread < sizeof(buf) - 1) {
        n = (int)recv(fd, buf + nread, sizeof(buf) - 1 - nread, 0);
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

rtw_ws_client_t *rtw_ws_connect(const char *url, rtw_ws_on_text_fn on_text,
                                rtw_ws_on_close_fn on_close, void *userdata)
{
    char host[256], path[512];
    int port = 80;
    struct addrinfo hints, *res = NULL, *rp;
    char portstr[16];
    int fd = -1;
    rtw_ws_client_t *c;
    if (parse_url(url, host, sizeof(host), &port, path, sizeof(path)) != 0) {
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
        if (fd < 0) continue;
        if (connect(fd, rp->ai_addr, rp->ai_addrlen) == 0) break;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    if (fd < 0) {
        return NULL;
    }
    if (http_handshake(fd, host, port, path) != 0) {
        close(fd);
        return NULL;
    }
    c = (rtw_ws_client_t *)calloc(1, sizeof(*c));
    if (!c) {
        close(fd);
        return NULL;
    }
    c->fd = fd;
    c->on_text = on_text;
    c->on_close = on_close;
    c->userdata = userdata;
    c->rx_cap = 65536;
    c->rx = (uint8_t *)malloc(c->rx_cap);
    if (!c->rx) {
        close(fd);
        free(c);
        return NULL;
    }
    return c;
}

void rtw_ws_close(rtw_ws_client_t *c)
{
    if (!c) return;
    if (c->fd >= 0) {
        close(c->fd);
        c->fd = -1;
    }
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
    if (!c || c->fd < 0 || !text) return -1;
    mask_key(key);
    hdr[0] = 0x81; /* FIN + text */
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
    if (!frame) return -1;
    memcpy(frame, hdr, hdr_len);
    for (i = 0; i < len; i++) {
        frame[hdr_len + i] = (uint8_t)text[i] ^ key[i % 4];
    }
    n = send(c->fd, frame, hdr_len + len, 0);
    free(frame);
    return n < 0 ? -1 : 0;
}

static int ensure_rx_cap(rtw_ws_client_t *c, size_t need)
{
    uint8_t *nbuf;
    if (need <= c->rx_cap) return 0;
    nbuf = (uint8_t *)realloc(c->rx, need);
    if (!nbuf) return -1;
    c->rx = nbuf;
    c->rx_cap = need;
    return 0;
}

static int process_frames(rtw_ws_client_t *c)
{
    while (c->rx_len >= 2) {
        size_t i = 0;
        uint8_t b0 = c->rx[0];
        uint8_t b1 = c->rx[1];
        int opcode = b0 & 0x0F;
        int masked = (b1 & 0x80) != 0;
        uint64_t plen = b1 & 0x7F;
        size_t hdr = 2;
        uint8_t mask[4] = {0};
        if (plen == 126) {
            if (c->rx_len < 4) return 0;
            plen = ((uint64_t)c->rx[2] << 8) | c->rx[3];
            hdr = 4;
        } else if (plen == 127) {
            return -1; /* oversized */
        }
        if (masked) {
            if (c->rx_len < hdr + 4) return 0;
            memcpy(mask, c->rx + hdr, 4);
            hdr += 4;
        }
        if (c->rx_len < hdr + plen) return 0;
        {
            uint8_t *payload = c->rx + hdr;
            size_t j;
            if (masked) {
                for (j = 0; j < plen; j++) payload[j] ^= mask[j % 4];
            }
            if (opcode == 0x1) { /* text */
                char *text = (char *)malloc(plen + 1);
                if (text) {
                    memcpy(text, payload, plen);
                    text[plen] = '\0';
                    if (c->on_text) c->on_text(c->userdata, text, (size_t)plen);
                    free(text);
                }
            } else if (opcode == 0x8) {
                c->closed = 1;
                if (c->on_close) c->on_close(c->userdata, 1000);
                return -1;
            } else if (opcode == 0x9) { /* ping -> pong */
                /* ignore for MVP */
            }
            memmove(c->rx, c->rx + hdr + plen, c->rx_len - hdr - plen);
            c->rx_len -= hdr + plen;
            (void)i;
        }
    }
    return 0;
}

int rtw_ws_poll(rtw_ws_client_t *c, int timeout_ms)
{
    struct pollfd pfd;
    int pr;
    ssize_t n;
    if (!c || c->fd < 0 || c->closed) return -1;
    pfd.fd = c->fd;
    pfd.events = POLLIN;
    pr = poll(&pfd, 1, timeout_ms);
    if (pr < 0) return -1;
    if (pr == 0) return 0;
    if (ensure_rx_cap(c, c->rx_len + 8192) != 0) return -1;
    n = recv(c->fd, c->rx + c->rx_len, c->rx_cap - c->rx_len, 0);
    if (n <= 0) {
        c->closed = 1;
        if (c->on_close) c->on_close(c->userdata, 1006);
        return -1;
    }
    c->rx_len += (size_t)n;
    return process_frames(c);
}

int rtw_ws_fd(const rtw_ws_client_t *c)
{
    return c ? c->fd : -1;
}
