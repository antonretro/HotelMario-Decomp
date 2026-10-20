/* runtime.h - shared by the port's runtime files */
#ifndef RUNTIME_H
#define RUNTIME_H

#include <stdint.h>

extern const char *fs_root;   /* folder with the files extracted from your disc */
extern int trace;
extern unsigned press_every;
extern int headless;                  /* --headless: no window (for automated runs) */
extern int full_frame;                /* --full-frame: show all 280 lines, not the TV-visible area */

#include <stdio.h>
#include "m68k.h"

void find_game_files(int given);      /* gamefiles.c: locate the disc files, ask once if needed */
void os9_init(void);
extern uint8_t program_copy[];        /* the program as loaded, for checking it wasn't overwritten */
extern uint32_t program_size;
void os9_tick(void);               /* update the tick counter in the system globals */
#include <time.h>
#ifdef __3DS__
/* The 3DS C library's clock() isn't wall time; use milliseconds from the system clock (3ds/window3ds.c). */
#undef CLOCKS_PER_SEC
#define CLOCKS_PER_SEC 1000
clock_t port_clock(void);
#define clock() port_clock()
#endif
clock_t game_clock(void);          /* real time minus main-loop stalls (window drags, the settings menu) */
void game_heartbeat(void);
RecompFn recomp_lookup(uint32_t addr);
void interp_call(uint32_t addr);   /* interp.c: run non-recompiled code on Musashi */
void interp_dump_history(void);    /* --trace: last interpreted instructions */
void interp_reset(void);         /* after a child program left the interpreter via F$Exit */
void interp_signal(uint32_t sig, uint32_t handler, uint32_t data);  /* signal for interpreted code */
void interp_rte(void);             /* F$RTE from an interpreted intercept routine */
void cdrtos_get_intercept(uint32_t *handler, uint32_t *data);
extern int signal_flag;
extern uint32_t field_signal;      /* signal to send every field, 0 = none */
extern uint32_t once_signal;       /* dc_ssig: signal to send at the next field only, 0 = none */

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
void display_note_video(uint32_t buf, uint32_t bytes, int sector_format);  /* 4 = RL7, 5 = DYUV ... */void display_maybe_dump(void);
void display_save_tables(void);
void display_restore_tables(void);
extern unsigned dump_at_ms, dump_every_ms;
/* Frames are shown only when the game has finished drawing one (it waits for the next field), like the
   real beam timing, or after 100 ms without such a wait. Otherwise the window can catch the game halfway
   through erasing and redrawing its sprites (the HUD and enemies flickered). */
extern unsigned frame_syncs;
int display_frame_due(unsigned *seen, unsigned long *last_ms);

/* window.c - SDL window, frame pacing and input */
void window_init(void);
void window_tick(void);               /* called often; redraws ~50 times a second, reads input */
void window_pointer(int *x, int *y, int *buttons);
void window_set_pointer(int x, int y);  /* pt_pos */
extern const char *input_script;      /* --input "ms:action,...": scripted input for headless tests */
int script_buttons(int *ptr_x, int *ptr_y);   /* inputscript.c: apply it; returns buttons held */
void window_sleep(int ms);
int window_refreshes_per_field(void); /* locked pacing: monitor refreshes per game field, 0 = off */
unsigned long window_refresh_count(void);

/* audio.c - streamed CD-i ADPCM audio through SDL */
void audio_init(void);
extern const char *wav_path;          /* --wav <file> */
void audio_sector(const uint8_t *payload, int coding);
void audio_stop(void);

/* soundmap.c - CD-i sound maps (the stage music and sound effects) */
int soundmap_setstt(uint32_t fn);
int soundmap_getstt(uint32_t fn);
void soundmap_filled(uint32_t buf);
void soundmap_poll(void);
uint32_t os9_alloc(uint32_t size);
void audio_set_volume(int v);         /* 0-10 */
void audio_pause(int paused);
unsigned long audio_sector_count(void);

#endif
