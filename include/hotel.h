/*
 * hotel.h - shared declarations for the Hotel Mario (CD-i) decompilation.
 *
 * Written for Microware OS-9/68000 C 3.x (K&R C): int and long are 32 bits, short is 16.
 * The first two arguments of a call go in d0/d1, the rest on the stack.
 * Names come from the original cdi_hotel.stb symbol table. Struct members that
 * are not understood yet are named after their offset (unkXXX).
 */
#ifndef HOTEL_H
#define HOTEL_H

#include <stdio.h>

/* Graphics/data set loaded per stage ("gds"); only the parts used so far. */
/* Offsets follow the 68000 layout (longs on 2-byte boundaries); the port will load this differently. */
typedef struct gds {
    int   path;             /* 0x000: video device path */
    int   unk004_sound;     /* 0x004: sound map id */
    char  unk008[0x10 - 0x8];
    long  afx[17];          /* 0x010: start offsets of the 16 audio effects, plus end marker */
    char  unk054[0x5a - 0x54];
    int   fct;              /* 0x05a: field control table id, plane A */
    int   fct_b;            /* 0x05e: field control table id, plane B */
    int   lct;              /* 0x062: line control table id */
    char  unk066[0x70 - 0x66];
    char *screen;           /* 0x070: frame buffer, 384 bytes per line */
    char  unk074[0xa0 - 0x74];
    char  clut[0x224 - 0xa0]; /* 0x0a0: palette data */
    long  afx_base;         /* 0x224: offset of the first audio effect */
    char  unk228[0x23c - 0x228];
    char *font;             /* 0x23c: font used by alphablit */
    long *afx_sizes;        /* 0x240: size of each audio effect */
    char  unk244[0x832 - 0x244];
    short sound_state;      /* 0x832: 2 while a sound map is still queued */
} GDS;

/* An open real-time file for the disc streaming ("ds") layer: 0x1c bytes, set to 0xff by ds_open. */
typedef struct ds_file {
    int   path;             /* 0x00: OS-9 path to the disc file */
    char *map;              /* 0x04: linked .map module */
    char  unk08[0x1c - 0x08];
} DS_FILE;

/* A stream buffer descriptor (0x16 bytes; ds_buffer_init fills it). */
typedef struct ds_buf {
    struct ds_buf *next;    /* 0x00: next descriptor in the ring */
    char  *buf;             /* 0x04: buffer */
    long   size;            /* 0x08: size in sectors (0 = take it from the map) */
    int  (*callback)();     /* 0x0c: called when the buffer is done (e.g. cd_terminate) */
    long   cbarg;           /* 0x10: its argument */
    char   ctrl, type;      /* 0x14: PCL control / coding byte of the last sector */
} DS_BUF;

/* A buffer set: the descriptor rings for video, audio and data (ds_buffer_set_init). */
typedef struct {
    DS_BUF *video, *audio, *data;
} DS_BUFSET;

/* Run-length decoder state for the stream maps (ds_init_decode / ds_decode_size). */
typedef struct {
    unsigned char *p;       /* 0x00: position in the map */
    char  unk4;
    char  literal;          /* 0x05: 1 = run of distinct bytes, 0 = one byte repeated */
    short count;            /* 0x06: bytes left in this run */
} DS_DECODE;

/* One entry of the deferred-call queue (ds_dispatch / ds_dispatch_execute). */
typedef struct {
    int  (*fn)();
    long arg;
} DS_DISPATCH;

/* disc streaming globals */
extern DS_FILE *DsFile;
extern short DsPcb;                 /* play control block status; bit 8 = playback finished */
extern short DsPcbError;            /* -$5e16(a6): error code after DsPcb (no symbol) */
extern short DsFlags;
extern int (*DsEorFunction)();
extern int (*DsTriggerFunction)();
extern int (*DsErrorFunction)();
extern DS_DISPATCH *DS_dispatch_Q_start, *DS_dispatch_Q_end, *DS_dispatch_Q_head, *DS_dispatch_Q_tail;

extern DS_FILE myrtf[];             /* real-time files of the game, indexed by cd_rtf */
extern DS_BUFSET mychan;            /* buffer set for ds_play */
extern DS_BUF mymem[3];             /* the three stream buffer descriptors */
extern long ds_index;
extern int  cd_rtf, cd_eors, cd_retry;
extern int (*termfunc)();

/* globals (a6-relative, names from the symbol table) */
extern char cd_play_flag;
extern char cd_trig_flag;
extern long cd_xbuf, cd_xend;       /* current audio buffer range */
extern long cd_lpstart, cd_lpend;   /* audio loop range */
extern long cd_trig;
extern GDS *gdsP;

/* game functions */
int  alphablit();
int  set_up_clut();
int  hm_error();
int  player_clut_change();
int  ds_dispatch_execute();

int  ds_init_channels();
int  ds_seek();
int  ds_play();
int  seekNplay();
int  dsTRIGcb();
int  dsERRORcb();
int  cd_terminate();
int  ds_buffer_init();
int  ds_buffer_set_init();
unsigned char *ds_init_decode();
int  dm();

/* CD-i system calls (CD-RTOS bindings) */
int  ss_cont();
int  ss_pause();
int  ss_abort();
int  sm_off();
int  gs_free();
int  _gs_pos();

/* CD-i display control (CD-RTOS) */
int  dc_wrli();
int  dc_wrfi();

/* OS-9 calls (Microware clib) */
char *srqcmem();
int  munlink();
long _getsys();
int  _sysdate();
int  _ev_link();
int  _ev_wait();
int  _ev_unlink();

#endif
