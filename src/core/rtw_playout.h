#ifndef RTW_PLAYOUT_H
#define RTW_PLAYOUT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RTW_MARK_MAX 64
#define RTW_MARK_NAME_LEN 128

typedef struct {
    char name[RTW_MARK_NAME_LEN];
    size_t bytes_after; /* bytes remaining in buffer when mark was queued */
} rtw_mark_t;

typedef struct {
    uint8_t *buf;
    size_t capacity;
    size_t head;
    size_t tail;
    size_t size;
    rtw_mark_t marks[RTW_MARK_MAX];
    size_t mark_count;
    uint64_t enqueued_bytes;
    uint64_t dequeued_bytes;
    uint64_t clear_count;
} rtw_playout_t;

int rtw_playout_init(rtw_playout_t *p, size_t capacity_bytes);
void rtw_playout_destroy(rtw_playout_t *p);

/* Append mulaw (or PCM) bytes. Returns 0 ok, -1 if would overflow (drops nothing). */
int rtw_playout_write(rtw_playout_t *p, const uint8_t *data, size_t len);

/* Read up to max_len bytes. Returns bytes read. */
size_t rtw_playout_read(rtw_playout_t *p, uint8_t *out, size_t max_len);

/* Queue a mark at current end-of-buffer position. */
int rtw_playout_add_mark(rtw_playout_t *p, const char *name);

/*
 * After reads, call to harvest completed marks into names_out.
 * Returns number of completed marks written (up to max_out).
 */
size_t rtw_playout_pop_completed_marks(rtw_playout_t *p, char names_out[][RTW_MARK_NAME_LEN],
                                       size_t max_out);

/*
 * Clear buffer. Copies pending mark names into names_out (Twilio: marks are returned).
 * Returns number of marks flushed.
 */
size_t rtw_playout_clear(rtw_playout_t *p, char names_out[][RTW_MARK_NAME_LEN], size_t max_out);

size_t rtw_playout_size(const rtw_playout_t *p);

#ifdef __cplusplus
}
#endif

#endif
