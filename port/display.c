/*
 * display.c - the CD-i video chip (MCD212) as the game drives it through the dc_* calls.
 *
 * The game builds a "display control program" in tables kept by the video driver:
 *   FCT (field control table): a list of instructions run once at the start of every field (frame)
 *   LCT (line control table):  per display line, up to 8 instructions run just before that line
 * dc_wrfct/dc_wrfi put instructions at FCT slots, dc_wrlct/dc_wrli at LCT line/column positions.
 * Each frame we run the planes' FCTs in slot order, then for every line its LCT entries, and draw
 * the line. Slot order matters: e.g. set_up_clut writes 64 palette loads right after a "palette bank 0"
 * slot and 64 after a "bank 1" slot.
 *
 * Instructions: top byte = opcode, low 24 bits = data.
 *   0x10       no operation                        0x40-0x4F  load display start address (owning plane)
 *   0x80-0xBF  palette entry (bank*64 + low 6 bits) 0xC0       image coding (A: bits 0-3, B: bits 8-11;
 *                                                             0 off, 1 CLUT8, 3 CLUT7, 5 DYUV, B CLUT4)
 *   0xC1       transparency control                0xC2       plane order
 *   0xC3       palette bank for following loads    0xC4/0xC6  transparent colour A / B
 *   0xCD       backdrop colour                     0xDB/0xDC  image contribution (brightness) A / B
 * Unknown opcodes are counted (--trace lists them) and otherwise ignored.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "m68k.h"
#include "runtime.h"

#define W 384
#define H 280
#define NTAB 32
#define FCT_SLOTS 256
#define LCT_LINES 320
#define LCT_COLS 8

static uint32_t fct[NTAB][FCT_SLOTS];
static uint32_t lct[NTAB][LCT_LINES][LCT_COLS];
static uint8_t tab_plane[NTAB], tab_is_lct[NTAB];
static int active_fct[2] = { -1, -1 }, active_lct[2] = { -1, -1 };
static int next_table = 1;

static struct {
    uint32_t line_addr[2];      /* address of the current line of plane A, B */
    int      coding[2];
    uint32_t clut[256];
    int      bank;
    uint32_t transp_ctl, transp[2], mask[2], order, backdrop;
    int transp_set;           /* 0xC1 has been written */
    int      icf[2];            /* 0..63 brightness */
} vs;

static unsigned seen[256];
uint32_t field_signal;      /* SetStt 0x59: signal to send every field (0 = none); sent by cdrtos */
uint32_t once_signal;       /* dc_ssig: signal to send at the next field only (0 = none) */

/* Video buffers filled from the disc, with the sector coding they arrived in. A plane whose display
   start points into a buffer that received run-length (RL7, sector format 4) video is decoded as RL7. */
#define NREGION 64
static struct { uint32_t start, len; int fmt; } regions[NREGION];
static int region_next;

/* A child program (the Philips logo) writes its own settings into the display tables while it runs. On a
   CD-i each program's video setup is its own, so when the child exits the parent's complete display state
   (every table, the links and the chip registers) comes back exactly as it was. */
static struct {
    uint32_t fct[NTAB][FCT_SLOTS];
    uint32_t lct[NTAB][LCT_LINES][LCT_COLS];
    uint8_t tab_plane[NTAB], tab_is_lct[NTAB];
    int active_fct[2], active_lct[2], next_table;
    uint32_t field_signal;
} saved;
static int saved_valid;

void display_save_tables(void)
{
    memcpy(saved.fct, fct, sizeof fct);
    memcpy(saved.lct, lct, sizeof lct);
    memcpy(saved.tab_plane, tab_plane, sizeof tab_plane);
    memcpy(saved.tab_is_lct, tab_is_lct, sizeof tab_is_lct);
    memcpy(saved.active_fct, active_fct, sizeof active_fct);
    memcpy(saved.active_lct, active_lct, sizeof active_lct);
    saved.next_table = next_table;
    saved_valid = 1;
}

void display_restore_tables(void)
{
    if (!saved_valid) return;
    memcpy(fct, saved.fct, sizeof fct);
    memcpy(lct, saved.lct, sizeof lct);
    memcpy(tab_plane, saved.tab_plane, sizeof tab_plane);
    memcpy(tab_is_lct, saved.tab_is_lct, sizeof tab_is_lct);
    memcpy(active_fct, saved.active_fct, sizeof active_fct);
    memcpy(active_lct, saved.active_lct, sizeof active_lct);
    next_table = saved.next_table;
    saved_valid = 0;
    if (trace) fprintf(stderr, "  [dcp] parent display state restored: FCT %d/%d LCT %d/%d\n",
                       active_fct[0], active_fct[1], active_lct[0], active_lct[1]);
}

void display_note_video(uint32_t buf, uint32_t bytes, int fmt)
{
    int i;
    for (i = 0; i < NREGION; i++)
        if (regions[i].start == buf) { regions[i].len = bytes; regions[i].fmt = fmt; return; }
    regions[region_next].start = buf;
    regions[region_next].len = bytes;
    regions[region_next].fmt = fmt;
    region_next = (region_next + 1) % NREGION;
}

static int is_run_length(uint32_t addr)
{
    int i;
    for (i = 0; i < NREGION; i++)
        if (regions[i].len && addr >= regions[i].start && addr < regions[i].start + regions[i].len)
            return regions[i].fmt == 4 || regions[i].fmt == 3;   /* RL7 / RL3 */
    return 0;
}

static void instr(uint32_t v, int plane)
{
    int op = (int)(v >> 24);
    uint32_t data = v & 0xFFFFFF;
    seen[op]++;
    if (op >= 0x80 && op <= 0xBF) {
        static int watch = -2;
        int e = (vs.bank * 64 + (op & 0x3F)) & 255;
        if (watch == -2) watch = getenv("HM_WATCHCLUT") ? atoi(getenv("HM_WATCHCLUT")) : -1;
        if (trace && (e == watch || e == watch + 128 || (watch == 999 && e < 128)) && vs.clut[e] != data)
            fprintf(stderr, "  [dcp] palette %d = %06x (plane %c table, bank %u) at %lu ms\n", e, data, 'A' + plane,
                    vs.bank, (unsigned long)(clock() * 1000 / CLOCKS_PER_SEC));
        vs.clut[(vs.bank * 64 + (op & 0x3F)) & 255] = data;
        return;
    }
    if ((op & 0xF0) == 0x40) { vs.line_addr[plane] = data; return; }
    if (trace && op >= 0xD0 && op <= 0xD8) {          /* region control / backdrop: log distinct values */
        static uint32_t seen_rc[64];
        static int n_rc;
        int i;
        for (i = 0; i < n_rc && seen_rc[i] != v; i++) ;
        if (i == n_rc && n_rc < 64) {
            seen_rc[n_rc++] = v;
            fprintf(stderr, "  [dcp] region/backdrop instruction %08x (plane %c) at %lu ms\n", v, 'A' + plane,
                    (unsigned long)(clock() * 1000 / CLOCKS_PER_SEC));
        }
    }
    switch (op) {
    case 0xC0:
        /* Image coding for both planes (0 = off). The game's FCT ends with 0x081010 (planes off) and its
           LCT switches them on at line 2: 0x081515 = DYUV for full-colour stills, 0x081313 = CLUT7. */
        vs.coding[0] = data & 0xF;
        vs.coding[1] = (data >> 8) & 0xF;
        break;
    case 0xC1: vs.transp_ctl = data; vs.transp_set = 1; break;
    case 0xC2: vs.order = data; break;
    case 0xC3: vs.bank = data & 3; break;
    /* The transparent colour goes to the plane whose table runs the instruction: the game's set_up_clut
       writes "0xC4 + colour 0 of the palette" into whichever plane's FCT it loads the palette for. */
    case 0xC4: vs.transp[plane] = data; break;
    case 0xC6: vs.transp[1] = data; break;
    case 0xC7: vs.mask[0] = data; break;
    case 0xC9: vs.mask[1] = data; break;
    case 0xCD: vs.backdrop = data; break;
    case 0xDB: vs.icf[0] = data & 0x3F; break;
    case 0xDC: vs.icf[1] = data & 0x3F; break;
    }
}

static void log_write(const char *how, uint32_t t, uint32_t where, uint32_t v)
{
    int op = (int)(v >> 24);
    if (trace && (op == 0x78 || op == 0xC0 || op == 0xC1 || (op & 0xF0) == 0x40 || op == 0xDB || op == 0xDC))
        fprintf(stderr, "  [dcp] %s table %u @%u: %08x at %lu ms\n", how, t, where, v,
                (unsigned long)(clock() * 1000 / CLOCKS_PER_SEC));
}

/* I$SetStt fn 0x56 on the video path: d2 = dc_* sub-function, arguments in d3.. and a0 */
void display_setstt(void)
{
    uint32_t i, j, t = cpu.d[3] & 0xFFFF;
    switch (cpu.d[2] & 0xFFFF) {
    case 0x00:                                  /* dc_crfct(plane, ...) -> id */
    case 0x06:                                  /* dc_crlct(plane, ...) -> id */
        if (next_table < NTAB) {
            int id = next_table++, plane = (int)(cpu.d[3] & 1), is_lct = (cpu.d[2] & 0xFFFF) == 6;
            tab_plane[id] = (uint8_t)plane;
            tab_is_lct[id] = (uint8_t)is_lct;
            memset(is_lct ? (void *)lct[id] : (void *)fct[id], 0x10, is_lct ? sizeof lct[id] : sizeof fct[id]);
            if (!is_lct) active_fct[plane] = id;
            else if (active_lct[plane] < 0) active_lct[plane] = id;   /* until dc_flnk says otherwise */
            if (trace) fprintf(stderr, "  [dcp] create %s %d for plane %c\n", is_lct ? "LCT" : "FCT", id, 'A' + plane);
            cpu.d[0] = (uint32_t)id;
        }
        break;
    case 0x02:                                  /* dc_wrfct(fct, start, count, buf) */
        for (i = 0; i < (cpu.d[5] & 0xFFFF) && t < NTAB; i++)
            if ((cpu.d[4] & 0xFFFF) + i < FCT_SLOTS) {
                fct[t][(cpu.d[4] & 0xFFFF) + i] = rd32(cpu.a[0] + 4 * i);
                log_write("wrfct", t, (cpu.d[4] & 0xFFFF) + i, rd32(cpu.a[0] + 4 * i));
            }
        break;
    case 0x04:                                  /* dc_wrfi(fct, slot, instruction) */
        if (t < NTAB && (cpu.d[4] & 0xFFFF) < FCT_SLOTS) fct[t][cpu.d[4] & 0xFFFF] = cpu.d[5];
        if (trace && ((cpu.d[5] >> 24) & 0xF8) == 0x78)
            fprintf(stderr, "  [dcp] decoder setting %08x (FCT %u slot %u) at %lu ms\n", cpu.d[5], t, cpu.d[4] & 0xFFFF,
                    (unsigned long)(clock() * 1000 / CLOCKS_PER_SEC));
        if (trace && ((cpu.d[5] >> 24) == 0xDB || (cpu.d[5] >> 24) == 0xDC))
            fprintf(stderr, "  [dcp] brightness %c = %u (FCT %u slot %u) at %lu ms\n", (cpu.d[5] >> 24) == 0xDB ? 'A' : 'B',
                    cpu.d[5] & 0x3F, t, cpu.d[4] & 0xFFFF, (unsigned long)(clock() * 1000 / CLOCKS_PER_SEC));
        break;
    case 0x08: {                                /* dc_wrlct(lct, line, col, nlines, ncols, buf) */
        uint32_t line = cpu.d[4] & 0xFFFF, col = cpu.d[5] & 0xFFFF, nl = cpu.d[6] & 0xFFFF, nc = cpu.d[7] & 0xFFFF;
        for (i = 0; i < nl && t < NTAB; i++)
            for (j = 0; j < nc; j++)
                if (line + i < LCT_LINES && col + j < LCT_COLS) {
                    lct[t][line + i][col + j] = rd32(cpu.a[0] + 4 * (i * nc + j));
                    log_write("wrlct", t, line + i, rd32(cpu.a[0] + 4 * (i * nc + j)));
                }
        break;
    }
    case 0x0A:                                  /* dc_wrli(lct, line, col, instruction) */
        if (t < NTAB && (cpu.d[4] & 0xFFFF) < LCT_LINES && (cpu.d[5] & 0xFFFF) < LCT_COLS)
            lct[t][cpu.d[4] & 0xFFFF][cpu.d[5] & 0xFFFF] = cpu.d[6];
        log_write("wrli", t, cpu.d[4] & 0xFFFF, cpu.d[6]);
        break;
    case 0x0C:                                  /* dc_flnk(fct, lct): the LCT the field table uses */
        if (t < NTAB && (cpu.d[4] & 0xFFFF) < NTAB)
            active_lct[tab_plane[t]] = (int)(cpu.d[4] & 0xFFFF);
        /* fall through to the trace */
    case 0x0D:                                  /* dc_llnk */
    case 0x0E:                                  /* dc_exec */
        if (trace)
            fprintf(stderr, "  [dcp] %s d3=%u d4=%u d5=%x d6=%x at %lu ms\n",
                    (cpu.d[2] & 0xFFFF) == 0x0C ? "flnk" : (cpu.d[2] & 0xFFFF) == 0x0D ? "llnk" : "exec",
                    cpu.d[3] & 0xFFFF, cpu.d[4] & 0xFFFF, cpu.d[5], cpu.d[6], (unsigned long)(clock() * 1000 / CLOCKS_PER_SEC));
        break;
    case 0x11:                                  /* dc_ssig(path, signal, ...): signal the program once, next field */
        if (trace)
            fprintf(stderr, "  [dcp] ssig d3=%x d4=%x d5=%x d6=%x at %lu ms\n", cpu.d[3], cpu.d[4], cpu.d[5], cpu.d[6],
                    (unsigned long)(clock() * 1000 / CLOCKS_PER_SEC));
        once_signal = cpu.d[3] & 0xFFFF;
        break;
    default:                                    /* signals etc.: nothing to draw */
        break;
    }
}

/* ---------------- drawing ---------------- */

static uint32_t yuv(int y, int u, int v)
{
    int r = y + (1371 * (v - 128)) / 1000, g = y - (336 * (u - 128) + 698 * (v - 128)) / 1000,
        b = y + (1733 * (u - 128)) / 1000;
    r = r < 0 ? 0 : r > 255 ? 255 : r;
    g = g < 0 ? 0 : g > 255 ? 255 : g;
    b = b < 0 ? 0 : b > 255 ? 255 : b;
    return (uint32_t)(r << 16 | g << 8 | b);
}

static const int DQ[16] = { 0, 1, 4, 9, 16, 27, 44, 79, -128, -79, -44, -27, -16, -9, -4, -1 };

static int plane_rl[2];   /* decide once per frame whether each plane shows run-length data */
static int only_plane;    /* debugging: 1 = draw plane A only, 2 = plane B only */

/* Draw one line of a plane; returns the bytes it used. opaque[x] = 0 where the pixel is transparent. */
static uint32_t draw_line(int p, uint32_t *rgb, uint8_t *opaque)
{
    uint32_t a = vs.line_addr[p], used = 0;
    int coding = vs.coding[p], x;
    int clut_off = p ? 128 : 0, icf = vs.icf[p];
    if (!a || !coding) { memset(opaque, 0, W); return 0; }
    if (plane_rl[p] && coding != 5) {                   /* RL7: colour byte, or 0x80|colour + count (0 = rest of line) */
        uint32_t s = a;
        x = 0;
        while (x < W) {
            int b = (int)rd8(a++);
            int n = 1;
            if (b & 0x80) {
                n = (int)rd8(a++);
                if (!n || x + n > W) n = W - x;
            }
            while (n-- > 0) rgb[x++] = vs.clut[clut_off + (b & 0x7F)];
        }
        used = a - s;
    } else if (coding == 5) {                           /* DYUV */
        int Y = 16, U = 128, V = 128;
        for (x = 0; x < W; x += 2) {
            int b0 = (int)rd8(a + x), b1 = (int)rd8(a + x + 1), y0;
            y0 = Y + DQ[b0 >> 4]; y0 = y0 < 0 ? 0 : y0 > 255 ? 255 : y0;
            U += DQ[b0 & 15]; U = U < 0 ? 0 : U > 255 ? 255 : U;
            Y = y0 + DQ[b1 >> 4]; Y = Y < 0 ? 0 : Y > 255 ? 255 : Y;
            V += DQ[b1 & 15]; V = V < 0 ? 0 : V > 255 ? 255 : V;
            rgb[x] = yuv(y0, U, V);
            rgb[x + 1] = yuv(Y, U, V);
        }
    } else if (coding == 0xB) {                         /* CLUT4 */
        for (x = 0; x < W; x += 2) {
            int b = (int)rd8(a + x / 2);
            rgb[x] = vs.clut[clut_off + (b >> 4)];
            rgb[x + 1] = vs.clut[clut_off + (b & 15)];
        }
    } else {                                            /* CLUT8 (1) / CLUT7 (3) */
        int mask = coding == 1 ? 255 : 127, off = coding == 1 ? 0 : clut_off;
        for (x = 0; x < W; x++) rgb[x] = vs.clut[off + ((int)rd8(a + x) & mask)];
    }
    /* Transparency (MCD212 register 0xC1): a 4-bit condition per plane (A bits 0-3, B bits 8-11).
       Bits 0-2: 0 always, 1 colour key, 2 transparency bit (RGB555), 3/4 matte flag 0/1, 5/6 matte flag
       0/1 or colour key; bit 3 inverts it (8 = never transparent). The colour key compares the pixel with
       the transparent colour register (0xC4 A / 0xC6 B), ignoring the bits set in the mask register
       (0xC7 / 0xC9) and the 2 low bits of each component, which the 6-bit-per-component CLUT can't hold.
       Matte flags (region detection) aren't emulated yet: they read as 0. */
    {
        uint32_t cond = vs.transp_set ? (vs.transp_ctl >> (p ? 8 : 0)) & 0xF : 8;   /* not set up yet: opaque */
        uint32_t care = ~vs.mask[p] & 0xFCFCFC, key = vs.transp[p] & care;
        for (x = 0; x < W; x++) {
            int t;
            switch (cond & 7) {
            case 0: t = 1; break;
            case 1: case 5: case 6: t = (rgb[x] & care) == key; break;
            default: t = 0; break;
            }
            if (cond & 8) t = !t;
            opaque[x] = !t;
        }
    }
    for (x = 0; x < W; x++) {
        uint32_t c = rgb[x];
        if (icf < 63) {                                 /* brightness (fades): scale by icf/63 */
            uint32_t r = ((c >> 16) & 255) * (uint32_t)icf / 63, g = ((c >> 8) & 255) * (uint32_t)icf / 63,
                     b = (c & 255) * (uint32_t)icf / 63;
            rgb[x] = r << 16 | g << 8 | b;
        }
    }
    if (used) return used;
    return coding == 0xB ? W / 2 : W;
}

static void run_fct(int p)
{
    int i, t = active_fct[p];
    if (t < 0) return;
    for (i = 0; i < FCT_SLOTS; i++) instr(fct[t][i], p);
}

/* Compose both planes over the backdrop into out[] (0x00RRGGBB, 384x280). */
void display_render(uint32_t *out)
{
    static uint32_t la[W], lb[W];
    static uint8_t oa[W], ob[W];
    int y, x, c, p;
    vs.icf[0] = vs.icf[1] = 63;
    run_fct(0);
    run_fct(1);
    if (trace) {                                 /* log each change of the transparency set-up */
        static uint32_t last[5];
        uint32_t now[5] = { vs.transp_ctl, vs.transp[0], vs.transp[1], vs.mask[0], vs.mask[1] };
        if (memcmp(now, last, sizeof now)) {
            fprintf(stderr, "  [dcp] transparency ctl %06x key A %06x B %06x mask A %06x B %06x, order %06x\n",
                    now[0], now[1], now[2], now[3], now[4], vs.order);
            memcpy(last, now, sizeof now);
        }
    }
    plane_rl[0] = plane_rl[1] = 0;
    for (y = 0; y < H; y++) {
        uint32_t used[2];
        for (p = 0; p < 2; p++) {
            uint32_t before = vs.line_addr[p];
            if (active_lct[p] >= 0 && y < LCT_LINES)
                for (c = 0; c < LCT_COLS; c++) instr(lct[active_lct[p]][y][c], p);
            if (vs.line_addr[p] != before || y == 0)   /* new display start: check what data it shows */
                plane_rl[p] = is_run_length(vs.line_addr[p]);
        }
        used[0] = draw_line(0, la, oa);
        used[1] = draw_line(1, lb, ob);
        if (trace && y == 140) {                 /* sample plane A's top-left pixel source for key debugging */
            static uint32_t last;
            uint32_t a = vs.line_addr[0];
            if (la[4] != last)
                fprintf(stderr, "  [dcp] line 140: A pixel %06x (byte %02x, coding %x, rl %d, bank %u) B pixel %06x\n",
                        la[4], rd8(a + 4), vs.coding[0], plane_rl[0], vs.bank, lb[4]);
            last = la[4];
        }
        if (only_plane == 1) memset(ob, 0, W);
        if (only_plane == 2) memset(oa, 0, W);
        for (x = 0; x < W; x++) {
            uint32_t col = vs.backdrop & 0xFFFFFF;
            if (vs.order & 1) { if (oa[x]) col = la[x]; if (ob[x]) col = lb[x]; }
            else              { if (ob[x]) col = lb[x]; if (oa[x]) col = la[x]; }
            out[y * W + x] = col;
        }
        vs.line_addr[0] += used[0];
        vs.line_addr[1] += used[1];
    }
}

/* --dump-at <ms>: write the frame shown at that time, plus each plane alone, as raw RGB for comparisons */
unsigned dump_at_ms;
unsigned dump_every_ms;   /* --dump-every <ms>: a timeline of frames, build/port/frames/<ms>.rgb */

void display_maybe_dump(void)
{
    static int done;
    static unsigned long next_every;
    static uint32_t f[W * H];
    unsigned long now = (unsigned long)(clock() * 1000 / CLOCKS_PER_SEC);
    int pass;
    if (dump_every_ms && now >= next_every) {   /* <ms>.rgb = the frame, <ms>_A / _B = each plane alone */
        char name[64];
        FILE *o;
        int i;
        next_every = now - now % dump_every_ms + dump_every_ms;
        for (pass = 0; pass < 3; pass++) {
            snprintf(name, sizeof name, "build/port/frames/%06lu%s.rgb", now - now % dump_every_ms,
                     pass == 0 ? "" : pass == 1 ? "_A" : "_B");
            only_plane = pass;
            display_render(f);
            only_plane = 0;
            if ((o = fopen(name, "wb"))) {
                for (i = 0; i < W * H; i++) {
                    fputc((int)(f[i] >> 16) & 255, o);
                    fputc((int)(f[i] >> 8) & 255, o);
                    fputc((int)f[i] & 255, o);
                }
                fclose(o);
            }
        }
        if (trace) fprintf(stderr, "  [dump] timeline frame %lu ms (A %06x coding %x / B %06x coding %x)\n",
                           now - now % dump_every_ms, vs.line_addr[0], vs.coding[0], vs.line_addr[1], vs.coding[1]);
    }
    if (dump_at_ms == 1) dump_at_ms = 0, done = 0;   /* F12 in the window: dump right now */
    else if (!dump_at_ms || done || now < dump_at_ms) return;
    done = 1;
    for (pass = 0; pass < 3; pass++) {           /* 0 = both planes, 1 = plane A only, 2 = plane B only */
        char name[64];
        FILE *o;
        int i, save_b = vs.coding[1];
        snprintf(name, sizeof name, "build/port/frame_%s.rgb", pass == 0 ? "both" : pass == 1 ? "A" : "B");
        only_plane = pass;
        display_render(f);
        only_plane = 0;
        (void)save_b;
        if ((o = fopen(name, "wb"))) {
            for (i = 0; i < W * H; i++) {
                fputc((int)(f[i] >> 16) & 255, o);
                fputc((int)(f[i] >> 8) & 255, o);
                fputc((int)f[i] & 255, o);
            }
            fclose(o);
        }
    }
    fprintf(stderr, "  [dump] frames written at %lu ms (plane A start %06x coding %x / B %06x coding %x)\n", now,
            vs.line_addr[0], vs.coding[0], vs.line_addr[1], vs.coding[1]);
}

void display_report(void)
{
    static uint32_t f[W * H];
    static uint8_t used[4096];
    int op, i, lit = 0, colours = 0, nz = 0;
    fprintf(stderr, "display instructions seen:");
    for (op = 0; op < 256; op++)
        if (seen[op] && (op < 0x80 || op > 0xBF)) fprintf(stderr, " %02x", op);
    display_render(f);
    memset(used, 0, sizeof used);
    for (i = 0; i < W * H; i++) {
        int key = (int)(((f[i] >> 12) & 0xF00) | ((f[i] >> 8) & 0xF0) | ((f[i] >> 4) & 0xF));
        if (f[i] != (vs.backdrop & 0xFFFFFF)) lit++;
        if (!used[key]) { used[key] = 1; colours++; }
    }
    for (i = 0; i < 256; i++) nz += vs.clut[i] != 0;
    fprintf(stderr, "\nframe: %d of %d pixels drawn, ~%d colours; palette %d/256 non-black; coding %x/%x; "
            "brightness %d/%d; FCT %d/%d LCT %d/%d\n", lit, W * H, colours, nz, vs.coding[0], vs.coding[1],
            vs.icf[0], vs.icf[1], active_fct[0], active_fct[1], active_lct[0], active_lct[1]);
}
