/*
 * cdrtos.c - CD-i real-time disc playback (SS_Play) and OS-9 signal delivery for the port.
 *
 * Structures as defined in Microware's "CD-I System Libraries Technical Reference Manual" (ss_play(),
 * cdfm.h; build/docs/cdisys.pdf), offsets as the game uses them:
 *
 *   PCB (play control block)                       PCL (play control list, 0x1c bytes, chained)
 *     +0x00 w  PCB_Stat  current status of the play  +0x00 b  PCL_Ctrl  control byte (bit 7 = error)
 *     +0x02 w  PCB_Sig   signal sent on termination;  +0x01 b  reserved
 *                        the error code on error      +0x02 b  PCL_Smode submode of the last sector
 *     +0x04 l  PCB_Rec   max number of records         +0x03 b  PCL_Type  coding of the last sector
 *     +0x08 l  PCB_Chan  channel selection mask        +0x04 w  PCL_Sig   signal sent on buffer full
 *     +0x0c w  PCB_AChan audio-to-memory mask          +0x06 l  PCL_Nxt   next PCL
 *     +0x0e l  PCB_Video array of PCL ptrs per channel +0x0a l  PCL_Buf   buffer
 *     +0x12 l  PCB_Audio                               +0x0e l  PCL_BufSz buffer size (sectors)
 *     +0x16 l  PCB_Data                                +0x12 l  PCL_Err, +0x16 w reserved
 *                                                      +0x18 l  PCL_Cnt   current offset (sectors)
 * The PCB signal goes out at each end of record and trigger bit and when the play ends; the status bits
 * and the three ways a play ends are described at end_play_how() below. The error code is written into
 * PCB_Sig, as the manual describes for the drive's asynchronous operations.
 * Audio sectors on channels in PCB_AChan go to the audio processor's memory, i.e. they are played; other
 * selected audio channels go to the program's RAM through the audio PCLs (see feed_sector).
 * Sectors are fed at the real single-speed rate (75 per second) from rt_poll().
 */
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "m68k.h"
#include "runtime.h"
#include "settings.h"

#define RAW 2352
#define SECTORS_PER_SEC 75

unsigned poll_ctr;

static struct {
    int active, paused, stopping, counted;
    FILE *f;
    uint32_t pcb;
    long sector;
    long eors;
    clock_t start;
    long fed;
} play;

static uint32_t icpt_handler, icpt_data;
static int in_handler;
int signal_flag;           /* set whenever a signal is delivered; F$Sleep wakes on it */
static jmp_buf rte_jmp;

void cdrtos_set_intercept(uint32_t handler, uint32_t data)
{
    icpt_handler = handler;
    icpt_data = data;
}

void cdrtos_get_intercept(uint32_t *handler, uint32_t *data)
{
    *handler = icpt_handler;
    *data = icpt_data;
}

/* Run the program's signal intercept routine, like OS-9 does when a signal arrives. */
static void deliver(uint32_t sig)
{
    Cpu saved;
    RecompFn fn;
    if (!icpt_handler || in_handler) return;
    signal_flag = 1;
    if (!(fn = recomp_lookup(icpt_handler))) {
        /* the handler is interpreted code (a child program such as cdi_bumper): run it in the interpreter */
        interp_signal(sig, icpt_handler, icpt_data);
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
    interp_rte();         /* or an interpreted one */
}

static void pcb_event(uint16_t bits)
{
    uint32_t pcb = play.pcb;
    uint16_t sig = (uint16_t)rd16(pcb + 2);
    if (trace) fprintf(stderr, "  [cd-i] event %04x at sector %ld (records left %u, signal %u)\n", bits, play.sector,
                       rd32(pcb + 4), sig);
    wr16(pcb, bits);
    deliver(sig);
}

/* PCB_Stat bits, as both programs' handlers read them: 0x01 end of record, 0x10 trigger, 0x80 end of file,
   0x100 play finished, 0x8000 error (code in PCB_Sig, which the manual says the drive writes there).
   How a play ends:
     records done (PCB_Rec reached 0)   0x0101, no error - the Philips logo program's 1-record preamble ends
                                        like this and it carries on to the logo
     end of file                        0x8180 + code 0x60a - Hotel Mario's dsERRORcb treats 0x60a as the
                                        normal end; the logo program uses it to see bumper.rtf is over
     ss_abort()                         0x8100 + code 0x60a - both programs then finish cleanly */
#define E_END 0x60A
enum { END_RECORDS, END_OF_FILE, END_ABORT };

static void end_play_how(int how)
{
    uint32_t pcb = play.pcb;
    uint16_t sig = (uint16_t)rd16(pcb + 2);
    static const char *what[] = { "end of play (records done)", "end of play (end of file)", "play aborted" };
    play.active = 0;
    /* The manual: "The file position pointer is updated to the new file position after the play
       terminates." Positions are in 2048-byte sectors (ds_seek uses lseek(sector << 11)); feed_sector moved
       the host file by raw 2352-byte sectors, so set it explicitly. The Philips logo program starts its
       second play from here without seeking; reading the raw offset as sectors made it skip one, which put
       every animation frame one sector out of step. */
    fseek(play.f, play.sector * 2048L, SEEK_SET);
    if (how == END_ABORT) audio_stop();
    if (trace) fprintf(stderr, "  [cd-i] %s at sector %ld (%lu audio sectors so far)\n", what[how], play.sector,
                       audio_sector_count());
    if (how == END_RECORDS) {
        wr16(pcb, 0x0101);
    } else {
        wr16(pcb + 2, E_END);
        wr16(pcb, how == END_OF_FILE ? 0x8180 : 0x8100);
    }
    deliver(sig);
}

static void end_play_status(uint16_t unused, int aborted)
{
    (void)unused;
    end_play_how(aborted ? END_ABORT : END_OF_FILE);
}

static void end_play(void) { end_play_status(0, 0); }

void cdrtos_kill(void)   /* the program that started the play has exited: stop without any signal */
{
    play.active = 0;
    play.stopping = 0;
    audio_stop();
}

void cdrtos_play(FILE *f, uint32_t pcb)
{
    play.f = f;
    play.pcb = pcb;
    play.sector = ftell(f) / 2048;    /* ds_seek positions with lseek(path, sector << 11) */
    play.eors = 0;
    play.counted = rd32(pcb + 4) != 0;            /* 0 records = play to the end of the file */
    play.active = 1;
    play.paused = 0;
    play.stopping = 0;
    play.start = clock();
    play.fed = 0;
    if (trace)
        fprintf(stderr, "  [cd-i] play from sector %ld: channels %08x audio %04x records %u lists %x/%x/%x\n",
                play.sector, rd32(pcb + 8), rd16(pcb + 0xc), rd32(pcb + 4), rd32(pcb + 0xe), rd32(pcb + 0x12),
                rd32(pcb + 0x16));
}

void cdrtos_stop(void)
{
    /* SS_Abort / SS_Stop (e.g. a button press skipping the intro): the player still reports the end of
       play, which is how the game's handlers learn the stream is over (cd_wait waits for exactly that).
       The signal goes out on the next poll, after the system call has returned. */
    if (play.active) play.stopping = 1;
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

    /* audio: PCB_AChan ("audio to memory") means the audio processor's memory, i.e. those channels are
       played. Other selected audio channels go to the program's RAM through the audio PCLs (that's how
       the game streams music into its sound map). The Philips logo program shows it: AChan 0x8000 and no
       audio PCL for channel 15, and its jingle plays on a real player. */
    if ((sm & 0x04) && ((rd32(pcb + 8) >> ch) & 1) && ch < 16 && ((rd16(pcb + 0x0c) >> ch) & 1))
        audio_sector(s + 24, ci);

    if (sm & 0x02) { list = rd32(pcb + 0x0e); base = 0xFF00; }
    else if (sm & 0x04) { list = (ch < 16 && ((rd16(pcb + 0x0c) >> ch) & 1)) ? 0 : rd32(pcb + 0x12); base = 0xFE00; }
    else if (sm & 0x08) { list = rd32(pcb + 0x16); base = 0xFD00; }
    if (!((rd32(pcb + 8) >> ch) & 1)) list = 0;
    if (trace && base == 0xFE00) {                /* audio to memory: where does it go? */
        static unsigned logged;
        if (logged++ < 12)
            fprintf(stderr, "  [cd-i] audio sector %ld ch%d coding %02x: audio list %x, PCL %x\n", play.sector - 1, ch, ci,
                    list, list ? rd32(list + ch * 4) : 0);
    }

    if (list && (pcl = rd32(list + ch * 4)) != 0) {
        int size = (sm & 0x20) ? 2324 : 2048;
        uint32_t cnt = rd32(pcl + 0x18), buf = rd32(pcl + 0x0a), bsz = rd32(pcl + 0x0e);
        uint32_t at = cnt ? fill_offset(pcl) : 0;   /* sectors are packed back to back, whatever their form */
        if (buf) memcpy(RAM + ((buf + at) & RAM_MASK), s + 24, size);
        set_fill_offset(pcl, at + size);
        if (buf && (sm & 0x02))                   /* tell the display what format this video data is in */
            display_note_video(buf, at + size, ci & 0x0F);
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
    if (sm & 0x80) {                              /* end of file wins over an end of record on the same sector */
        if (rd32(pcb + 4)) wr32(pcb + 4, rd32(pcb + 4) - 1);
        end_play_how(END_OF_FILE);
        return;
    }
    if (sm & 0x01) {                              /* end of record */
        /* The drive counts the PCB's "records to play" down itself before signalling; handlers rely on
           it (the Philips logo program only starts its fade-out once it reads 0). */
        /* the drive counts PCB_Rec down; when the requested records are done the play finishes */
        uint32_t left = rd32(pcb + 4);
        if (left) wr32(pcb + 4, --left);
        if (play.counted && !left) { end_play_how(END_RECORDS); return; }
        pcb_event(0x01);                          /* intermediate end of record (the game counts these) */
    }
}

void rt_poll(void)
{
    long due;
    if (!in_handler) { window_tick(); display_maybe_dump(); }
    if (trace) {                               /* periodic display summary for headless debugging */
        static clock_t last;
        if (clock() - last > 10 * CLOCKS_PER_SEC) { last = clock(); display_report(); }
    }
    if ((field_signal || once_signal) && !in_handler) {   /* signals requested from the video driver */
        static clock_t next_field;
        clock_t now = clock();
        if (now >= next_field) {
            next_field = now + CLOCKS_PER_SEC / field_rate();
            if (once_signal) {
                uint32_t sig = once_signal;
                once_signal = 0;
                deliver(sig);
            }
            if (field_signal) deliver(field_signal);
        }
    }
    if (!play.active || in_handler) return;
    if (play.stopping) {
        play.stopping = 0;
        end_play_status(0, 1);                 /* aborted: end of play right away, sound cut */
        return;
    }
    if (play.paused) return;
    due = (long)((double)(clock() - play.start) / CLOCKS_PER_SEC * SECTORS_PER_SEC);
    while (play.active && play.fed < due) {
        play.fed++;
        feed_sector();
    }
}
