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
typedef struct gds {
    char  unk000[0x10];
    long  afx[17];          /* 0x010: start offsets of the 16 audio effects, plus end marker */
    char  unk054[0x224 - 0x54];
    long  afx_base;         /* 0x224: offset of the first audio effect */
    char  unk228[0x240 - 0x228];
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
int  hm_error();
int  player_clut_change();
int  ds_dispatch_execute();

/* OS-9 event calls (Microware clib) */
int  _ev_link();
int  _ev_wait();
int  _ev_unlink();

#endif
