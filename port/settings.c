/*
 * settings.c - the port's own options, kept apart from the game. Everything here is a feature the original
 * CD-i release didn't have, so each one can be switched off, and the defaults stay close to the original.
 *
 * F1 (or the gamepad's Back / Select button) pauses the game and opens the menu over it:
 * up/down choose, left/right/Enter change, F1 or Esc closes. Choices are saved in hotel_settings.ini.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "settings.h"

Settings settings = {
    0,      /* fullscreen */
    2,      /* window scale */
    2,      /* crop: 0 none, 1 edges, 2 like a TV (what the game was seen through) */
    0,      /* smooth scaling (the original is sharp pixels on a TV) */
    1,      /* redraw at the monitor's refresh rate */
    10,     /* volume 0-10 */
    0,      /* skip the Philips logo */
    0,      /* game speed: original 50 Hz */
};

/* Game fields per second. The original PAL game updates 50 times a second; the faster settings only speed up
   the game's own frame timer - cutscenes, music and speech follow the disc and stay in sync. */
int field_rate(void)
{
    return settings.game_speed == 2 ? 75 : settings.game_speed == 1 ? 60 : 50;
}

static const char *ini = "hotel_settings.ini";

static struct { const char *key; int *val, lo, hi; } fields[] = {
    { "fullscreen", &settings.fullscreen, 0, 1 },
    { "scale", &settings.scale, 1, 5 },
    { "tv_crop", &settings.tv_crop, 0, 2 },
    { "smooth", &settings.smooth, 0, 1 },
    { "monitor_rate", &settings.monitor_rate, 0, 1 },
    { "volume", &settings.volume, 0, 10 },
    { "skip_philips_logo", &settings.skip_philips, 0, 1 },
    { "game_speed", &settings.game_speed, 0, 2 },
};
#define NFIELDS (int)(sizeof fields / sizeof fields[0])

void settings_load(void)
{
    char line[128], key[64];
    int v, i;
    FILE *f = fopen(ini, "r");
    if (!f) return;
    while (fgets(line, sizeof line, f))
        if (sscanf(line, " %63[a-z_] = %d", key, &v) == 2)
            for (i = 0; i < NFIELDS; i++)
                if (!strcmp(key, fields[i].key))
                    *fields[i].val = v < fields[i].lo ? fields[i].lo : v > fields[i].hi ? fields[i].hi : v;
    fclose(f);
}

void settings_save(void)
{
    int i;
    FILE *f = fopen(ini, "w");
    if (!f) return;
    fprintf(f, "# Hotel Mario port options - none of these are in the original game\n");
    for (i = 0; i < NFIELDS; i++) fprintf(f, "%s = %d\n", fields[i].key, *fields[i].val);
    fclose(f);
}

/* ---- the menu ---- */

/* 5x7 font, one row per byte (bit 4 = left column), for the characters the menu uses */
static const struct { char c; unsigned char rows[7]; } font[] = {
    { ' ', { 0, 0, 0, 0, 0, 0, 0 } },
    { '0', { 14, 17, 19, 21, 25, 17, 14 } }, { '1', { 4, 12, 4, 4, 4, 4, 14 } },
    { '2', { 14, 17, 1, 2, 4, 8, 31 } }, { '3', { 30, 1, 1, 14, 1, 1, 30 } },
    { '4', { 2, 6, 10, 18, 31, 2, 2 } }, { '5', { 31, 16, 30, 1, 1, 17, 14 } },
    { '6', { 6, 8, 16, 30, 17, 17, 14 } }, { '7', { 31, 1, 2, 4, 8, 8, 8 } },
    { '8', { 14, 17, 17, 14, 17, 17, 14 } }, { '9', { 14, 17, 17, 15, 1, 2, 12 } },
    { 'A', { 14, 17, 17, 31, 17, 17, 17 } }, { 'B', { 30, 17, 17, 30, 17, 17, 30 } },
    { 'C', { 14, 17, 16, 16, 16, 17, 14 } }, { 'D', { 30, 17, 17, 17, 17, 17, 30 } },
    { 'E', { 31, 16, 16, 30, 16, 16, 31 } }, { 'F', { 31, 16, 16, 30, 16, 16, 16 } },
    { 'G', { 14, 17, 16, 23, 17, 17, 15 } }, { 'H', { 17, 17, 17, 31, 17, 17, 17 } },
    { 'I', { 14, 4, 4, 4, 4, 4, 14 } }, { 'J', { 7, 2, 2, 2, 2, 18, 12 } },
    { 'K', { 17, 18, 20, 24, 20, 18, 17 } }, { 'L', { 16, 16, 16, 16, 16, 16, 31 } },
    { 'M', { 17, 27, 21, 21, 17, 17, 17 } }, { 'N', { 17, 17, 25, 21, 19, 17, 17 } },
    { 'O', { 14, 17, 17, 17, 17, 17, 14 } }, { 'P', { 30, 17, 17, 30, 16, 16, 16 } },
    { 'Q', { 14, 17, 17, 17, 21, 18, 13 } }, { 'R', { 30, 17, 17, 30, 20, 18, 17 } },
    { 'S', { 15, 16, 16, 14, 1, 1, 30 } }, { 'T', { 31, 4, 4, 4, 4, 4, 4 } },
    { 'U', { 17, 17, 17, 17, 17, 17, 14 } }, { 'V', { 17, 17, 17, 17, 17, 10, 4 } },
    { 'W', { 17, 17, 17, 21, 21, 21, 10 } }, { 'X', { 17, 17, 10, 4, 10, 17, 17 } },
    { 'Y', { 17, 17, 10, 4, 4, 4, 4 } }, { 'Z', { 31, 1, 2, 4, 8, 16, 31 } },
    { ':', { 0, 12, 12, 0, 12, 12, 0 } }, { '-', { 0, 0, 0, 14, 0, 0, 0 } },
    { '.', { 0, 0, 0, 0, 0, 12, 12 } }, { '/', { 1, 1, 2, 4, 8, 16, 16 } },
    { '<', { 2, 4, 8, 16, 8, 4, 2 } }, { '>', { 8, 4, 2, 1, 2, 4, 8 } },
    { '(', { 2, 4, 8, 8, 8, 4, 2 } }, { ')', { 8, 4, 2, 2, 2, 4, 8 } },
    { 'x', { 0, 0, 17, 10, 4, 10, 17 } }, { '\'', { 4, 4, 8, 0, 0, 0, 0 } },
};

static void glyph(uint32_t *fb, int w, int h, int x, int y, char c, uint32_t col)
{
    int i, r, b;
    if (c >= 'a' && c <= 'z' && c != 'x') c = (char)(c - 32);
    for (i = 0; i < (int)(sizeof font / sizeof font[0]); i++)
        if (font[i].c == c) {
            for (r = 0; r < 7; r++)
                for (b = 0; b < 5; b++)
                    if (font[i].rows[r] >> (4 - b) & 1 && x + b >= 0 && x + b < w && y + r >= 0 && y + r < h)
                        fb[(y + r) * w + x + b] = col;
            return;
        }
}

static void text(uint32_t *fb, int w, int h, int x, int y, const char *s, uint32_t col)
{
    for (; *s; s++, x += 6) glyph(fb, w, h, x, y, *s, col);
}

static const char *labels[] = {
    "FULLSCREEN", "WINDOW SIZE", "PICTURE CROP", "SMOOTH SCALING", "REDRAW AT MONITOR RATE",
    "VOLUME", "SKIP PHILIPS LOGO", "GAME SPEED",
};

int settings_cursor;

/* Draw the menu over a 384 x h frame: a darkened game picture with the options in the middle. */
void settings_draw(uint32_t *fb, int w, int h, int top)
{
    int i, y0 = top + 40;
    for (i = 0; i < w * h; i++) fb[i] = (fb[i] >> 2) & 0x3F3F3F;          /* dim the game */
    text(fb, w, h, 120, y0 - 24, "PORT SETTINGS", 0xFFD860);
    text(fb, w, h, 72, y0 - 12, "EXTRAS THAT ARE NOT IN THE ORIGINAL", 0x9090A0);
    for (i = 0; i < NFIELDS; i++) {
        char val[16];
        int y = y0 + i * 14;
        uint32_t col = i == settings_cursor ? 0xFFFFFF : 0xA0A0B0;
        if (fields[i].hi == 1) strcpy(val, *fields[i].val ? "ON" : "OFF");
        else if (i == 1) snprintf(val, sizeof val, "%dx", *fields[i].val);
        else if (i == 2) strcpy(val, *fields[i].val == 0 ? "NONE" : *fields[i].val == 1 ? "EDGES" : "TV");
        else if (i == 7) strcpy(val, *fields[i].val == 0 ? "ORIGINAL" : *fields[i].val == 1 ? "60HZ" : "75HZ");
        else snprintf(val, sizeof val, "%d", *fields[i].val);
        if (i == settings_cursor) text(fb, w, h, 30, y, ">", 0xFFD860);
        text(fb, w, h, 40, y, labels[i], col);
        text(fb, w, h, 280, y, "<", col);
        text(fb, w, h, 290, y, val, col);
        text(fb, w, h, 290 + 6 * (int)strlen(val) + 4, y, ">", col);
    }
    text(fb, w, h, 40, y0 + NFIELDS * 14 + 10, "UP/DOWN CHOOSE  LEFT/RIGHT CHANGE  F1 CLOSE", 0x9090A0);
}

/* Change the highlighted option by d (-1/+1); returns 1 if anything changed. */
int settings_change(int d)
{
    int *v = fields[settings_cursor].val, lo = fields[settings_cursor].lo, hi = fields[settings_cursor].hi;
    int n = *v + d;
    if (hi == 1) n = !*v;                          /* on/off toggles flip either way */
    if (n < lo || n > hi) return 0;
    *v = n;
    return 1;
}

int settings_count(void) { return NFIELDS; }
