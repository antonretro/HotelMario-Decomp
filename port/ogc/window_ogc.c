/*
 * window_ogc.c - the Wii / GameCube side of window.c (libogc): the CD-i picture on the TV, controllers as the
 * CD-i pointer.
 *
 * The TV crop (lines 2-240, 384 x 239) is scaled to 640 x 478 and written straight into the TV framebuffer,
 * which holds pixels in pairs as Y1 Cb Y2 Cr. Two framebuffers: draw into the hidden one, show it next vsync.
 *
 * Controls: Wii Remote held sideways (d-pad = move, 1 = button 1, 2 = button 2, HOME = quit), or a GameCube
 * controller (stick / d-pad = move, A = button 1, B = button 2, START + Z = quit).
 */
#include <gccore.h>
#include <ogc/lwp_watchdog.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#ifdef HW_RVL
#include <wiiuse/wpad.h>
#endif

#include "m68k.h"
#include "runtime.h"
#include "settings.h"

#define W 384
#define H 280
#define VIS_TOP 2
#define VIS_H 239
#define OUT_W 640                       /* scaled picture width (TV pixels) */

int full_frame;
int headless;

static uint32_t frame[W * H];
static GXRModeObj *rmode;
static u32 *xfb[2];
static int back;                        /* the framebuffer not on screen */
static u64 start_ticks;
static unsigned long next_frame_ms, refresh_count;
static int src_x[OUT_W];                /* which picture column each TV pixel shows */
#define PTR_W 768
#define PTR_H 560
static int ptr_x = PTR_W / 2, ptr_y = PTR_H / 2, ptr_buttons;

clock_t port_clock(void)
{
    if (!start_ticks) start_ticks = gettime();
    return (clock_t)ticks_to_millisecs(gettime() - start_ticks);
}

void window_init(void)
{
    int i;
    settings_load();
    /* video is already up (files_ogc.c sets it up first, to show messages); just the extras */
    for (i = 0; i < OUT_W; i++) src_x[i] = i * W / OUT_W;
    PAD_Init();
#ifdef HW_RVL
    WPAD_Init();
#endif
    audio_init();
    audio_set_volume(settings.volume);
}

void ogc_video_init(void)               /* called by files_ogc.c before anything else */
{
    if (rmode) return;
    VIDEO_Init();
    rmode = VIDEO_GetPreferredMode(NULL);
    xfb[0] = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));
    xfb[1] = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));
    console_init(xfb[0], 20, 20, rmode->fbWidth, rmode->xfbHeight, rmode->fbWidth * VI_DISPLAY_PIX_SZ);
    VIDEO_Configure(rmode);
    VIDEO_SetNextFramebuffer(xfb[0]);
    VIDEO_SetBlack(FALSE);
    VIDEO_Flush();
    VIDEO_WaitVSync();
    if (rmode->viTVMode & VI_NON_INTERLACE) VIDEO_WaitVSync();
    back = 1;
}

static inline u32 ycbcr(uint32_t a, uint32_t b)   /* two RGB pixels -> Y1 Cb Y2 Cr */
{
    int r1 = (a >> 16) & 255, g1 = (a >> 8) & 255, b1 = a & 255;
    int r2 = (b >> 16) & 255, g2 = (b >> 8) & 255, b2 = b & 255;
    int y1 = (299 * r1 + 587 * g1 + 114 * b1) / 1000, y2 = (299 * r2 + 587 * g2 + 114 * b2) / 1000;
    int r = (r1 + r2) >> 1, g = (g1 + g2) >> 1, bl = (b1 + b2) >> 1;
    int cb = 128 + (-169 * r - 331 * g + 500 * bl) / 1000, cr = 128 + (500 * r - 419 * g - 81 * bl) / 1000;
    y1 = 16 + y1 * 219 / 255; y2 = 16 + y2 * 219 / 255;
    cb = cb < 16 ? 16 : cb > 240 ? 240 : cb;
    cr = cr < 16 ? 16 : cr > 240 ? 240 : cr;
    return (u32)y1 << 24 | (u32)cb << 16 | (u32)y2 << 8 | (u32)cr;
}

static void present(const uint32_t *fb)
{
    u32 *out = (u32 *)MEM_K1_TO_K0(xfb[back]);
    int pairs = rmode->fbWidth / 2, left = (rmode->fbWidth - OUT_W) / 4;
    int top = (rmode->xfbHeight - VIS_H * 2) / 2, y, x;
    if (top < 0) top = 0;
    for (x = 0; x < pairs * rmode->xfbHeight; x++) out[x] = 0x10801080;   /* black borders (all-zero is green) */
    for (y = 0; y < VIS_H && top + 2 * y + 1 < rmode->xfbHeight; y++) {
        const uint32_t *src = fb + (VIS_TOP + y) * W;
        u32 *row = out + (top + 2 * y) * pairs + left;
        for (x = 0; x < OUT_W / 2; x++) row[x] = ycbcr(src[src_x[2 * x]], src[src_x[2 * x + 1]]);
        memcpy(row + pairs, row, OUT_W * 2);                           /* each line twice */
    }
    DCFlushRange(out, rmode->fbWidth * rmode->xfbHeight * 2);
    VIDEO_SetNextFramebuffer(xfb[back]);
    VIDEO_Flush();
    back ^= 1;
}

static void read_input(void)
{
    int dx = 0, dy = 0, b = 0;
    u16 p;
    s8 sx, sy;
    PAD_ScanPads();
    p = PAD_ButtonsHeld(0);
    sx = PAD_StickX(0);
    sy = PAD_StickY(0);
    if ((p & PAD_BUTTON_START) && (p & PAD_TRIGGER_Z)) exit(0);
    if ((p & PAD_BUTTON_LEFT) || sx < -40) dx -= 3;
    if ((p & PAD_BUTTON_RIGHT) || sx > 40) dx += 3;
    if ((p & PAD_BUTTON_UP) || sy > 40) dy -= 3;
    if ((p & PAD_BUTTON_DOWN) || sy < -40) dy += 3;
    if (p & PAD_BUTTON_A) b |= 1;
    if (p & PAD_BUTTON_B) b |= 2;
#ifdef HW_RVL
    {
        u32 w;
        WPAD_ScanPads();
        w = WPAD_ButtonsHeld(0);
        if (w & WPAD_BUTTON_HOME) exit(0);
        if (w & WPAD_BUTTON_UP) dx -= 3;         /* held sideways: the d-pad turns a quarter */
        if (w & WPAD_BUTTON_DOWN) dx += 3;
        if (w & WPAD_BUTTON_RIGHT) dy -= 3;
        if (w & WPAD_BUTTON_LEFT) dy += 3;
        if (w & WPAD_BUTTON_1) b |= 1;
        if (w & WPAD_BUTTON_2) b |= 2;
    }
#endif
    ptr_x += dx; ptr_y += dy;
    ptr_x = ptr_x < 0 ? 0 : ptr_x >= PTR_W ? PTR_W - 1 : ptr_x;
    ptr_y = ptr_y < 0 ? 0 : ptr_y >= PTR_H ? PTR_H - 1 : ptr_y;
    ptr_buttons = b;
}

void window_tick(void)
{
    unsigned long now = (unsigned long)port_clock();
    os9_tick();
    if (now < next_frame_ms) return;
    next_frame_ms = now - next_frame_ms < 20 ? next_frame_ms + 20 : now + 20;   /* 50 pictures a second */
    read_input();
    {
        static unsigned seen;
        static unsigned long last_ms;
        if (display_frame_due(&seen, &last_ms)) {
            display_render(frame);
            present(frame);
        }
    }
    refresh_count++;
}

int window_refreshes_per_field(void) { return 0; }
unsigned long window_refresh_count(void) { return refresh_count; }
void window_sleep(int ms) { usleep((useconds_t)ms * 1000); }

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
