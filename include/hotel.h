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
    char  unk004[0x10 - 0x4];
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
} GDS;

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

/* CD-i display control (CD-RTOS) */
int  dc_wrli();
int  dc_wrfi();

/* OS-9 calls (Microware clib) */
int  _sysdate();
int  _ev_link();
int  _ev_wait();
int  _ev_unlink();

#endif
