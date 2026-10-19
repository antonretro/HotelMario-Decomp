/*
 * cdrtos.c - CD-i real-time disc playback (SS_Play) and OS-9 signal delivery for the port.
 *
 * The layout below was read from the game's own streaming code (ds_seek, ds_play, ds_setup_channel
 * and the ds_*_signal_handler functions):
 *
 *   PCB (play control block, DsPcb)                PCL (buffer descriptor, 0x1c bytes, chained)
 *     +0x00 w  status: 0x01 end of record            +0x00 b  control (bit 7 = error)
 *              0x10 trigger, 0x100 finished,         +0x01 b  submode of the last sector
 *              0x8000 error/end                      +0x03 b  coding of the last sector
 *     +0x02 w  signal to send; error code at end     +0x04 w  signal to send when this buffer is done
 *     +0x04 l  number of records to play (0 = all)   +0x06 l  next PCL
 *                                                    +0x0a l  buffer
 *     +0x08 l  channel mask                          +0x0e l  buffer size in sectors
 *     +0x0c w  audio channel mask                    +0x18 l  sectors transferred
 *     +0x0e l  video channel list  (CIL: one PCL pointer per channel)
 *     +0x12 l  audio channel list
 *     +0x16 l  data channel list
 *
 * Each filled buffer is reported with the signal stored in its PCL (the game uses 0xFF00/0xFE00/0xFD00
 * for video/audio/data plus the index of its channel entry - not the disc channel number);
 * PCB events with the PCB's own signal (0xFC00). The end of play is reported as error 0x60a.
 * Sectors are fed at the real single-speed rate (75 per second) from rt_poll().
 */
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "m68k.h"
#include "runtime.h"

#define RAW 2352
#define SECTORS_PER_SEC 75

unsigned poll_ctr;

static struct {
    int active, paused;
    FILE *f;
    uint32_t pcb;
    long sector;
    long eors;
    clock_t start;
    long fed;
} play;

static uint32_t icpt_handler, icpt_data;
static int in_handler;
static jmp_buf rte_jmp;

void cdrtos_set_intercept(uint32_t handler, uint32_t data)
{
    icpt_handler = handler;
    icpt_data = data;
}

/* Run the program's signal intercept routine, like OS-9 does when a signal arrives. */
static void deliver(uint32_t sig)
{
    Cpu saved;
    RecompFn fn;
    if (!icpt_handler || in_handler) return;
    if (!(fn = recomp_lookup(icpt_handler))) {
        fprintf(stderr, "signal handler at 0x%x has no entry point\n", icpt_handler);
        return;
    }
    if (trace) fprintf(stderr, "  [cd-i] signal 0x%04x\n", sig);
    saved = cpu;
    cpu.d[1] = sig;
    cpu.a[6] = icpt_data;
    cpu.a[7] -= 0x80;                 /* keep clear of whatever the interrupted code had pushed */
    PUSH32(0xFFFFFFFEu);
    in_handler = 1;
    if (!setjmp(rte_jmp))
        fn();
    in_handler = 0;
    cpu = saved;
}

void cdrtos_rte(void)   /* F$RTE: leave the intercept routine */
{
    if (in_handler) longjmp(rte_jmp, 1);
}

static void pcb_event(uint16_t bits)
{
    uint32_t pcb = play.pcb;
    uint16_t sig = (uint16_t)rd16(pcb + 2);
    wr16(pcb, bits);
    deliver(sig);
}

static void end_play(void)
{
    uint32_t pcb = play.pcb;
    uint16_t sig = (uint16_t)rd16(pcb + 2);
    play.active = 0;
    if (trace) fprintf(stderr, "  [cd-i] end of play at sector %ld\n", play.sector);
    wr16(pcb + 2, 0x60A);             /* the game treats error 0x60a as a normal end */
    wr16(pcb, 0x8000 | 0x100);
    deliver(sig);
}

void cdrtos_play(FILE *f, uint32_t pcb)
{
    play.f = f;
    play.pcb = pcb;
    play.sector = ftell(f) / 2048;    /* ds_seek positions with lseek(path, sector << 11) */
    play.eors = 0;
    play.active = 1;
    play.paused = 0;
    play.start = clock();
    play.fed = 0;
    if (trace)
        fprintf(stderr, "  [cd-i] play from sector %ld: channels %08x audio %04x records %u lists %x/%x/%x\n",
                play.sector, rd32(pcb + 8), rd16(pcb + 0xc), rd32(pcb + 4), rd32(pcb + 0xe), rd32(pcb + 0x12),
                rd32(pcb + 0x16));
}

void cdrtos_stop(void)
{
    if (play.active) {
        play.active = 0;
        wr16(play.pcb, 0x100);
    }
}

void cdrtos_pause(int paused)
{
    play.paused = paused;
    play.start = clock();
    play.fed = 0;
}

/* Bytes already written into each PCL's buffer (form 1 and form 2 sectors differ in size, so the
   sector count alone isn't enough). Keyed by PCL address; reset whenever the game zeroes the count. */
#define NFILL 64
static struct { uint32_t pcl, bytes; } fills[NFILL];

static uint32_t fill_offset(uint32_t pcl)
{
    int i;
    for (i = 0; i < NFILL; i++)
        if (fills[i].pcl == pcl) return fills[i].bytes;
    return 0;
}

static void set_fill_offset(uint32_t pcl, uint32_t bytes)
{
    int i, free_slot = -1;
    for (i = 0; i < NFILL; i++) {
        if (fills[i].pcl == pcl) { fills[i].bytes = bytes; return; }
        if (!fills[i].pcl && free_slot < 0) free_slot = i;
    }
    if (free_slot < 0) free_slot = (int)(pcl >> 2) % NFILL;
    fills[free_slot].pcl = pcl;
    fills[free_slot].bytes = bytes;
}

/* Put one sector into the matching channel's current buffer and send the signals it causes. */
static void feed_sector(void)
{
    uint8_t s[RAW];
    uint32_t pcb = play.pcb, list = 0, pcl, base = 0;
    int ch, sm, ci;

    if (fseek(play.f, play.sector * RAW, SEEK_SET) || fread(s, 1, RAW, play.f) != RAW) {
        end_play();
        return;
    }
    play.sector++;
    ch = s[17] & 31;
    sm = s[18];
    ci = s[19];

    if (sm & 0x02) { list = rd32(pcb + 0x0e); base = 0xFF00; }
    else if (sm & 0x04) { list = (rd16(pcb + 0x0c) >> ch) & 1 ? rd32(pcb + 0x12) : 0; base = 0xFE00; }
    else if (sm & 0x08) { list = rd32(pcb + 0x16); base = 0xFD00; }
    if (!((rd32(pcb + 8) >> ch) & 1)) list = 0;

    if (list && (pcl = rd32(list + ch * 4)) != 0) {
        int size = (sm & 0x20) ? 2324 : 2048;
        uint32_t cnt = rd32(pcl + 0x18), buf = rd32(pcl + 0x0a), bsz = rd32(pcl + 0x0e);
        uint32_t at = cnt ? fill_offset(pcl) : 0;   /* sectors are packed back to back, whatever their form */
        if (buf) memcpy(RAM + ((buf + at) & RAM_MASK), s + 24, size);
        set_fill_offset(pcl, at + size);
        wr8(pcl + 1, sm);
        wr8(pcl + 3, ci);
        wr32(pcl + 0x18, ++cnt);
        if (cnt >= bsz || (sm & 0x01)) {          /* buffer full or record ended: move on and report */
            if (trace)
                fprintf(stderr, "  [cd-i] ch%d %s pcl %x sig %04x buf %x size %u sectors, filled %u sectors / %u bytes, next %x\n",
                        ch, base == 0xFF00 ? "video" : base == 0xFE00 ? "audio" : "data", pcl, rd16(pcl + 4), buf, bsz,
                        cnt, at + size, rd32(pcl + 6));
            uint16_t sig = (uint16_t)rd16(pcl + 4);  /* the game puts the signal to send in the PCL */
            wr32(list + ch * 4, rd32(pcl + 6));
            deliver(sig ? sig : (base | ch));
        }
    }
    if (sm & 0x10) pcb_event(0x10);               /* trigger */
    if (sm & 0x01) {                              /* end of record */
        long want = (long)rd32(pcb + 4);
        pcb_event(0x01);
        if (want && ++play.eors >= want) { end_play(); return; }
    }
    if (sm & 0x80) end_play();                    /* end of file */
}

void rt_poll(void)
{
    long due;
    if (!play.active || play.paused || in_handler) return;
    due = (long)((double)(clock() - play.start) / CLOCKS_PER_SEC * SECTORS_PER_SEC);
    while (play.active && play.fed < due) {
        play.fed++;
        feed_sector();
    }
}
