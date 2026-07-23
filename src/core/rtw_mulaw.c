#include "rtw_mulaw.h"

#define BIAS 0x84
#define CLIP 32635

uint8_t rtw_linear_to_mulaw(int16_t sample)
{
    static const uint8_t exp_lut[256] = {
        0, 0, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3,
        4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
        5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
        5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
        6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
        6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
        6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
        6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
        7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
        7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
        7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
        7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
        7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
        7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
        7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
        7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7
    };

    int sign = (sample >> 8) & 0x80;
    if (sign) {
        sample = (int16_t)(-sample);
    }
    if (sample > CLIP) {
        sample = CLIP;
    }
    sample = (int16_t)(sample + BIAS);
    int exponent = exp_lut[(sample >> 7) & 0xFF];
    int mantissa = (sample >> (exponent + 3)) & 0x0F;
    uint8_t mulaw = (uint8_t)(~(sign | (exponent << 4) | mantissa));
    return mulaw;
}

int16_t rtw_mulaw_to_linear(uint8_t mulaw)
{
    mulaw = (uint8_t)~mulaw;
    int sign = mulaw & 0x80;
    int exponent = (mulaw >> 4) & 0x07;
    int mantissa = mulaw & 0x0F;
    int sample = ((mantissa << 3) + BIAS) << exponent;
    sample -= BIAS;
    return (int16_t)(sign ? -sample : sample);
}

void rtw_pcm16_to_mulaw(const int16_t *pcm, size_t nsamples, uint8_t *out)
{
    size_t i;
    for (i = 0; i < nsamples; i++) {
        out[i] = rtw_linear_to_mulaw(pcm[i]);
    }
}

void rtw_mulaw_to_pcm16(const uint8_t *mulaw, size_t nsamples, int16_t *out)
{
    size_t i;
    for (i = 0; i < nsamples; i++) {
        out[i] = rtw_mulaw_to_linear(mulaw[i]);
    }
}
