/*
 * soundmap.c - CD-i sound maps: memory holding ADPCM sound groups that the audio processor plays, here
 * decoded through audio.c. The game makes one Level B stereo map of 18 sound groups (one sector) at start-up
 * and loops it forever; in the stages the disc streams the music into it a sector every 53 ms and cd_mix
 * ORs the sound effects in, so whatever is in the map when it plays is what you hear.
 *
 * System calls (CD-I System Libraries manual, sm_* / sd_* / sc_*; the codes from the game's clib bindings):
 *   SetStt 0x3b d2=0 sm_creat  d3 = type, d4 = sound groups -> d0 = id, a0 = data area
 *               d2=1 sm_out    d3 = id, a0 = status block (asy_stat, asy_sig)
 *               d2=2 sm_off    stop output      d2=4 sm_close
 *   GetStt 0x3b d2=0 sm_info   d3 = id -> a0 = SoundmapDesc      d2=1 sm_stat
 *   SetStt 0x3c d2=2 sd_loop   d3 = id, d4 = first group, d5 = last group, d6 = loop count (-1 = for ever)
 *   SetStt 0x3d      sc_atten  d3 = attenuation (0 = full volume)
 */
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "m68k.h"
#include "runtime.h"

#define GROUP 128                 /* bytes per sound group */
#define NMAPS 8

static struct {
    int used, type, playing;
    uint32_t data, size, desc, statblk;
    int sectors, pos;             /* size in sectors, sector playing next */
    clock_t next;                 /* when the next sector is due */
    int fresh;                    /* new data was streamed in since that sector last played */
} maps[NMAPS];

/* Level B/C, mono/stereo -> the coding byte audio_sector() takes (bit 0 stereo, bit 2 18.9 kHz) */
static int coding_of(int type)
{
    switch (type) {
    case 2: return 0x00;          /* Level B mono   37.8 kHz */
    case 3: return 0x01;          /* Level B stereo 37.8 kHz */
    case 4: return 0x04;          /* Level C mono   18.9 kHz */
    case 5: return 0x05;          /* Level C stereo 18.9 kHz */
    default: return 0x30;         /* Level A (8-bit): not used by this game, audio_sector skips it */
    }
}

/* One sector of sound groups plays for 18 x 8 x 28 samples, halved for stereo, at the type's rate. */
static clock_t sector_time(int type)
{
    int c = coding_of(type), frames = 18 * 8 * 28 / ((c & 1) ? 2 : 1), rate = (c & 4) ? 18900 : 37800;
    return (clock_t)((double)frames / rate * CLOCKS_PER_SEC);
}

static void play_sector(int i)
{
    if (!maps[i].sectors) return;
    audio_sector(RAM + ((maps[i].data + (uint32_t)maps[i].pos * 18 * GROUP) & RAM_MASK), coding_of(maps[i].type));
    maps[i].pos = (maps[i].pos + 1) % maps[i].sectors;
    maps[i].fresh = 0;
}

int soundmap_setstt(uint32_t fn)
{
    int i, id = (int)(cpu.d[3] & 0xFFFF);
    uint32_t sub = cpu.d[2] & 0xFFFF;
    if (fn == 0x3b && sub == 0) {                   /* sm_creat */
        int groups = (int)(cpu.d[4] & 0xFFFF);
        for (i = 0; i < NMAPS && maps[i].used; i++) ;
        if (i == NMAPS) return 0;
        memset(&maps[i], 0, sizeof maps[i]);
        maps[i].used = 1;
        maps[i].type = id;                          /* d3 carries the data type here */
        maps[i].sectors = (groups + 17) / 18;
        maps[i].size = (uint32_t)maps[i].sectors * 18 * GROUP;
        maps[i].data = os9_alloc(maps[i].size);
        maps[i].desc = os9_alloc(0x30);
        /* SoundmapDesc: smd_id, stloop, endloop, lpcnt, lcntr (shorts), smaddr, smsize, curaddr, asyblk (longs),
           nogrps, grpnum (shorts), coding, res1 (bytes), lpaddr ... */
        wr16(maps[i].desc + 0x00, (uint32_t)(i + 1));
        wr32(maps[i].desc + 0x0a, maps[i].data);
        wr32(maps[i].desc + 0x0e, maps[i].size);
        wr32(maps[i].desc + 0x12, maps[i].data);
        wr16(maps[i].desc + 0x1a, (uint32_t)groups);
        wr8(maps[i].desc + 0x1e, (uint32_t)coding_of(id));
        cpu.d[0] = (uint32_t)(i + 1);
        cpu.a[0] = maps[i].data;
        if (trace) fprintf(stderr, "  [snd] sm_creat type %d, %d groups -> map %d at %x\n", id, groups, i + 1,
                           maps[i].data);
        return 1;
    }
    i = id - 1;
    if (i < 0 || i >= NMAPS || !maps[i].used) i = -1;
    if (fn == 0x3b && sub == 1 && i >= 0) {         /* sm_out: start playing */
        maps[i].playing = 1;
        maps[i].pos = 0;
        maps[i].statblk = cpu.a[0];
        maps[i].next = game_clock();
        maps[i].fresh = 1;
        if (maps[i].statblk) wr16(maps[i].statblk, 0);           /* asy_stat: running */
        if (trace) fprintf(stderr, "  [snd] sm_out map %d\n", id);
        return 1;
    }
    if (fn == 0x3b && (sub == 2 || sub == 4)) {     /* sm_off (any map), sm_close */
        int k;
        for (k = 0; k < NMAPS; k++)
            if (maps[k].used && (sub == 2 || k == i)) maps[k].playing = 0;
        if (sub == 4 && i >= 0) maps[i].used = 0;
        audio_stop();
        if (trace) fprintf(stderr, "  [snd] sm_%s\n", sub == 2 ? "off" : "close");
        return 1;
    }
    if (fn == 0x3c && sub == 2 && i >= 0) {         /* sd_loop: the map loops; only whole-map loops are used */
        if (trace) fprintf(stderr, "  [snd] sd_loop map %d groups %d-%d count %d\n", id, (int)(cpu.d[4] & 0xFFFF),
                           (int)(cpu.d[5] & 0xFFFF), (int16_t)cpu.d[6]);
        return 1;
    }
    if (fn == 0x3d) return 1;                       /* sc_atten: the game sets full volume (0) */
    return 0;
}

int soundmap_getstt(uint32_t fn)
{
    int i = (int)(cpu.d[3] & 0xFFFF) - 1;
    if (fn != 0x3b) return 0;
    if ((cpu.d[2] & 0xFFFF) == 0 && i >= 0 && i < NMAPS && maps[i].used) {   /* sm_info */
        cpu.a[0] = maps[i].desc;
        return 1;
    }
    return (cpu.d[2] & 0xFFFF) == 1;                /* sm_stat: nothing to report */
}

/* The disc just streamed a sector into memory at buf (and the game's callback has run: cd_mix mixed in the
   sound effects). If that's a playing map, play it now: the stream sets the pace, a sector per 53 ms. */
void soundmap_filled(uint32_t buf)
{
    int i;
    for (i = 0; i < NMAPS; i++)
        if (maps[i].used && buf >= maps[i].data && buf < maps[i].data + maps[i].size) {
            maps[i].fresh = 1;
            if (maps[i].playing) {
                maps[i].pos = (int)((buf - maps[i].data) / (18 * GROUP));
                play_sector(i);
                maps[i].next = game_clock() + sector_time(maps[i].type);
            }
        }
}

/* Called often: a playing map with no new data keeps looping what it holds, like the audio processor. */
void soundmap_poll(void)
{
    int i;
    clock_t now = game_clock();
    for (i = 0; i < NMAPS; i++)
        if (maps[i].used && maps[i].playing && now >= maps[i].next + sector_time(maps[i].type) / 2) {
            play_sector(i);
            maps[i].next = now + sector_time(maps[i].type);
        }
}
