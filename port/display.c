/*
 * display.c - the CD-i video chip (MCD212) as the game drives it through the dc_* calls.
 *
 * The game writes "display control program" instructions into field and line control tables
 * (FCT/LCT). Each instruction is 32 bits: the top byte is the opcode, the rest its data.
 * This first version applies every instruction as soon as it is written (no per-line timing) and
 * keeps the state needed to draw a frame:
 *   0x40     plane A display start address        0x80-0xBF  load palette entry (bank*64 + low 6 bits)
 *   0x50     plane B display start address        0xC0       image coding (A: bits 0-3, B: bits 8-11)
 *   0xC1     transparency control                 0xC2       plane order
 *   0xC3     palette bank for following loads     0xC4/0xC6  transparent colour A / B
 *   0xCD     backdrop colour
 * Opcodes not handled yet are counted and listed with --trace, to guide the next round.
 */
#include <stdio.h>
#include <string.h>

#include "m68k.h"
#include "runtime.h"

#define W 384
#define H 280

static struct {
    uint32_t start[2];          /* display start address of plane A, B */
    int      coding[2];         /* MCD212 coding nibble per plane */
    uint32_t clut[256];
    int      bank;
    uint32_t transp_ctl, transp[2], order, backdrop;
} vs;

static unsigned seen[256];
static int next_table = 1;

static void instr(uint32_t v)
{
    int op = (int)(v >> 24);
    uint32_t data = v & 0xFFFFFF;
    seen[op]++;
    if (op >= 0x80 && op <= 0xBF) { vs.clut[(vs.bank * 64 + (op & 0x3F)) & 255] = data; return; }
    switch (op & 0xF0) {
    case 0x40: vs.start[0] = data; return;
    case 0x50: vs.start[1] = data; return;
    }
    switch (op) {
    case 0xC0: vs.coding[0] = data & 0xF; vs.coding[1] = (data >> 8) & 0xF; break;
    case 0xC1: vs.transp_ctl = data; break;
    case 0xC2: vs.order = data; break;
    case 0xC3: vs.bank = data & 3; break;
    case 0xC4: vs.transp[0] = data; break;
    case 0xC6: vs.transp[1] = data; break;
    case 0xCD: vs.backdrop = data; break;
    }
}

/* I$SetStt fn 0x56 on the video path: d2 = dc_* sub-function, arguments in d3.. and a0 */
void display_setstt(void)
{
    uint32_t i, j;
    switch (cpu.d[2] & 0xFFFF) {
    case 0x00:                                  /* dc_crfct: create field control table -> id */
    case 0x06:                                  /* dc_crlct: create line control table -> id */
        cpu.d[0] = (uint32_t)next_table++;
        break;
    case 0x02:                                  /* dc_wrfct(fct, start, count, buf) */
        for (i = 0; i < (cpu.d[5] & 0xFFFF); i++) instr(rd32(cpu.a[0] + 4 * i));
        break;
    case 0x04:                                  /* dc_wrfi(fct, index, instruction) */
        instr(cpu.d[5]);
        break;
    case 0x08:                                  /* dc_wrlct(lct, line, col, nlines, ncols, buf) */
        for (i = 0; i < (cpu.d[6] & 0xFFFF); i++)
            for (j = 0; j < (cpu.d[7] & 0xFFFF); j++)
                instr(rd32(cpu.a[0] + 4 * (i * (cpu.d[7] & 0xFFFF) + j)));
        break;
    case 0x0A:                                  /* dc_wrli(lct, line, col, instruction) */
        instr(cpu.d[6]);
        break;
    default:                                    /* links, exec, signals...: nothing to draw yet */
        break;
    }
}

void display_report(void)
{
    int op;
    fprintf(stderr, "display instructions seen:");
    for (op = 0; op < 256; op++)
        if (seen[op]) fprintf(stderr, " %02x:%u", op, seen[op]);
    fprintf(stderr, "\nplane A start %06x coding %x, plane B start %06x coding %x\n",
            vs.start[0], vs.coding[0], vs.start[1], vs.coding[1]);
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

/* Draw one plane into rgb[]; opaque[] marks pixels that are not transparent. */
static void draw_plane(int p, uint32_t *rgb, uint8_t *opaque)
{
    uint32_t a = vs.start[p];
    int coding = vs.coding[p], x, y;
    int clut_off = p ? 128 : 0;
    if (!a || !coding) { memset(opaque, 0, W * H); return; }
    for (y = 0; y < H; y++) {
        if (coding == 5) {                                  /* DYUV */
            int Y = 16, U = 128, V = 128;
            for (x = 0; x < W; x += 2) {
                int b0 = (int)rd8(a + x), b1 = (int)rd8(a + x + 1), y0;
                y0 = Y + DQ[b0 >> 4]; y0 = y0 < 0 ? 0 : y0 > 255 ? 255 : y0;
                U += DQ[b0 & 15]; U = U < 0 ? 0 : U > 255 ? 255 : U;
                Y = y0 + DQ[b1 >> 4]; Y = Y < 0 ? 0 : Y > 255 ? 255 : Y;
                V += DQ[b1 & 15]; V = V < 0 ? 0 : V > 255 ? 255 : V;
                rgb[y * W + x] = yuv(y0, U, V);
                rgb[y * W + x + 1] = yuv(Y, U, V);
            }
            a += W;
        } else if (coding == 0xB) {                         /* CLUT4: two pixels per byte */
            for (x = 0; x < W; x += 2) {
                int b = (int)rd8(a + x / 2);
                rgb[y * W + x] = vs.clut[clut_off + (b >> 4)];
                rgb[y * W + x + 1] = vs.clut[clut_off + (b & 15)];
            }
            a += W / 2;
        } else {                                            /* CLUT8 (1) / CLUT7 (3) */
            int mask = coding == 1 ? 255 : 127;
            for (x = 0; x < W; x++)
                rgb[y * W + x] = vs.clut[(coding == 1 ? 0 : clut_off) + ((int)rd8(a + x) & mask)];
            a += W;
        }
        for (x = 0; x < W; x++)
            opaque[y * W + x] = !(vs.transp_ctl && rgb[y * W + x] == (vs.transp[p] & 0xFFFFFF));
    }
}

/* Compose both planes over the backdrop into out[] (0x00RRGGBB, 384x280). */
void display_render(uint32_t *out)
{
    static uint32_t pa[W * H], pb[W * H];
    static uint8_t oa[W * H], ob[W * H];
    int i, b_front = vs.order & 1;
    draw_plane(0, pa, oa);
    draw_plane(1, pb, ob);
    for (i = 0; i < W * H; i++) {
        uint32_t c = vs.backdrop & 0xFFFFFF;
        if (b_front) { if (oa[i]) c = pa[i]; if (ob[i]) c = pb[i]; }
        else         { if (ob[i]) c = pb[i]; if (oa[i]) c = pa[i]; }
        out[i] = c;
    }
}
