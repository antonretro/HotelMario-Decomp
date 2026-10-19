/* mario.c - player state handlers (0x96be-0xe2dc); each state is called with (game, player) */
#include "hotel.h"

/* 0x9668: copy the screen rows from 0x48 above the player's floor into the shadow buffer (0x6c00 bytes) */
shadow_copy(game)
char *game;
{
    char *player = *(char **)(game + 0x250);
    long *src, *dst, *end;

    src = (long *)(*(char **)(game + 0x84)
          + (unsigned long)(*(short *)(game + 0x49c + 2 * *(short *)(player + 0x112)) - 0x48) * 0x180);
    end = (long *)(*(char **)(game + 0x8c) + 0x6c00);
    for (dst = *(long **)(game + 0x8c); dst < end;)
        *dst++ = *src++;
}

/* 0xa89c: shrinking/standing, facing forward - count down, then go to state 6; redraw and call the draw hook */
movi_sh_st_fo(player, game)
char *player, *game;
{
    short x = *(short *)(player + 0x10e);
    short y = *(short *)(player + 0x110);

    if (--*(short *)(player + 0x14c) == 0)
        *(short *)(player + 0x11e) = 6;
    dm(x, y - 1, *(char **)(game + 0x84), x, 0x29, *(char **)(game + 0x8c));
    (**(int (**)())(player + 0xd8))(*(char **)(game + 0x84) + 0x180 * y + x);
}

/* 0xa904: the same facing backward; goes to state 7 and uses the hook at 0xdc */
movi_sh_st_ba(player, game)
char *player, *game;
{
    short x = *(short *)(player + 0x10e);
    short y = *(short *)(player + 0x110);

    if (--*(short *)(player + 0x14c) == 0)
        *(short *)(player + 0x11e) = 7;
    dm(x, y - 1, *(char **)(game + 0x84), x, 0x29, *(char **)(game + 0x8c));
    (**(int (**)())(player + 0xdc))(*(char **)(game + 0x84) + 0x180 * y + x);
}

/* 0xa96c: standing still - redraw the 32x31 background block under the player, then call its draw hook */
movi_stan(player, game)
char *player, *game;
{
    short x = *(short *)(player + 0x10e);
    short y = *(short *)(player + 0x110);

    dm(x, y - 1, *(char **)(game + 0x84), x, 0x29, *(char **)(game + 0x8c));
    (**(int (**)())(player + 0x64))(*(char **)(game + 0x84) + 0x180 * y + x);
}

/* 0xa534: the "dead, being" state does nothing */
dead_bein(game, player)
char *game, *player;
{
}

/* 0xcbb0: waiting - restore the background under the player for 6 frames; in game mode 4, then switch to state 2/0x1d */
in_do_wait(player, game)
char *player, *game;
{
    if (*(short *)(player + 0x140))
        dm(*(short *)(player + 0x10e),
           *(short *)(game + 0x49c + 2 * *(short *)(player + 0x112)) - 0x1f,
           *(char **)(game + 0x84), 0x110, 0, *(char **)(game + 0x90));
    if (++*(short *)(player + 0x108) <= 5)
        return 1;
    if (*(long *)(game + 0x480) == 4) {
        *(short *)(player + 0x11e) = 2;
        *(short *)(player + 0x120) = 0x1d;
        *(short *)(player + 0x108) = 0;
    }
    return 4;
}

/* 0xe1c4: last frame of the dying sequence does nothing */
dyin_end(game, player)
char *game, *player;
{
}

/* 0xe290: demo mode stands in for pt_coord - the next three values come from the recorded input */
demoHelp_pt_coord(unused, a, b, c)
int unused;
long *a, *b, *c;
{
    *a = *gdsP->demo_data++;
    *b = *gdsP->demo_data++;
    *c = *gdsP->demo_data++;
}
