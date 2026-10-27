/*
 * window3ds.c - the 3DS side of window.c: the CD-i picture on the top screen, buttons as the CD-i pointer.
 *
 * The TV crop (lines 2-240, where the pictures are) fits the 400 x 240 top screen
 * at full size with an 8-pixel border each side. Pixels are written straight into the framebuffer (no GPU):
 * the top screen is stored rotated, one 240-pixel column after another, bottom to top, 3 bytes BGR.
 * The bottom screen is a text console for messages.
 *
 * Buttons: d-pad / circle pad = move, A = button 1, B = button 2, START + SELECT = quit.
 */
#include <3ds.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "m68k.h"
#include "runtime.h"
#include "settings.h"

#define W 384
#define H 280
#define VIS_TOP 2
#define VIS_H 239              /* lines 2-240; the screen's bottom row stays black */
#define LEFT ((400 - W) / 2)

int full_frame;
int headless;

static uint32_t frame[W * H];
static u64 start_ms, next_frame_ms;
static unsigned long refresh_count;
static u64 t_idle;                  /* time spent sleeping, for the [perf] line */
#define PTR_W 768
#define PTR_H 560
static int ptr_x = PTR_W / 2, ptr_y = PTR_H / 2, ptr_buttons;

/* Drawing on another core: the game thread copies what a finished picture needs (display_snapshot) and the
   drawer turns it into pixels on the screen while the game carries on. Core 2 on a New 3DS, else core 1. */
static LightEvent draw_go;
static volatile int draw_busy;
static int draw_core = -1;          /* -1 = drawing on the game's core */
static volatile u64 draw_ticks;
static volatile unsigned drawn;
static void present(const uint32_t *fb);
void window_note(const char *fmt, ...);

static void drawer(void *unused)
{
    (void)unused;
    for (;;) {
        u64 a;
        LightEvent_Wait(&draw_go);
        a = svcGetSystemTick();
        display_render_snapshot(frame);
        present(frame);
        draw_ticks += svcGetSystemTick() - a;
        drawn++;
        __atomic_store_n(&draw_busy, 0, __ATOMIC_RELEASE);
    }
}

static void start_drawer(void)
{
    s32 prio = 0x30;
    if (dump_every_ms || dump_at_ms) return;        /* frame dumps draw on the game thread */
    svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
    LightEvent_Init(&draw_go, RESET_ONESHOT);
    if (threadCreate(drawer, 0, 32 * 1024, prio + 1, 2, true)) draw_core = 2;
    else {
        APT_SetAppCpuTimeLimit(80);                 /* an original 3DS lends core 1 to games, up to 80% */
        if (threadCreate(drawer, 0, 32 * 1024, prio + 1, 1, true)) draw_core = 1;
    }
    if (draw_core >= 0) window_note("Drawing on CPU core %d\n", draw_core);
    else window_note("Drawing on the game's core\n");
}

clock_t port_clock(void)
{
    if (!start_ms) start_ms = osGetTime();
    return (clock_t)(osGetTime() - start_ms);
}

/* The bottom screen: a picture (sdmc:/3ds/HotelMario/bottom.bin, 320x240 RGB, made by tools/make_3ds_art.py from
   your disc's own title screen) or, after SELECT, the info screen: these messages and the speed line. The screen
   stays in the text console's format (RGB565, one buffer), so either can be drawn at any time. */
static char notes[1024];
static int info_shown = 1;
static u8 *bottom_pic;

void window_note(const char *fmt, ...)
{
    va_list ap;
    size_t n = strlen(notes);
    va_start(ap, fmt);
    vsnprintf(notes + n, sizeof notes - n, fmt, ap);
    va_end(ap);
    if (info_shown) printf("%s", notes + n);
}

static void show_info(void)
{
    consoleInit(GFX_BOTTOM, 0);         /* clears it */
    info_shown = 1;
    printf("%s", notes);
}

static void show_picture(void)
{
    u16 *fb = (u16 *)gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, 0, 0);
    int x, y;
    info_shown = 0;
    for (x = 0; x < 320; x++)           /* stored rotated: one 240-pixel column per x, bottom to top */
        for (y = 0; y < 240; y++) {
            const u8 *s = bottom_pic + (y * 320 + x) * 3;
            fb[x * 240 + 239 - y] = (u16)((s[0] >> 3) << 11 | (s[1] >> 2) << 5 | (s[2] >> 3));
        }
    GSPGPU_FlushDataCache(fb, 320 * 240 * 2);
}

static void load_picture(void)
{
    FILE *f = fopen("sdmc:/3ds/HotelMario/bottom.bin", "rb");
    if (!f) return;
    bottom_pic = malloc(320 * 240 * 3);
    if (bottom_pic && fread(bottom_pic, 1, 320 * 240 * 3, f) != 320 * 240 * 3) { free(bottom_pic); bottom_pic = 0; }
    fclose(f);
}

void window_init(void)
{
    extern int audio_ok;
    settings_load();
    osSetSpeedupEnable(true);           /* New 3DS: 804 MHz and the L2 cache (no effect on an original 3DS) */
    gfxInitDefault();
    gfxSetDoubleBuffering(GFX_TOP, true);
    consoleInit(GFX_BOTTOM, 0);
    window_note("Hotel Mario (CD-i) - 3DS port\nPort by Anton Retro\n\nA = button 1, B = button 2\n"
                "SELECT = this screen / picture\nSTART + SELECT = quit\n\n");
    audio_init();
    audio_set_volume(settings.volume);
    start_drawer();
    load_picture();
    if (bottom_pic && audio_ok) show_picture();   /* a sound problem keeps the info screen up, so it's seen */
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
    if ((hidKeysDown() & KEY_SELECT) && !(k & KEY_START) && bottom_pic) {   /* flip the bottom screen */
        if (info_shown) show_picture();
        else show_info();
    }
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
        if (display_frame_due(&seen, &last_ms)) {
            if (draw_core < 0) { display_render(frame); fresh = 1; }
            else if (!__atomic_load_n(&draw_busy, __ATOMIC_ACQUIRE) && display_snapshot()) {
                draw_busy = 1;                      /* the drawer takes it from here */
                LightEvent_Signal(&draw_go);
            }                                       /* (drawer still busy: this picture is skipped) */
        }
        b = svcGetSystemTick();
        if (fresh) {           /* the screen keeps showing the last picture: only copy a new one */
            present(frame);
            presents++;
            pictures++;
        }
        c = svcGetSystemTick();
        t_render += b - a;
        t_present += c - b;
        if (!t_log) t_log = c;
        if (c - t_log >= SYSCLOCK_ARM11 * 2) {   /* every 2 s: where the time went */
            double s = (double)(c - t_log) / SYSCLOCK_ARM11;
            double shown = draw_core < 0 ? pictures : drawn, other = 100.0 * draw_ticks / (c - t_log);
            fprintf(stderr, "  [perf] %lu ms: %.1f pictures/s, drawing %.0f%% (+ other core %.0f%%), screen copy %.0f%%, "
                    "idle %.0f%%, game %.0f%%\n", (unsigned long)now, shown / s,
                    100.0 * t_render / (c - t_log), other, 100.0 * t_present / (c - t_log), 100.0 * t_idle / (c - t_log),
                    100.0 - 100.0 * (t_render + t_present + t_idle) / (c - t_log));
            /* and on the bottom screen, so the speed can be read off a real console */
            if (info_shown) printf("\x1b[28;0H%4.1f pictures/s  game %2.0f%%  draw core %2.0f%%   ", shown / s,
                   100.0 - 100.0 * t_idle / (c - t_log), draw_core < 0 ? 100.0 * (t_render + t_present) / (c - t_log) : other);
            t_log = c; t_render = t_present = t_idle = 0; pictures = presents = 0;
            draw_ticks = 0; drawn = 0;
        }
        (void)t_loop;
    }
    refresh_count++;
}

/* Smooth pacing is off on the 3DS: the screen runs at ~60 Hz, which would speed the 50 Hz game up. */
int window_refreshes_per_field(void) { return 0; }
unsigned long window_refresh_count(void) { return refresh_count; }

void window_sleep(int ms)
{
    u64 a = svcGetSystemTick();
    svcSleepThread((s64)ms * 1000000LL);
    t_idle += svcGetSystemTick() - a;
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
