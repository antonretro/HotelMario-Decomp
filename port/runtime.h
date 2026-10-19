/* runtime.h - shared by the port's runtime files */
#ifndef RUNTIME_H
#define RUNTIME_H

#include <stdint.h>

extern const char *fs_root;   /* folder with the files extracted from your disc */
extern int trace;
extern unsigned press_every;

#include <stdio.h>
#include "m68k.h"

void os9_init(void);
RecompFn recomp_lookup(uint32_t addr);
void interp_call(uint32_t addr);   /* interp.c: run non-recompiled code on Musashi */

/* cdrtos.c - CD-RTOS real-time playback and signal delivery */
void cdrtos_set_intercept(uint32_t handler, uint32_t data);
void cdrtos_play(FILE *f, uint32_t pcb);
void cdrtos_stop(void);
void cdrtos_pause(int paused);
void cdrtos_rte(void);

#endif
