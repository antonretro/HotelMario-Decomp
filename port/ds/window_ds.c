/*
 * window_ds.c - the DS side of window.c: start-up, buttons as the CD-i pointer, the game's clock, and finding your
 * disc image on the SD card. The picture is in display_ds.c. The bottom screen shows the controls and the credit, like
 * the 3DS; the messages and the speed are behind it, for SELECT or for when the game stops.
 *
 * Buttons: d-pad = move, A = button 1, B = button 2, X = zoom, SELECT = messages, START + SELECT = quit.
 * Your disc image (the .bin, 2352 bytes a sector) is either inside the .nds (tools/build_ds.py --full) or in
 * /hotelmario/ on the SD card.
 */
#include <dirent.h>
#include <nds.h>
#include <fat.h>
#include <filesystem.h>
#include <stdarg.h>
#include <unistd.h>

#include "m68k.h"
#include "runtime.h"
#include "settings.h"

#undef main
#undef exit
#undef fopen
#undef fread
#undef fgetc
#undef fseek
#undef ftell
#undef fclose

#define PTR_W 768
#define PTR_H 560
#define VBLANKS_PER_PICTURE 2        /* 30 pictures a second, evenly spaced */
#define GAME_STACK (128 * 1024)     /* the recompiled game calls deep; the DS's own stack is 16 KB, in DTCM */

int full_frame;
int headless;
extern int display_present(void);
extern void display_ds_init(void);
int hotel_main(int argc, char **argv);

static int ptr_x = PTR_W / 2, ptr_y = PTR_H / 2, ptr_buttons;
static unsigned long refresh_count;
static uint32_t game_stack[GAME_STACK / 4];
static int have_card, have_inside;   /* an SD card; files packed inside the .nds */
static int info_shown;
static PrintConsole messages, guide;
int ds_zoom;                         /* X: a stage's picture at 1:1, with a camera (display_ds.c) */
extern int ds_menu_reset;
extern volatile unsigned ds_vblanks;
static int saved_argc;
static char **saved_argv;

/* Two text layers: 0 takes the messages all the time, 1 is the controls. Only the layers are switched: the crash
   screen draws on this display too, and has to be seen. */
static void show_info(int on)
{
    info_shown = on;
    if (on) {
        videoBgEnableSub(0);
        videoBgDisableSub(1);
    } else {
        videoBgEnableSub(1);
        videoBgDisableSub(0);
    }
}

static void write_guide(void)
{
    static const char *const rows[][2] = {
        { "      A", "Button 1 / OK" }, { "      B", "Button 2" }, { "  D-Pad", "Move" }, { "      X", "Stage zoom" },
        { " SELECT", "Info" }, { "ST + SE", "Quit" },
    };
    PrintConsole *was = consoleSelect(&guide);
    unsigned i;
    printf("\x1b[2;0H");
    for (i = 0; i < sizeof rows / sizeof rows[0]; i++)
        printf("\x1b[%u;4H\x1b[37;1m%s  \x1b[37;0m%s", 5 + i * 2, rows[i][0], rows[i][1]);
    printf("\x1b[22;6H\x1b[30;1mPort by Anton Retro");
    consoleSelect(was);
}

/* Only crash reports disassemble, and the 300 KB it costs are better spent */
unsigned int m68k_disassemble(char *str, unsigned int pc, unsigned int cpu_type)
{
    (void)pc;
    (void)cpu_type;
    str[0] = 0;
    return 2;
}

void ds_exit(int code)
{
    show_info(1);
    fprintf(stderr, "stopped (%d). Press START + SELECT.\n", code);
    for (;;) {
        swiWaitForVBlank();
        scanKeys();
        if ((keysHeld() & KEY_START) && (keysHeld() & KEY_SELECT)) exit(code);
    }
}

/* Timers 0 and 1 count at 33.51 MHz / 1024 from the moment the game starts: 32.73 per millisecond */
static void start_clock(void)
{
    TIMER_DATA(0) = 0;
    TIMER_DATA(1) = 0;
    TIMER_CR(1) = TIMER_ENABLE | TIMER_CASCADE;
    TIMER_CR(0) = TIMER_ENABLE | TIMER_DIV_1024;
}

clock_t port_clock(void)
{
    uint32_t hi, lo;
    do {
        hi = TIMER_DATA(1);
        lo = TIMER_DATA(0);
    } while (hi != TIMER_DATA(1));
    return (clock_t)(((uint64_t)(hi << 16 | lo) * 2002) >> 16);
}

/* The game spins on the OS-9 tick counter without any system call, so a timer keeps it moving, like the ticker
   thread on the other ports. */
static void tick_irq(void)
{
    os9_tick();
}

void window_init(void)
{
    settings_load();
    audio_init();
    audio_set_volume(settings.volume);
    display_ds_init();
    timerStart(2, ClockDivider_1024, TIMER_FREQ_1024(200), tick_irq);
}

static void read_input(void)
{
    uint32_t k;
    int dx = 0, dy = 0, b = 0;
    scanKeys();
    k = keysHeld();
    if ((k & KEY_START) && (k & KEY_SELECT)) ds_exit(0);
    if (keysDown() & KEY_SELECT) show_info(!info_shown);
    if (keysDown() & KEY_X) ds_zoom = !ds_zoom;
    if (keysDown() & (KEY_A | KEY_B)) ds_menu_reset = 1;
    if (k & KEY_LEFT) dx -= 3;
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

/* The speed line, so a test on a real console or in an emulator can be read off the bottom screen */
static void show_speed(unsigned long now, unsigned pictures, unsigned long draw_ms, unsigned long polls)
{
    printf("\x1b[23;0H%2u.%u pictures/s %3lu ms each  ", pictures / 2, pictures * 5 % 10,
           pictures ? draw_ms / pictures : 0);
    (void)now;
    (void)polls;
}

void window_tick(void)
{
    static unsigned long next_input, next_speed, draw_ms, polls;
    static unsigned seen, pictures, last_vblank;
    static unsigned long seen_ms;
    unsigned long now = (unsigned long)port_clock();
    unsigned line = REG_VCOUNT;
    os9_tick();
    polls++;
    /* Drawing starts as the blank period begins: the copy then stays ahead of the screen's scan, so no picture is
       ever shown half old, half new, and the pictures come at an even 30 a second. Interpreted code can look past
       that moment, so a picture never waits more than one refresh for it. */
    if ((ds_vblanks - last_vblank >= VBLANKS_PER_PICTURE + 1 ||
         (ds_vblanks - last_vblank >= VBLANKS_PER_PICTURE && line >= 192 && line < 224)) &&
        display_frame_due(&seen, &seen_ms)) {
        unsigned long a = (unsigned long)port_clock();
        if (display_present()) {
            last_vblank = ds_vblanks;
            draw_ms += (unsigned long)port_clock() - a;
            pictures++;
        }
    }
    if (now < next_input) return;
    next_input = now + 20;
    refresh_count++;
    read_input();
    if (now >= next_speed) {
        next_speed = now + 2000;
        if (info_shown) show_speed(now, pictures, draw_ms, polls);
        pictures = 0;
        draw_ms = polls = 0;
    }
}

/* Smooth pacing belongs to monitors: the DS keeps to the clock, like the 3DS */
int window_refreshes_per_field(void) { return 0; }
unsigned long window_refresh_count(void) { return refresh_count; }

void window_sleep(int ms)
{
    unsigned long end = (unsigned long)port_clock() + (unsigned long)ms;
    do {
        swiWaitForIRQ();
    } while ((unsigned long)port_clock() < end);
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

static int is_image(const char *name)
{
    size_t n = strlen(name);
    return n > 4 && !strcasecmp(name + n - 4, ".bin");
}

/* First .bin in the folder */
static int mount_from(const char *dir)
{
    DIR *d = opendir(dir);
    struct dirent *e;
    int mounted = 0;
    if (!d) return 0;
    while (!mounted && (e = readdir(d))) {
        char path[300];
        if (!is_image(e->d_name)) continue;
        snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
        printf("Disc: %.28s\n", e->d_name);
        mounted = disc_mount(path) > 0;
    }
    closedir(d);
    if (mounted) chdir(dir);                /* saved games go in saves/ beside the image */
    return mounted;
}

/* A loader can pass the image as the first argument (runtime.c puts it in fs_root); otherwise look on the card. */
void find_game_files(int given)
{
    if (given && is_image(fs_root) && disc_mount(fs_root) > 0) return;
    if (have_inside && disc_mount("nitro:/hotel.bin") > 0) {
        if (have_card) {                    /* saved games still need a card; without one the game just can't save */
            mkdir("/hotelmario", 0777);
            chdir("/hotelmario");
        }
        return;
    }
    if (have_card && mount_from("/hotelmario")) return;
    printf("\nNo disc image found.\n\nPut your Hotel Mario .bin in\n/hotelmario/ on the SD card.\n");
    ds_exit(1);
}

static void game(void)
{
    hotel_main(saved_argc, saved_argv);
}

/* Run the game on a stack in main RAM: this function and everything under it never return here otherwise */
static void on_game_stack(void)
{
    void *top = game_stack + GAME_STACK / 4;
    __asm__ volatile("mov r4, sp\n\tmov sp, %0\n\tblx %1\n\tmov sp, r4\n\t"
                     : : "r"(top), "r"(game)
                     : "r0", "r1", "r2", "r3", "r4", "r12", "lr", "cc", "memory");
}

int main(int argc, char **argv)
{
    defaultExceptionHandler();
    lcdMainOnTop();
    videoSetModeSub(MODE_0_2D);
    vramSetBankH(VRAM_H_SUB_BG);
    consoleInit(&messages, 0, BgType_Text4bpp, BgSize_T_256x256, 0, 1, false, true);
    consoleInit(&guide, 1, BgType_Text4bpp, BgSize_T_256x256, 1, 1, false, true);
    consoleSelect(&messages);
    consoleDebugInit(DebugDevice_CONSOLE);
    write_guide();
    show_info(0);
    if (isDSiMode()) setCpuClock(true);     /* 133 MHz */
    start_clock();
    have_card = fatInitDefault();
    have_inside = nitroFSInit(NULL);
    saved_argc = argc;
    saved_argv = argv;
    on_game_stack();
    return 0;
}
