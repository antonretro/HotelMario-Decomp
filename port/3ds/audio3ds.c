/*
 * audio3ds.c - the 3DS side of audio.c: CD-i ADPCM sectors decoded and queued on one NDSP channel at the
 * sector's own rate (37.8 or 18.9 kHz, mono or stereo), so nothing needs resampling here.
 * NDSP needs the DSP firmware (sdmc:/3ds/dspfirm.cdc, dumped once with DSP1) on a real console.
 */
#include <3ds.h>
#include <stdio.h>
#include <string.h>

#include "m68k.h"
#include "runtime.h"

#define NBUF 24                     /* ~1.3 s of 37.8 kHz stereo sectors queued at most */
#define SECTOR_SAMPLES (18 * 8 * 28)

const char *wav_path;               /* --wav isn't available on the 3DS */
static int ok, volume = 10, cur_rate, cur_ch;
static ndspWaveBuf wbuf[NBUF];
static int16_t *pcm[NBUF];
static int next;
static int hist[2][2];
static unsigned long sectors_played;

static const int K0[4] = { 0, 60, 115, 98 };
static const int K1[4] = { 0, 0, -52, -55 };

void audio_init(void)
{
    int i;
    if (R_FAILED(ndspInit())) { printf("No sound: DSP firmware missing (run DSP1 once)\n"); return; }
    ndspSetOutputMode(NDSP_OUTPUT_STEREO);
    for (i = 0; i < NBUF; i++) {
        pcm[i] = linearAlloc(SECTOR_SAMPLES * sizeof(int16_t));
        memset(&wbuf[i], 0, sizeof wbuf[i]);
        wbuf[i].data_vaddr = pcm[i];
        wbuf[i].status = NDSP_WBUF_DONE;
    }
    ok = 1;
}

static void set_format(int rate, int ch)
{
    float mix[12] = { 0 };
    ndspChnReset(0);
    ndspChnSetInterp(0, NDSP_INTERP_LINEAR);
    ndspChnSetRate(0, (float)rate);
    ndspChnSetFormat(0, ch == 2 ? NDSP_FORMAT_STEREO_PCM16 : NDSP_FORMAT_MONO_PCM16);
    mix[0] = mix[1] = volume / 10.0f;
    ndspChnSetMix(0, mix);
    cur_rate = rate;
    cur_ch = ch;
    memset(hist, 0, sizeof hist);
}

static int decode_sector(const uint8_t *p, int stereo, int16_t *out)
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

void audio_sector(const uint8_t *payload, int coding)
{
    int rate = (coding & 0x04) ? 18900 : 37800, ch = (coding & 0x01) ? 2 : 1, n;
    ndspWaveBuf *b;
    sectors_played++;
    if (!ok || (coding & 0x30)) return;
    if (rate != cur_rate || ch != cur_ch) set_format(rate, ch);
    b = &wbuf[next];
    if (b->status != NDSP_WBUF_DONE && b->status != NDSP_WBUF_FREE) {   /* queue full: drop the oldest */
        ndspChnWaveBufClear(0);
        set_format(rate, ch);
    }
    n = decode_sector(payload, ch == 2, pcm[next]);
    b->nsamples = (u32)(n / ch);
    DSP_FlushDataCache(pcm[next], (u32)n * sizeof(int16_t));
    ndspChnWaveBufAdd(0, b);
    next = (next + 1) % NBUF;
}

void audio_set_volume(int v)
{
    volume = v < 0 ? 0 : v > 10 ? 10 : v;
    if (ok && cur_rate) {
        float mix[12] = { 0 };
        mix[0] = mix[1] = volume / 10.0f;
        ndspChnSetMix(0, mix);
    }
}

void audio_pause(int paused) { if (ok) ndspChnSetPaused(0, paused); }

void audio_stop(void)
{
    if (ok && cur_rate) ndspChnWaveBufClear(0);
    memset(hist, 0, sizeof hist);
}

unsigned long audio_sector_count(void) { return sectors_played; }
