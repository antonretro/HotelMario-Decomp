/*
 * cdi_adpcm.c - CD-i 4-bit ADPCM (levels B and C) sector decoder for tools/extract_audio.py.
 * The decoding is port/audio.c's decode_sector, unchanged, with the filter history passed in so that each
 * stream keeps its own. Built into build/tmp/cdi_adpcm.dll (or .so) by extract_audio.py and called through ctypes.
 */
#include <stdint.h>

#ifdef _WIN32
#define EXPORT __declspec(dllexport)
#else
#define EXPORT
#endif

static const int K0[4] = { 0, 60, 115, 98 };
static const int K1[4] = { 0, 0, -52, -55 };

/* Decode one sector: 18 sound groups of 128 bytes, 8 sound units of 28 samples each. hist is int[2][2].
   Returns samples written (stereo pairs count as 2). */
static int decode_sector(const uint8_t *p, int stereo, int16_t *out, int hist[2][2])
{
    int g, k, j, n = 0;
    int16_t units[8][28];
    for (g = 0; g < 18; g++) {
        const uint8_t *grp = p + g * 128;
        for (k = 0; k < 8; k++) {
            int shift = grp[4 + k] & 0x0F, filt = (grp[4 + k] >> 4) & 3;
            int *h = hist[stereo ? (k & 1) : 0];
            for (j = 0; j < 28; j++) {
                int nib = (grp[16 + j * 4 + (k >> 1)] >> ((k & 1) ? 4 : 0)) & 0x0F;
                int t = nib & 8 ? nib - 16 : nib;
                int s = ((t << 12) >> shift) + ((h[0] * K0[filt] + h[1] * K1[filt] + 32) >> 6);
                s = s < -32768 ? -32768 : s > 32767 ? 32767 : s;
                h[1] = h[0];
                h[0] = s;
                units[k][j] = (int16_t)s;
            }
        }
        if (stereo) {
            for (k = 0; k < 8; k += 2)
                for (j = 0; j < 28; j++) { out[n++] = units[k][j]; out[n++] = units[k + 1][j]; }
        } else {
            for (k = 0; k < 8; k++)
                for (j = 0; j < 28; j++) out[n++] = units[k][j];
        }
    }
    return n;
}

/* Decode count sectors of 2304 bytes each, laid end to end, with one shared history (one stream). */
EXPORT int decode_sectors(const uint8_t *p, int count, int stereo, int16_t *out, int *hist)
{
    int i, n = 0;
    for (i = 0; i < count; i++) n += decode_sector(p + i * 2304, stereo, out + n, (int (*)[2])hist);
    return n;
}
