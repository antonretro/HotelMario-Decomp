/* runtime.h - shared by the port's runtime files */
#ifndef RUNTIME_H
#define RUNTIME_H

#include <stdint.h>

extern const char *fs_root;   /* folder with the files extracted from your disc */
extern int trace;
extern unsigned press_every;
extern int headless;                  /* --headless: no window (for automated runs) */

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

/* display.c - CD-i video chip state from the game's dc_* calls */
void display_setstt(void);
void display_render(uint32_t *out);   /* 384x280, 0x00RRGGBB */
void display_report(void);

/* window.c - SDL window, frame pacing and input */
void window_init(void);
void window_tick(void);               /* called often; redraws ~50 times a second, reads input */
void window_pointer(int *x, int *y, int *buttons);
void window_sleep(int ms);

#endif
