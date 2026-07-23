#ifndef RTW_MULAW_H
#define RTW_MULAW_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* G.711 µ-law encode/decode (ITU-T). */

uint8_t rtw_linear_to_mulaw(int16_t sample);
int16_t rtw_mulaw_to_linear(uint8_t mulaw);

/* Encode PCM16LE samples to mulaw. out must hold nsamples bytes. */
void rtw_pcm16_to_mulaw(const int16_t *pcm, size_t nsamples, uint8_t *out);

/* Decode mulaw to PCM16LE. out must hold nsamples int16s. */
void rtw_mulaw_to_pcm16(const uint8_t *mulaw, size_t nsamples, int16_t *out);

#ifdef __cplusplus
}
#endif

#endif
