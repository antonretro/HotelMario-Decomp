/* mario.c - player state handlers (0x96be-0xe2dc); each state is called with (game, player) */
#include "hotel.h"

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

/* 0xe1c4: last frame of the dying sequence does nothing */
dyin_end(game, player)
char *game, *player;
{
}
