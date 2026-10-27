/*
 * audio_ogc.c - the Wii / GameCube side of audio.c: CD-i ADPCM sectors decoded and played on one ASND voice at
 * the sector's own rate (37.8 or 18.9 kHz, mono or stereo). Sectors queue up in a ring; the voice's callback
 * hands it the next one when it needs more.
 */
#include <gccore.h>
#include <asndlib.h>
#include <stdio.h>
#include <string.h>

#include "m68k.h"
#include "runtime.h"

#define NBUF 24
#define SECTOR_SAMPLES (18 * 8 * 28)

const char *wav_path;               /* --wav isn't available here */
static int16_t pcm[NBUF][SECTOR_SAMPLES] __attribute__((aligned(32)));
static int bytes[NBUF];
static volatile int rd, wr;         /* ring: wr = next to fill, rd = next to play */
static int cur_rate, cur_ch, volume = 10, started;
static int hist[2][2];
static unsigned long sectors_played;

static const int K0[4] = { 0, 60, 115, 98 };
static const int K1[4] = { 0, 0, -52, -55 };

static void more(s32 voice)         /* ASND: the voice can take the next buffer */
{
    if (rd != wr && ASND_AddVoice(voice, pcm[rd], bytes[rd]) == SND_OK) rd = (rd + 1) % NBUF;
    /* (nothing waiting: the voice runs dry and stops; the next sector starts it again) */
}

void audio_init(void)
{
    ASND_Init();
    ASND_Pause(0);
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
    int rate = (coding & 0x04) ? 18900 : 37800, ch = (coding & 0x01) ? 2 : 1, n, vol = volume * 255 / 10;
    sectors_played++;
    if (coding & 0x30) return;
    if (rate != cur_rate || ch != cur_ch) {                 /* new format: start over */
        ASND_StopVoice(0);
        rd = wr = 0;
        started = 0;
        cur_rate = rate;
        cur_ch = ch;
        memset(hist, 0, sizeof hist);
    }
    if ((wr + 1) % NBUF == rd) rd = (rd + 1) % NBUF;        /* full: drop the oldest */
    n = decode_sector(payload, ch == 2, pcm[wr]);
    bytes[wr] = n * (int)sizeof(int16_t);
    DCFlushRange(pcm[wr], bytes[wr]);
    wr = (wr + 1) % NBUF;
    if (!started || ASND_StatusVoice(0) == SND_UNUSED) {
        ASND_SetVoice(0, ch == 2 ? VOICE_STEREO_16BIT : VOICE_MONO_16BIT, rate, 0, pcm[rd], bytes[rd], vol, vol, more);
        rd = (rd + 1) % NBUF;
        started = 1;
    }
}

void audio_set_volume(int v)
{
    volume = v < 0 ? 0 : v > 10 ? 10 : v;
    ASND_ChangeVolumeVoice(0, volume * 255 / 10, volume * 255 / 10);
}

void audio_pause(int paused) { ASND_PauseVoice(0, paused); }

void audio_stop(void)
{
    ASND_StopVoice(0);
    rd = wr = 0;
    started = 0;
    memset(hist, 0, sizeof hist);
}

unsigned long audio_sector_count(void) { return sectors_played; }
