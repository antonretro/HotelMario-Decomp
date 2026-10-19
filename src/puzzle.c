/* puzzle.c - per-floor puzzles (0xfc28) */
#include "hotel.h"

/* 0xfc28: unless the flag at +0xc is set, run the puzzle handler for the current entry's type (2-8; 6 has none).
   Entries are 0x1c bytes from game+0x7a6, indexed by game+0x596 (probably the current floor - not confirmed). */
puzzle(game, arg)
char *game;
short arg;
{
    char *floor = game + 0x1c * *(short *)(game + 0x596);

    if (*(short *)(floor + 0x7b2))
        return;
    switch (*(short *)(floor + 0x7a6)) {
    case 2: puzzle_2(game, arg); break;
    case 3: puzzle_3(game, arg); break;
    case 4: puzzle_4(game, arg); break;
    case 5: puzzle_5(game, arg); break;
    case 7: puzzle_7(game, arg); break;
    case 8: puzzle_8(game, arg); break;
    }
}
