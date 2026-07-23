#include "rtw_queue.h"

#include <stdlib.h>
#include <string.h>

int rtw_queue_init(rtw_bounded_queue_t *q, size_t capacity)
{
    if (!q || capacity == 0) {
        return -1;
    }
    memset(q, 0, sizeof(*q));
    q->items = (rtw_queue_item_t *)calloc(capacity, sizeof(rtw_queue_item_t));
    if (!q->items) {
        return -1;
    }
    q->capacity = capacity;
    return 0;
}

void rtw_queue_destroy(rtw_bounded_queue_t *q)
{
    size_t i;
    if (!q) {
        return;
    }
    for (i = 0; i < q->capacity; i++) {
        free(q->items[i].data);
    }
    free(q->items);
    memset(q, 0, sizeof(*q));
}

static void free_slot(rtw_queue_item_t *it)
{
    free(it->data);
    it->data = NULL;
    it->len = 0;
}

int rtw_queue_push(rtw_bounded_queue_t *q, const char *data, size_t len)
{
    char *copy;
    if (!q || !data) {
        return -1;
    }
    if (q->size == q->capacity) {
        free_slot(&q->items[q->head]);
        q->head = (q->head + 1) % q->capacity;
        q->size--;
        q->drops++;
    }
    copy = (char *)malloc(len + 1);
    if (!copy) {
        return -1;
    }
    memcpy(copy, data, len);
    copy[len] = '\0';
    q->items[q->tail].data = copy;
    q->items[q->tail].len = len;
    q->tail = (q->tail + 1) % q->capacity;
    q->size++;
    q->enqueued++;
    return 0;
}

int rtw_queue_pop(rtw_bounded_queue_t *q, rtw_queue_item_t *out)
{
    if (!q || !out || q->size == 0) {
        return -1;
    }
    *out = q->items[q->head];
    q->items[q->head].data = NULL;
    q->items[q->head].len = 0;
    q->head = (q->head + 1) % q->capacity;
    q->size--;
    return 0;
}

int rtw_queue_peek(const rtw_bounded_queue_t *q, const char **data_out, size_t *len_out)
{
    if (!q || q->size == 0 || !data_out) {
        return -1;
    }
    *data_out = q->items[q->head].data;
    if (len_out) {
        *len_out = q->items[q->head].len;
    }
    return 0;
}

int rtw_queue_drop_head(rtw_bounded_queue_t *q)
{
    if (!q || q->size == 0) {
        return -1;
    }
    free_slot(&q->items[q->head]);
    q->head = (q->head + 1) % q->capacity;
    q->size--;
    return 0;
}

size_t rtw_queue_size(const rtw_bounded_queue_t *q)
{
    return q ? q->size : 0;
}
