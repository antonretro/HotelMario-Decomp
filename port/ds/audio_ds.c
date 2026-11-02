/*
 * audio_ds.c - the DS side of audio.c: CD-i ADPCM sectors decoded on the ARM9 into a looping ring that the sound
 * hardware plays at the sector's own rate (37.8 or 18.9 kHz, mono or stereo), so nothing needs resampling here.
 * The ring is 8 stereo sectors (0.43 s) or 4 mono ones; playing starts two sectors behind the decoder. Each sector
 * also clears the one after it, so a late sector gives silence rather than a repeat.
 */
#include <nds.h>
#include <stdio.h>
#include <string.h>

#include "m68k.h"
#include "runtime.h"

#define SECTOR_SAMPLES (18 * 8 * 28)  /* mono; stereo gives half as many to each side */
#define RING_SAMPLES (4 * SECTOR_SAMPLES)
#define LEAD_SECTORS 2

const char *wav_path;                 /* --wav isn't available on the DS */
static int16_t ring[2][RING_SAMPLES] __attribute__((aligned(4)));
static int volume = 10, cur_rate, cur_ch, playing, write_pos, ready_sectors;
static int sound_id[2] = { -1, -1 };
static int hist[2][2];
static unsigned long sectors_played;

static const int K0[4] = { 0, 60, 115, 98 };
static const int K1[4] = { 0, 0, -52, -55 };

void audio_init(void)
{
    soundEnable();
}

static void stop_channels(void)
{
    int c;
    for (c = 0; c < 2; c++)
        if (sound_id[c] >= 0) {
            soundKill(sound_id[c]);
            sound_id[c] = -1;
        }
    playing = 0;
}

static void start_channels(void)
{
    int vol = volume * 127 / 10, c;
    DC_FlushRange(ring, sizeof ring);
    for (c = 0; c < cur_ch; c++)
        sound_id[c] = soundPlaySample(ring[c], SoundFormat_16Bit, RING_SAMPLES * sizeof(int16_t), (u16)cur_rate, (u8)vol,
                                      cur_ch == 1 ? 64 : c ? 127 : 0, true, 0);
    playing = 1;
}

static void set_format(int rate, int ch)
{
    stop_channels();
    cur_rate = rate;
    cur_ch = ch;
    write_pos = 0;
    ready_sectors = 0;
    memset(hist, 0, sizeof hist);
    memset(ring, 0, sizeof ring);
}

/* 18 sound groups of 8 units of 28 samples: 4032 samples at a time, 18 times a second for 37.8 kHz stereo */
__attribute__((noinline)) ARM_CODE ITCM_CODE static void decode_sector(const uint8_t *p, int stereo, int16_t *left,
                                                                       int16_t *right)
{
    int g, k, j, step = stereo ? 28 * 4 : 28 * 8;
    for (g = 0; g < 18; g++) {
        const uint8_t *grp = p + g * 128;
        for (k = 0; k < 8; k++) {
            int shift = grp[4 + k] & 0x0F, filt = (grp[4 + k] >> 4) & 3, k0 = K0[filt], k1 = K1[filt];
            int nibble_shift = (k & 1) ? 4 : 0, byte = k >> 1;
            int *h = hist[stereo ? (k & 1) : 0];
            int h0 = h[0], h1 = h[1];
            int16_t *to = stereo ? ((k & 1) ? right : left) + byte * 28 : left + k * 28;
            for (j = 0; j < 28; j++) {
                int nib = (grp[16 + j * 4 + byte] >> nibble_shift) & 0x0F;
                int t = nib & 8 ? nib - 16 : nib;
                int s = ((t << 12) >> shift) + ((h0 * k0 + h1 * k1 + 32) >> 6);
                s = s < -32768 ? -32768 : s > 32767 ? 32767 : s;
                h1 = h0;
                h0 = s;
                to[j] = (int16_t)s;
            }
            h[0] = h0;
            h[1] = h1;
        }
        left += step;
        right += step;
    }
}

void audio_sector(const uint8_t *payload, int coding)
{
    int rate = (coding & 0x04) ? 18900 : 37800, ch = (coding & 0x01) ? 2 : 1;
    int samples = SECTOR_SAMPLES / ch, next;
    sectors_played++;
    if (coding & 0x30) return;
    if (rate != cur_rate || ch != cur_ch) set_format(rate, ch);
    decode_sector(payload, ch == 2, ring[0] + write_pos, ring[1] + write_pos);
    next = (write_pos + samples) % RING_SAMPLES;
    {
        int c;
        for (c = 0; c < ch; c++) {
            if (playing) memset(ring[c] + next, 0, (size_t)samples * sizeof(int16_t));
            DC_FlushRange(ring[c] + write_pos, (size_t)samples * sizeof(int16_t));
            DC_FlushRange(ring[c] + next, (size_t)samples * sizeof(int16_t));
        }
    }
    write_pos = next;
    if (!playing && ++ready_sectors >= LEAD_SECTORS) start_channels();
}

void audio_set_volume(int v)
{
    int c;
    volume = v < 0 ? 0 : v > 10 ? 10 : v;
    for (c = 0; c < 2; c++)
        if (sound_id[c] >= 0) soundSetVolume(sound_id[c], (u8)(volume * 127 / 10));
}

void audio_pause(int paused)
{
    int c;
    for (c = 0; c < 2; c++)
        if (sound_id[c] >= 0) {
            if (paused) soundPause(sound_id[c]);
            else soundResume(sound_id[c]);
        }
}

void audio_stop(void)
{
    stop_channels();
    write_pos = 0;
    ready_sectors = 0;
    memset(hist, 0, sizeof hist);
    memset(ring, 0, sizeof ring);
}

unsigned long audio_sector_count(void) { return sectors_played; }
