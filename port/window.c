/*
 * window.c - SDL2 window for the port: shows the emulated CD-i display ~50 times a second and turns
 * keyboard / gamepad input into the CD-i pointer (thumbpad + 2 buttons) the game reads with pt_coord.
 *
 * Keys: arrows = move, Z or Space = button 1, X or Enter = button 2, F12 = save frame (debug), Esc = quit.
 * Gamepad: left stick / d-pad = move, A = button 1, B = button 2.
 */
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>

#include "m68k.h"
#include "runtime.h"

#define W 384
#define H 280
#define SCALE 2

/* The visible area, like a TV's overscan. The player puts out 280 lines (PAL), but the game switches its
   pictures on at line 2 and they run out after ~262 lines; below that the chip shows whatever memory follows
   the picture, which a TV hid behind its bezel. Show lines 2-261; --full-frame shows all 280. */
#define VIS_TOP 2
#define VIS_H   260
int full_frame;

static SDL_Window *win;
static SDL_Renderer *ren;
static SDL_Texture *tex;
static SDL_GameController *pad;
static uint32_t frame[W * H];
static Uint32 last_frame;
static int ptr_x = W / 2, ptr_y = H / 2, ptr_buttons;
int headless;

void window_init(void)
{
    if (headless) { audio_init(); return; }    /* no device, but --wav still records */
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS) < 0) {
        fprintf(stderr, "SDL: %s - running without a window\n", SDL_GetError());
        headless = 1;
        return;
    }
    win = SDL_CreateWindow("Hotel Mario (CD-i) - decomp port", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                           W * SCALE, (full_frame ? H : VIS_H) * SCALE, SDL_WINDOW_RESIZABLE);
    ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    SDL_RenderSetLogicalSize(ren, W * SCALE, (full_frame ? H : VIS_H) * SCALE);
    tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING, W, H);
    audio_init();
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
    ptr_x = ptr_x < 0 ? 0 : ptr_x >= W ? W - 1 : ptr_x;
    ptr_y = ptr_y < 0 ? 0 : ptr_y >= H ? H - 1 : ptr_y;
    ptr_buttons = b;
}

void window_tick(void)
{
    Uint32 now;
    os9_tick();
    if (headless || !win) return;
    now = SDL_GetTicks();
    if (now - last_frame < 20) return;   /* 50 Hz, like a PAL CD-i */
    last_frame = now;
    read_input();
    display_render(frame);
    SDL_UpdateTexture(tex, 0, frame, W * 4);
    SDL_RenderClear(ren);
    {
        SDL_Rect vis = { 0, VIS_TOP, W, VIS_H };
        SDL_RenderCopy(ren, tex, full_frame ? 0 : &vis, 0);
    }
    SDL_RenderPresent(ren);
}

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

void window_pointer(int *x, int *y, int *buttons)
{
    window_tick();
    *x = ptr_x;
    *y = ptr_y;
    *buttons = ptr_buttons;
}
