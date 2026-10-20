/* settings.h - the port's own options (none of them are in the original game); see settings.c */
#ifndef SETTINGS_H
#define SETTINGS_H

#include <stdint.h>

typedef struct {
    int fullscreen;
    int scale;          /* window size, times 384 x 260 */
    int tv_crop;        /* 0 = all 280 lines, 1 = edges (2-261), 2 = like a TV (2-241, 240 lines) */
    int smooth;         /* smooth (linear) scaling instead of sharp pixels */
    int monitor_rate;   /* redraw at the monitor's refresh rate instead of 50 Hz */
    int volume;         /* 0-10 */
    int skip_philips;   /* don't run the Philips logo program at start-up */
    int game_speed;     /* 0 = original 50 Hz (PAL), 1 = 60 Hz (NTSC pace), 2 = 75 Hz */
} Settings;

extern Settings settings;
extern int settings_cursor;

void settings_load(void);
void settings_save(void);
void settings_draw(uint32_t *fb, int w, int h, int top);
int settings_change(int d);
int settings_count(void);
int field_rate(void);   /* game fields per second from the game speed setting */

#endif
