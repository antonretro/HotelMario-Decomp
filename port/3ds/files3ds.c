/*
 * files3ds.c - the 3DS side of gamefiles.c. Everything lives on the SD card:
 *   sdmc:/3ds/HotelMario/HotelMario.3dsx   the game
 *   sdmc:/3ds/HotelMario/game/              the files from your disc (cdi_hotel and the rest)
 *   sdmc:/3ds/HotelMario/saves/             saves, made when needed
 * If the files aren't there, say so on the bottom screen and wait for START.
 */
#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

extern const char *fs_root;

#define BASE "sdmc:/3ds/HotelMario"

void find_game_files(int given)
{
    FILE *f;
    if (given) return;
    chdir(BASE);
    fs_root = BASE "/game";
    if ((f = fopen(BASE "/game/cdi_hotel", "rb"))) { fclose(f); return; }
    gfxInitDefault();                   /* runs before window_init: set up a screen for the message */
    consoleInit(GFX_BOTTOM, 0);
    printf("\nHotel Mario needs the files from your own disc.\n\n"
           "Copy the extracted files (cdi_hotel and the rest,\n"
           "from build/eu/fs) to:\n  " BASE "/game/\n\nPress START to quit.\n");
    while (aptMainLoop()) {
        hidScanInput();
        if (hidKeysDown() & KEY_START) break;
        gspWaitForVBlank();
    }
    exit(1);
}
