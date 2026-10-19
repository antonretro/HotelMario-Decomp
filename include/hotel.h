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
    int   fct;              /* 0x05a: field control table id */
    char  unk05e[0x62 - 0x5e];
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

/* disc streaming globals */
extern DS_FILE *DsFile;
extern short DsPcb;                 /* play control block status; bit 8 = playback finished */
extern short DsPcbError;            /* -$5e16(a6): error code after DsPcb (no symbol) */
extern short DsFlags;
extern int (*DsEorFunction)();
extern int (*DsTriggerFunction)();
extern int (*DsErrorFunction)();
extern char *DS_dispatch_Q_start, *DS_dispatch_Q_end, *DS_dispatch_Q_head, *DS_dispatch_Q_tail;

extern DS_FILE myrtf[];             /* real-time files of the game, indexed by cd_rtf */
extern char mychan[];               /* channel selection for ds_play */
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
int  _sysdate();
int  _ev_link();
int  _ev_wait();
int  _ev_unlink();

#endif
