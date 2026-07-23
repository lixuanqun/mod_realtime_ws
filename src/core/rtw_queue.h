#ifndef RTW_QUEUE_H
#define RTW_QUEUE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char *data;
    size_t len;
} rtw_queue_item_t;

typedef struct {
    rtw_queue_item_t *items;
    size_t capacity;
    size_t head;
    size_t tail;
    size_t size;
    uint64_t drops;
    uint64_t enqueued;
} rtw_bounded_queue_t;

int rtw_queue_init(rtw_bounded_queue_t *q, size_t capacity);
void rtw_queue_destroy(rtw_bounded_queue_t *q);

/* Copies data. If full, drops oldest then enqueues (drop-oldest policy). */
int rtw_queue_push(rtw_bounded_queue_t *q, const char *data, size_t len);

/* Pops into *out (malloc'd). Caller frees out->data. Returns 0 ok, -1 empty. */
int rtw_queue_pop(rtw_bounded_queue_t *q, rtw_queue_item_t *out);

size_t rtw_queue_size(const rtw_bounded_queue_t *q);

#ifdef __cplusplus
}
#endif

#endif
