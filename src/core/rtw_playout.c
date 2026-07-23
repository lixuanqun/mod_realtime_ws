#include "rtw_playout.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int rtw_playout_init(rtw_playout_t *p, size_t capacity_bytes)
{
    if (!p || capacity_bytes == 0) {
        return -1;
    }
    memset(p, 0, sizeof(*p));
    p->buf = (uint8_t *)malloc(capacity_bytes);
    if (!p->buf) {
        return -1;
    }
    p->capacity = capacity_bytes;
    return 0;
}

void rtw_playout_destroy(rtw_playout_t *p)
{
    if (!p) {
        return;
    }
    free(p->buf);
    memset(p, 0, sizeof(*p));
}

static void advance_marks_on_read(rtw_playout_t *p, size_t n)
{
    size_t i;
    for (i = 0; i < p->mark_count; i++) {
        if (p->marks[i].bytes_after >= n) {
            p->marks[i].bytes_after -= n;
        } else {
            p->marks[i].bytes_after = 0;
        }
    }
}

int rtw_playout_write(rtw_playout_t *p, const uint8_t *data, size_t len)
{
    size_t i;
    if (!p || (!data && len) || len == 0) {
        return len == 0 ? 0 : -1;
    }
    if (p->size + len > p->capacity) {
        return -1;
    }
    for (i = 0; i < len; i++) {
        p->buf[p->tail] = data[i];
        p->tail = (p->tail + 1) % p->capacity;
    }
    p->size += len;
    p->enqueued_bytes += len;
    return 0;
}

size_t rtw_playout_read(rtw_playout_t *p, uint8_t *out, size_t max_len)
{
    size_t n;
    size_t i;
    if (!p || !out || max_len == 0) {
        return 0;
    }
    n = max_len < p->size ? max_len : p->size;
    for (i = 0; i < n; i++) {
        out[i] = p->buf[p->head];
        p->head = (p->head + 1) % p->capacity;
    }
    p->size -= n;
    p->dequeued_bytes += n;
    advance_marks_on_read(p, n);
    return n;
}

int rtw_playout_add_mark(rtw_playout_t *p, const char *name)
{
    rtw_mark_t *m;
    if (!p || !name || p->mark_count >= RTW_MARK_MAX) {
        return -1;
    }
    m = &p->marks[p->mark_count++];
    snprintf(m->name, sizeof(m->name), "%s", name);
    m->bytes_after = p->size;
    return 0;
}

size_t rtw_playout_pop_completed_marks(rtw_playout_t *p, char names_out[][RTW_MARK_NAME_LEN],
                                       size_t max_out)
{
    size_t out_n = 0;
    size_t i = 0;
    if (!p) {
        return 0;
    }
    while (i < p->mark_count) {
        if (p->marks[i].bytes_after == 0) {
            if (names_out && out_n < max_out) {
                snprintf(names_out[out_n], RTW_MARK_NAME_LEN, "%s", p->marks[i].name);
                out_n++;
            }
            memmove(&p->marks[i], &p->marks[i + 1],
                    (p->mark_count - i - 1) * sizeof(rtw_mark_t));
            p->mark_count--;
        } else {
            i++;
        }
    }
    return out_n;
}

size_t rtw_playout_clear(rtw_playout_t *p, char names_out[][RTW_MARK_NAME_LEN], size_t max_out)
{
    size_t n = 0;
    size_t i;
    if (!p) {
        return 0;
    }
    for (i = 0; i < p->mark_count && n < max_out; i++) {
        if (names_out) {
            snprintf(names_out[n], RTW_MARK_NAME_LEN, "%s", p->marks[i].name);
        }
        n++;
    }
    p->head = p->tail = p->size = 0;
    p->mark_count = 0;
    p->clear_count++;
    return n;
}

size_t rtw_playout_size(const rtw_playout_t *p)
{
    return p ? p->size : 0;
}
