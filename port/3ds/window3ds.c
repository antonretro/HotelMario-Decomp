/*
 * window3ds.c - the 3DS side of window.c: the CD-i picture on the top screen, buttons as the CD-i pointer.
 *
 * The TV crop (lines 2-241) is exactly 240 lines, so the 384 x 240 picture fits the 400 x 240 top screen
 * at full size with an 8-pixel border each side. Pixels are written straight into the framebuffer (no GPU):
 * the top screen is stored rotated, one 240-pixel column after another, bottom to top, 3 bytes BGR.
 * The bottom screen is a text console for messages.
 *
 * Buttons: d-pad / circle pad = move, A = button 1, B = button 2, START + SELECT = quit.
 */
#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "m68k.h"
#include "runtime.h"
#include "settings.h"

#define W 384
#define H 280
#define VIS_TOP 2
#define VIS_H 240
#define LEFT ((400 - W) / 2)

int full_frame;
int headless;

static uint32_t frame[W * H];
static u64 start_ms, next_frame_ms;
static unsigned long refresh_count;
#define PTR_W 768
#define PTR_H 560
static int ptr_x = PTR_W / 2, ptr_y = PTR_H / 2, ptr_buttons;

clock_t port_clock(void)
{
    if (!start_ms) start_ms = osGetTime();
    return (clock_t)(osGetTime() - start_ms);
}

void window_init(void)
{
    settings_load();
    osSetSpeedupEnable(true);           /* New 3DS: 804 MHz and the L2 cache (no effect on an original 3DS) */
    gfxInitDefault();
    gfxSetDoubleBuffering(GFX_TOP, true);
    consoleInit(GFX_BOTTOM, 0);
    printf("Hotel Mario (CD-i) - 3DS port\nA = button 1, B = button 2\nSTART + SELECT = quit\n\n");
    audio_init();
    audio_set_volume(settings.volume);
}

static void present(const uint32_t *fb)
{
    u8 *out = gfxGetFramebuffer(GFX_TOP, GFX_LEFT, 0, 0);
    int x0, x, y;
    /* 8 columns at a time: each source row read is one cache line, each column written top to bottom */
    for (x0 = 0; x0 < W; x0 += 8)
        for (y = 0; y < VIS_H; y++) {
            const uint32_t *src = fb + (VIS_TOP + y) * W + x0;
            u8 *p = out + ((x0 + LEFT) * 240 + 239 - y) * 3;
            for (x = 0; x < 8; x++, p += 240 * 3) {
                uint32_t c = src[x];
                p[0] = (u8)c;
                p[1] = (u8)(c >> 8);
                p[2] = (u8)(c >> 16);
            }
        }
    gfxFlushBuffers();
    gfxSwapBuffers();
}

static void read_input(void)
{
    u32 k;
    int dx = 0, dy = 0, b = 0;
    if (!aptMainLoop()) exit(0);
    hidScanInput();
    k = hidKeysHeld();
    if ((k & KEY_START) && (k & KEY_SELECT)) exit(0);
    if (k & KEY_LEFT) dx -= 3;      /* KEY_LEFT etc. cover both the d-pad and the circle pad */
    if (k & KEY_RIGHT) dx += 3;
    if (k & KEY_UP) dy -= 3;
    if (k & KEY_DOWN) dy += 3;
    if (k & KEY_A) b |= 1;
    if (k & KEY_B) b |= 2;
    ptr_x += dx; ptr_y += dy;
    ptr_x = ptr_x < 0 ? 0 : ptr_x >= PTR_W ? PTR_W - 1 : ptr_x;
    ptr_y = ptr_y < 0 ? 0 : ptr_y >= PTR_H ? PTR_H - 1 : ptr_y;
    ptr_buttons = b;
}

void window_tick(void)
{
    u64 now = (u64)port_clock();
    os9_tick();
    if (now < next_frame_ms) return;
    next_frame_ms = now - next_frame_ms < 20 ? next_frame_ms + 20 : now + 20;   /* 50 pictures a second */
    read_input();
    {
        static unsigned seen;
        static unsigned long last_ms;
        static u64 t_render, t_present, t_log, t_loop;
        static unsigned pictures, presents;
        u64 a = svcGetSystemTick(), b, c;
        int fresh = 0;
        if (display_frame_due(&seen, &last_ms)) { display_render(frame); pictures++; fresh = 1; }
        b = svcGetSystemTick();
        if (fresh) {           /* the screen keeps showing the last picture: only copy a new one */
            present(frame);
            presents++;
        }
        c = svcGetSystemTick();
        t_render += b - a;
        t_present += c - b;
        if (!t_log) t_log = c;
        if (c - t_log >= SYSCLOCK_ARM11 * 2) {   /* every 2 s: where the time went */
            double s = (double)(c - t_log) / SYSCLOCK_ARM11;
            fprintf(stderr, "  [perf] %lu ms: %.1f pictures/s, %.1f presents/s, drawing %.0f%%, screen copy %.0f%%, "
                    "game %.0f%%\n", (unsigned long)now, pictures / s, presents / s,
                    100.0 * t_render / (c - t_log), 100.0 * t_present / (c - t_log),
                    100.0 - 100.0 * (t_render + t_present) / (c - t_log));
            t_log = c; t_render = t_present = 0; pictures = presents = 0;
        }
        (void)t_loop;
    }
    refresh_count++;
}

/* Smooth pacing is off on the 3DS: the screen runs at ~60 Hz, which would speed the 50 Hz game up. */
int window_refreshes_per_field(void) { return 0; }
unsigned long window_refresh_count(void) { return refresh_count; }

void window_sleep(int ms) { svcSleepThread((s64)ms * 1000000LL); }

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
