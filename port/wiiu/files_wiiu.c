/*
 * files_wiiu.c - Wii U file resolution for Hotel Mario.
 * Locates extracted disc files (cdi_hotel and *.rtf) from:
 *   1. fs:/vol/content/game or fs:/vol/content (if bundled in .wuhb)
 *   2. fs:/vol/external01/wiiu/apps/hotelmario/game
 *   3. sd:/wiiu/apps/hotelmario/game
 *   4. build/eu/fs or game/ (local test / Cemu)
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/stat.h>

extern const char *fs_root;

static const char *candidates[] = {
    "fs:/vol/content/game",
    "fs:/vol/content",
    "fs:/vol/external01/wiiu/apps/hotelmario/game",
    "fs:/vol/external01/wiiu/apps/hotelmario",
    "sd:/wiiu/apps/hotelmario/game",
    "sd:/wiiu/apps/hotelmario",
    "build/eu/fs",
    "game",
    NULL
};

void find_game_files(int given)
{
    if (given && fs_root) return;

    for (int i = 0; candidates[i]; i++) {
        char test[256];
        snprintf(test, sizeof(test), "%s/cdi_hotel", candidates[i]);
        FILE *f = fopen(test, "rb");
        if (f) {
            fclose(f);
            fs_root = candidates[i];
            fprintf(stderr, "[wiiu] Found game files at: %s\n", fs_root);
            if (strstr(candidates[i], "hotelmario")) {
                chdir("fs:/vol/external01/wiiu/apps/hotelmario");
            }
            return;
        }
    }

    fprintf(stderr, "[wiiu] Notice: cdi_hotel not found in standard paths, defaulting to fs:/vol/content\n");
    fs_root = "fs:/vol/content";
}
