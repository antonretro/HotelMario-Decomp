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
void interp_reset(void);           /* after a child program left the interpreter via F$Exit */
void interp_signal(uint32_t sig, uint32_t handler, uint32_t data);  /* signal for interpreted code */
void interp_rte(void);             /* F$RTE from an interpreted intercept routine */
void cdrtos_get_intercept(uint32_t *handler, uint32_t *data);
extern int signal_flag;
extern uint32_t field_signal;      /* display.c: signal to send every field, 0 = none */

/* cdrtos.c - CD-RTOS real-time playback and signal delivery */
void cdrtos_set_intercept(uint32_t handler, uint32_t data);
void cdrtos_play(FILE *f, uint32_t pcb);
void cdrtos_stop(void);
void cdrtos_pause(int paused);
void cdrtos_rte(void);
void cdrtos_kill(void);

/* display.c - CD-i video chip state from the game's dc_* calls */
void display_setstt(void);
void display_render(uint32_t *out);   /* 384x280, 0x00RRGGBB */
void display_report(void);
void display_note_video(uint32_t buf, uint32_t bytes, int sector_format);  /* 4 = RL7, 5 = DYUV ... */
void display_maybe_dump(void);
void display_save_tables(void);
void display_restore_tables(void);
extern unsigned dump_at_ms;

/* window.c - SDL window, frame pacing and input */
void window_init(void);
void window_tick(void);               /* called often; redraws ~50 times a second, reads input */
void window_pointer(int *x, int *y, int *buttons);
void window_sleep(int ms);

/* audio.c - streamed CD-i ADPCM audio through SDL */
void audio_init(void);
void audio_sector(const uint8_t *payload, int coding);
void audio_stop(void);
unsigned long audio_sector_count(void);

#endif
