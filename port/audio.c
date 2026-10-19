/*
 * audio.c - CD-i real-time audio (XA ADPCM, levels B and C: 4-bit, 37.8 or 18.9 kHz, mono or stereo)
 * decoded and played through SDL. The disc streaming code hands over every audio sector on a channel the
 * game selected in its play control block, just like the player's audio decoder receives them.
 */
#include <SDL.h>
#include <stdio.h>
#include <string.h>

#include "m68k.h"
#include "runtime.h"

#define OUT_RATE 44100

static SDL_AudioDeviceID dev;
const char *wav_path;          /* --wav <file>: also write everything played (44.1 kHz stereo) to a WAV file */
static FILE *wav;
static uint32_t wav_bytes;

static void wav_header(void)
{
    uint8_t h[44];
    uint32_t v[] = { 36 + wav_bytes, 16, 1 | (2u << 16), OUT_RATE, OUT_RATE * 4, 4 | (16u << 16), wav_bytes };
    memcpy(h, "RIFF", 4); memcpy(h + 4, &v[0], 4); memcpy(h + 8, "WAVEfmt ", 8); memcpy(h + 16, &v[1], 4);
    memcpy(h + 20, &v[2], 4); memcpy(h + 24, &v[3], 4); memcpy(h + 28, &v[4], 4); memcpy(h + 32, &v[5], 4);
    memcpy(h + 36, "data", 4); memcpy(h + 40, &v[6], 4);
    fseek(wav, 0, SEEK_SET);
    fwrite(h, 1, 44, wav);
    fseek(wav, 0, SEEK_END);
    fflush(wav);
}
static SDL_AudioStream *stream;
static int stream_rate, stream_ch;
static int hist[2][2];
static unsigned long sectors_played;

static const int K0[4] = { 0, 60, 115, 98 };
static const int K1[4] = { 0, 0, -52, -55 };

void audio_init(void)
{
    SDL_AudioSpec want, have;
    if (wav_path && (wav = fopen(wav_path, "wb"))) wav_header();
    if (headless) return;
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) < 0) {
        fprintf(stderr, "SDL audio: %s - no sound\n", SDL_GetError());
        return;
    }
    memset(&want, 0, sizeof want);
    want.freq = OUT_RATE;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 2048;
    dev = SDL_OpenAudioDevice(0, 0, &want, &have, 0);
    if (!dev) { fprintf(stderr, "SDL audio: %s - no sound\n", SDL_GetError()); return; }
    SDL_PauseAudioDevice(dev, 0);
}

/* Decode one 4-bit ADPCM sector: 18 sound groups of 128 bytes, 8 sound units of 28 samples each. */
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
    return n;   /* samples (stereo pairs count as 2) */
}

/* One real-time audio sector (2304 bytes of sound groups) with its coding byte. */
void audio_sector(const uint8_t *payload, int coding)
{
    static int16_t pcm[18 * 8 * 28];
    int rate = (coding & 0x04) ? 18900 : 37800, ch = (coding & 0x01) ? 2 : 1, n;
    sectors_played++;
    if ((!dev && !wav) || (coding & 0x30)) return;  /* 8-bit level A is not used by this disc */
    if (!stream || rate != stream_rate || ch != stream_ch) {
        if (stream) SDL_FreeAudioStream(stream);
        stream = SDL_NewAudioStream(AUDIO_S16SYS, (Uint8)ch, rate, AUDIO_S16SYS, 2, OUT_RATE);
        stream_rate = rate;
        stream_ch = ch;
        memset(hist, 0, sizeof hist);
    }
    n = decode_sector(payload, ch == 2, pcm);
    SDL_AudioStreamPut(stream, pcm, n * (int)sizeof(int16_t));
    {
        static uint8_t buf[65536];
        int got;
        while ((got = SDL_AudioStreamGet(stream, buf, sizeof buf)) > 0) {
            if (wav) { fwrite(buf, 1, (size_t)got, wav); wav_bytes += (uint32_t)got; wav_header(); }
            if (!dev) continue;
            /* keep latency bounded: if more than half a second is waiting, drop the oldest */
            if (SDL_GetQueuedAudioSize(dev) > OUT_RATE * 4 / 2) SDL_ClearQueuedAudio(dev);
            SDL_QueueAudio(dev, buf, (Uint32)got);
        }
    }
}

void audio_stop(void)   /* playback ended or was aborted: silence what is still queued */
{
    if (dev) SDL_ClearQueuedAudio(dev);
    memset(hist, 0, sizeof hist);
}

unsigned long audio_sector_count(void) { return sectors_played; }
