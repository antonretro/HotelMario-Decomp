/* mario.c - player state handlers (0x96be-0xe2dc); each state is called with (game, player) */
#include "hotel.h"

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
