/* colors.c - palette cycling for the player (0x1d42-0x23ce) */
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
