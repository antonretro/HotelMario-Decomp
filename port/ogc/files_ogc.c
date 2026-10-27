/*
 * files_ogc.c - the Wii / GameCube side of gamefiles.c. Runs first: sets up the TV (for messages), the CD-i
 * memory, and the SD card. Everything lives in one folder:
 *   sd:/apps/hotelmario/boot.dol    the game (Homebrew Channel layout)
 *   sd:/apps/hotelmario/game/       the files from your disc (cdi_hotel and the rest)
 *   sd:/apps/hotelmario/saves/      saves, made when needed
 * On the Wii a USB stick (usb:/apps/hotelmario) works too.
 */
#include <gccore.h>
#include <fat.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "m68k.h"

extern const char *fs_root;
void ogc_video_init(void);

uint8_t *RAM;

static int has_game(const char *dir)
{
    char p[128];
    FILE *f;
    snprintf(p, sizeof p, "%s/game/cdi_hotel", dir);
    if (!(f = fopen(p, "rb"))) return 0;
    fclose(f);
    return 1;
}

static void stop(const char *msg)
{
    printf("\n\n  Hotel Mario\n\n  %s\n\n", msg);
#ifdef HW_RVL
    printf("  Press HOME on the Wii Remote, or wait, to go back.\n");
#endif
    sleep(15);
    exit(1);
}

void find_game_files(int given)
{
    static const char *bases[] = { "sd:/apps/hotelmario", "usb:/apps/hotelmario" };
    static char root[64];
    int i;
    ogc_video_init();
#ifdef HW_RVL
    /* the CD-i memory goes in MEM2, the Wii's 64 MB bank: MEM1 (24 MB) holds the program */
    RAM = (uint8_t *)SYS_GetArena2Lo();
    if ((uint8_t *)SYS_GetArena2Hi() - RAM < (long)RAM_SIZE) stop("Not enough memory.");
    SYS_SetArena2Lo(RAM + RAM_SIZE);
#else
    if (!(RAM = malloc(RAM_SIZE))) stop("Not enough memory for the game (GameCube support is not finished).");
#endif
    if (!fatInitDefault()) stop("No SD card (or USB stick) found.");
    if (given) return;
    for (i = 0; i < 2; i++)
        if (has_game(bases[i])) {
            chdir(bases[i]);
            snprintf(root, sizeof root, "%s/game", bases[i]);
            fs_root = root;
            return;
        }
    stop("The game needs the files from your own disc.\n\n"
         "  Copy the extracted files (cdi_hotel and the rest,\n"
         "  from build/eu/fs) to sd:/apps/hotelmario/game/");
}
