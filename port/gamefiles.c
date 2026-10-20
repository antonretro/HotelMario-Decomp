/*
 * gamefiles.c - finding the files from your disc, so hotel.exe can be started by double-clicking it.
 *
 * In order:
 *   1. a folder given on the command line (hotel.exe path\to\files)
 *   2. build/eu/fs in the current folder (running from the repository, as the tools do)
 *   3. next to hotel.exe: the files themselves, or a "game" folder - a portable copy for yourself
 *   4. the repository hotel.exe was built in (build/port/hotel.exe -> build/eu/fs)
 *   5. the folder chosen last time (game_folder in hotel_settings.ini)
 *   6. otherwise ask once with a folder picker, and remember the answer
 * From 3 on, the game works in hotel.exe's folder (or the repository), so saves/ and hotel_settings.ini
 * stay with it however it was started. Windows only for now.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <direct.h>
#include <windows.h>
#include <shlobj.h>

#include "settings.h"

extern const char *fs_root;
extern int headless;

static char found[MAX_PATH + 32];

static int has_game(const char *dir)
{
    char p[MAX_PATH + 48];
    FILE *f;
    snprintf(p, sizeof p, "%s\\cdi_hotel", dir);
    if (!(f = fopen(p, "rb"))) return 0;
    fclose(f);
    return 1;
}

/* dir may hold the files, or be a folder above them: game\, fs\ or a repository's build\eu\fs */
static int use_folder(const char *dir)
{
    static const char *sub[] = { "", "\\game", "\\fs", "\\build\\eu\\fs" };
    int i;
    for (i = 0; i < 4; i++) {
        snprintf(found, sizeof found, "%s%s", dir, sub[i]);
        if (has_game(found)) { fs_root = found; return 1; }
    }
    return 0;
}

static int pick_folder(void)
{
    BROWSEINFOA bi;
    char name[MAX_PATH], path[MAX_PATH];
    LPITEMIDLIST id;
    CoInitialize(0);
    memset(&bi, 0, sizeof bi);
    bi.pszDisplayName = name;
    bi.lpszTitle = "Where are the files from your Hotel Mario disc?\n"
                   "(the folder with cdi_hotel in it, extracted by tools/setup.py)";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    while ((id = SHBrowseForFolderA(&bi))) {
        int ok = SHGetPathFromIDListA(id, path) && use_folder(path);
        CoTaskMemFree(id);
        if (ok) {
            snprintf(settings.game_folder, sizeof settings.game_folder, "%s", path);
            settings_save();
            return 1;
        }
        MessageBoxA(0, "That folder doesn't have the Hotel Mario files in it (cdi_hotel and the others from "
                       "the disc). Please choose another one.", "Hotel Mario", MB_ICONWARNING);
    }
    return 0;
}

void find_game_files(int given)
{
    char exe[MAX_PATH], repo[MAX_PATH], *slash;
    if (given) return;
    if (has_game(fs_root)) return;
    GetModuleFileNameA(0, exe, sizeof exe);
    if ((slash = strrchr(exe, '\\'))) *slash = 0;
    if (use_folder(exe)) { _chdir(exe); return; }
    snprintf(repo, sizeof repo, "%s\\..\\..", exe);
    if (_fullpath(repo, repo, sizeof repo) && use_folder(repo)) { _chdir(repo); return; }
    _chdir(exe);
    settings_load();
    if (settings.game_folder[0] && use_folder(settings.game_folder)) return;
    if (!headless && pick_folder()) return;
    if (!headless)
        MessageBoxA(0, "Hotel Mario needs the files from your own disc. Extract them with tools/setup.py, "
                       "or put them in a folder called \"game\" next to hotel.exe.", "Hotel Mario", MB_ICONINFORMATION);
    fprintf(stderr, "no game files found - run tools/setup.py with your disc image first\n");
    exit(1);
}
