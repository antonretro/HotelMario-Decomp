/* colors.c - palette cycling for the player (0x1d42-0x2478) */
#include "hotel.h"

static short sparkle_color;   /* -$7d48(a6) */
static short sparkle_timer;   /* -$7d46(a6) */

/* 0x1e34: mode 0 resets; mode 1 blinks between 2 colours each frame; otherwise steps 0,2,4->1,3,5->0 every 3 frames */
sparkle_color_cycle(player, mode)
char *player;
short mode;
{
    if (mode == 0) {
        sparkle_color = 0;
        sparkle_timer = -1;
        return 0;
    }
    if (mode == 1) {
        if (++sparkle_timer != 1)
            return;
        sparkle_timer = -1;
        if (++sparkle_color == 2)
            sparkle_color = 0;
    } else {
        if (++sparkle_timer != 3)
            return;
        sparkle_timer = -1;
        sparkle_color += 2;
        if (sparkle_color == 4)
            sparkle_color = 1;
        if (sparkle_color == 5)
            sparkle_color = 0;
    }
    player_clut_change(player, sparkle_color);
}

/* -$669c(a6), no symbol: five shades of green (r, g, b), light to dark */
static short luigi_hat_rgb[5][3] = {
    { 15, 234, 15 }, { 12, 191, 12 }, { 12, 148, 12 }, { 12, 102, 12 }, { 12, 58, 12 },
};

/* 0x23ce: load the green shades into colour registers 0x10-0x14 through FCT entries 0x66-0x6a */
luigi_hat_spit(g)
GDS *g;
{
    long fct[5];
    register short i;

    for (i = 0; i < 5; i++)
        fct[i] = 0x80000000 + ((long)((i + 0x50) & 0x3f) << 24)
               | (long)(unsigned char)luigi_hat_rgb[i][0] << 16
               | (long)(unsigned char)luigi_hat_rgb[i][1] << 8
               | (long)(unsigned char)luigi_hat_rgb[i][2];
    dc_wrfct(g->path, g->fct, 0x66, 5, fct);
}
