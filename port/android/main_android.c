/*
 * main_android.c - the Android TV side of the start-up: SDL calls SDL_main, which starts the game's main().
 *
 * The game files (your extracted disc, see tools/setup.py) go in /sdcard/HotelMario/game on the box; push
 * them there with adb (tools/push_android.py). The APK carries no game data.
 */
#include <SDL.h>

extern const char *fs_root;
extern int main(int argc, char **argv);

/* runtime.c calls this once the arguments are read; on Android the game folder is always the one above */
void find_game_files(int given)
{
    (void)given;
    fs_root = "/sdcard/HotelMario/game";
}

int SDL_main(int argc, char *argv[])
{
    fs_root = "/sdcard/HotelMario/game";
    return main(argc, argv);
}
