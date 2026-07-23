#ifndef RTW_BASE64_H
#define RTW_BASE64_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Returns bytes written excluding NUL, or 0 on failure. out_cap includes NUL. */
size_t rtw_base64_encode(const uint8_t *in, size_t in_len, char *out, size_t out_cap);

/* Returns decoded byte count, or (size_t)-1 on failure. */
size_t rtw_base64_decode(const char *in, size_t in_len, uint8_t *out, size_t out_cap);

/* Required output capacity (including NUL) for encode. */
size_t rtw_base64_encoded_len(size_t in_len);

#ifdef __cplusplus
}
#endif

#endif
