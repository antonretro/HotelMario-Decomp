/*
 * window.c - SDL2 window for the port: shows the emulated CD-i display ~50 times a second and turns
 * keyboard / gamepad input into the CD-i pointer (thumbpad + 2 buttons) the game reads with pt_coord.
 *
 * Keys: arrows = move, Z or Space = button 1, X or Enter = button 2, F1 = settings, F11 = fullscreen,
 * F12 = save frame (debug), Esc = quit.
 * Gamepad: left stick / d-pad = move, A = button 1, B = button 2, Back/Select = settings.
 */
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "m68k.h"
#include "runtime.h"
#include "settings.h"

#define W 384
#define H 280

/* The visible area. The player puts out 280 lines (PAL), but the game switches its pictures on at line 2 and
   they run out after ~262 lines (below that the chip shows whatever memory follows), and a TV's bezel hid
   more still. settings.tv_crop: 0 = all 280 lines, 1 = edges only (2-261), 2 = like a TV (2-240, the
   default): the picture an NTSC player showed; line 241 is already the strip below the pictures. The intro and cutscene pictures are 240
   lines and end in a bar below that; the title and stages draw 20 more lines of PAL-only extra art.
   --full-frame shows all 280 whatever the setting. */
int full_frame;
static int vis_top = 2, vis_h = 239;

static SDL_Window *win;
static SDL_Renderer *ren;
static SDL_Texture *tex;
static SDL_GameController *pad;
static uint32_t frame[W * H];
static Uint64 next_frame;
static unsigned long refresh_count;   /* pictures presented so far (one per monitor refresh) */
static int refresh_hz = 60;
/* The CD-i pointer works in double resolution (768 x 560): the game centres it at (383, 234) in play and
   reads movement from there, so it must be able to go past the 384-pixel screen edge. */
#define PTR_W 768
#define PTR_H 560
static int ptr_x = PTR_W / 2, ptr_y = PTR_H / 2, ptr_buttons;
int headless;

static int cli_full_frame;   /* --full-frame given: always show all 280 lines */

/* Apply the port options (settings.c) to the window. */
static void apply_settings(int resize)
{
    SDL_DisplayMode mode;
    int vh;
    full_frame = cli_full_frame || !settings.tv_crop;
    vis_top = full_frame ? 0 : 2;
    vis_h = full_frame ? H : settings.tv_crop == 1 ? 260 : 239;
    vh = vis_h;
    SDL_SetWindowFullscreen(win, settings.fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
    if (resize && !settings.fullscreen) {
        SDL_SetWindowSize(win, W * settings.scale, vh * settings.scale);
        SDL_SetWindowPosition(win, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    }
    SDL_RenderSetLogicalSize(ren, W, vh);
    SDL_SetTextureScaleMode(tex, settings.smooth ? SDL_ScaleModeLinear : SDL_ScaleModeNearest);
    /* redraw at the monitor's refresh rate (60 Hz or more), or 50 Hz like the PAL player; the game itself
       always keeps its 50 Hz timing */
    refresh_hz = 50;
    if (settings.monitor_rate && SDL_GetCurrentDisplayMode(SDL_GetWindowDisplayIndex(win), &mode) == 0 &&
        mode.refresh_rate >= 50)
        refresh_hz = mode.refresh_rate;
    audio_set_volume(settings.volume);
}

void window_init(void)
{
    settings_load();
    cli_full_frame = full_frame;
    if (headless) { audio_init(); return; }    /* no device, but --wav still records */
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS) < 0) {
        fprintf(stderr, "SDL: %s - running without a window\n", SDL_GetError());
        headless = 1;
        return;
    }
    win = SDL_CreateWindow("Hotel Mario (CD-i) - decomp port  [F1: settings]", SDL_WINDOWPOS_CENTERED,
                           SDL_WINDOWPOS_CENTERED, W * settings.scale, 239 * settings.scale, SDL_WINDOW_RESIZABLE);
    ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING, W, H);
    audio_init();
    apply_settings(1);
    fprintf(stderr, "window redraws at %d Hz\n", refresh_hz);
}

static void present(const uint32_t *fb)
{
    SDL_Rect vis = { 0, vis_top, W, vis_h };
    SDL_UpdateTexture(tex, 0, fb, W * 4);
    SDL_RenderClear(ren);
    SDL_RenderCopy(ren, tex, &vis, 0);
    SDL_RenderPresent(ren);
}

/* The settings menu: the game is paused (nothing returns to it) until the menu closes. */
static void settings_menu(void)
{
    static uint32_t fb[W * H];
    int open = 1, axis_held = 0;
    audio_pause(1);
    settings_cursor = 0;
    while (open) {
        SDL_Event e;
        int d = 0, move = 0;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) { settings_save(); exit(0); }
            if (e.type == SDL_KEYDOWN) {
                switch (e.key.keysym.sym) {
                case SDLK_F1: case SDLK_ESCAPE: open = 0; break;
                case SDLK_UP: move = -1; break;
                case SDLK_DOWN: move = 1; break;
                case SDLK_LEFT: d = -1; break;
                case SDLK_RIGHT: case SDLK_RETURN: case SDLK_SPACE: d = 1; break;
                }
            }
            if (e.type == SDL_CONTROLLERBUTTONDOWN) {
                switch (e.cbutton.button) {
                case SDL_CONTROLLER_BUTTON_BACK: case SDL_CONTROLLER_BUTTON_START: case SDL_CONTROLLER_BUTTON_B:
                    open = 0; break;
                case SDL_CONTROLLER_BUTTON_DPAD_UP: move = -1; break;
                case SDL_CONTROLLER_BUTTON_DPAD_DOWN: move = 1; break;
                case SDL_CONTROLLER_BUTTON_DPAD_LEFT: d = -1; break;
                case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: case SDL_CONTROLLER_BUTTON_A: d = 1; break;
                }
            }
        }
        if (pad) {                                  /* the left stick, one step per push */
            int ay = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTY);
            if (!axis_held && (ay < -16000 || ay > 16000)) { move = ay < 0 ? -1 : 1; axis_held = 1; }
            if (ay > -8000 && ay < 8000) axis_held = 0;
        }
        if (move) settings_cursor = (settings_cursor + move + settings_count()) % settings_count();
        if (d && settings_change(d)) apply_settings(1);
        memcpy(fb, frame, sizeof fb);
        settings_draw(fb, W, H, vis_top);
        present(fb);
        SDL_Delay(16);
    }
    settings_save();
    audio_pause(0);
    next_frame = 0;
}

static void read_input(void)
{
    SDL_Event e;
    const Uint8 *k;
    int dx = 0, dy = 0, b = 0;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT || (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE)) {
            if (trace) display_report();
            exit(0);
        }
        if (e.type == SDL_CONTROLLERDEVICEADDED && !pad) pad = SDL_GameControllerOpen(e.cdevice.which);
        if ((e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_F1) ||
            (e.type == SDL_CONTROLLERBUTTONDOWN && e.cbutton.button == SDL_CONTROLLER_BUTTON_BACK)) {
            settings_menu();
            return;
        }
        if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_F11) {   /* quick fullscreen toggle */
            settings.fullscreen = !settings.fullscreen;
            apply_settings(1);
            settings_save();
        }
        if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_F12) {   /* save the frame for debugging */
            dump_at_ms = 1;
            display_maybe_dump();
        }
    }
    k = SDL_GetKeyboardState(0);
    if (k[SDL_SCANCODE_LEFT]) dx -= 3;
    if (k[SDL_SCANCODE_RIGHT]) dx += 3;
    if (k[SDL_SCANCODE_UP]) dy -= 3;
    if (k[SDL_SCANCODE_DOWN]) dy += 3;
    if (k[SDL_SCANCODE_Z] || k[SDL_SCANCODE_SPACE]) b |= 1;
    if (k[SDL_SCANCODE_X] || k[SDL_SCANCODE_RETURN]) b |= 2;
    if (pad) {
        int ax = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTX);
        int ay = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTY);
        if (ax < -8000 || SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_LEFT)) dx -= 3;
        if (ax > 8000 || SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_RIGHT)) dx += 3;
        if (ay < -8000 || SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_UP)) dy -= 3;
        if (ay > 8000 || SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_DOWN)) dy += 3;
        if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_A)) b |= 1;
        if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_B)) b |= 2;
    }
    ptr_x += dx; ptr_y += dy;
    ptr_x = ptr_x < 0 ? 0 : ptr_x >= PTR_W ? PTR_W - 1 : ptr_x;
    ptr_y = ptr_y < 0 ? 0 : ptr_y >= PTR_H ? PTR_H - 1 : ptr_y;
    ptr_buttons = b;
}

void window_tick(void)
{
    Uint64 now, period;
    os9_tick();
    if (headless || !win) return;
    now = SDL_GetPerformanceCounter();
    period = SDL_GetPerformanceFrequency() / (Uint64)refresh_hz;
    if (now < next_frame) return;
    next_frame = (now - next_frame < period) ? next_frame + period : now + period;   /* steady, no drift */
    read_input();
    {   /* draw a new picture only when the game has finished one; otherwise show the last one again */
        static unsigned seen;
        static unsigned long last_ms;
        static unsigned pictures, syncs0;
        static Uint64 t0;
        if (display_frame_due(&seen, &last_ms)) { display_render(frame); pictures++; }
        if (getenv("HM_PACELOG")) {       /* debug: new pictures and frame syncs per second, in the window */
            if (!t0) { t0 = now; syncs0 = frame_syncs; }
            if (now - t0 >= 2 * SDL_GetPerformanceFrequency()) {
                double s = (double)(now - t0) / (double)SDL_GetPerformanceFrequency();
                fprintf(stderr, "  [window] at %lu ms: %.1f new pictures/s, %.1f frame syncs/s\n",
                        (unsigned long)(clock() * 1000 / CLOCKS_PER_SEC), pictures / s, (frame_syncs - syncs0) / s);
                t0 = now; pictures = 0; syncs0 = frame_syncs;
            }
        }
    }
    present(frame);
    refresh_count++;
}

static unsigned long refresh_count_value(void);

/* Smooth pacing (settings): run the game one field every k monitor refreshes, k chosen so the field rate is
   as close as possible to the game speed (144 Hz: k = 3, 48 fields a second; 60 Hz: k = 1). Every game
   frame then stays on screen for the same time. 0 = off (no window, or the option is off). */
int window_refreshes_per_field(void)
{
    int k;
    if (headless || !win || !settings.smooth_pacing) return 0;
    k = (refresh_hz + field_rate() / 2) / field_rate();
    return k < 1 ? 1 : k;
}

unsigned long window_refresh_count(void) { return refresh_count_value(); }
static unsigned long refresh_count_value(void) { return refresh_count; }

void window_sleep(int ms)
{
    if (headless || !win) {
#ifdef _WIN32
        extern void __stdcall Sleep(unsigned long);
        Sleep((unsigned long)ms);
#endif
        return;
    }
    SDL_Delay((Uint32)ms);
}

void window_set_pointer(int x, int y)
{
    ptr_x = x < 0 ? 0 : x >= PTR_W ? PTR_W - 1 : x;
    ptr_y = y < 0 ? 0 : y >= PTR_H ? PTR_H - 1 : y;
}

void window_pointer(int *x, int *y, int *buttons)
{
    int scripted;
    window_tick();
    scripted = input_script ? script_buttons(&ptr_x, &ptr_y) : 0;
    *x = ptr_x;
    *y = ptr_y;
    *buttons = ptr_buttons | scripted;
}
