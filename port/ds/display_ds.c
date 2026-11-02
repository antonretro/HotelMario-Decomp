/*
 * display_ds.c - the CD-i picture on the DS's 2D engine, without a software mixer.
 *
 * display.c keeps the game's display tables as on every port. Mixing two 384 x 280 planes in software would use
 * all of the DS, but its 2D engine can do the same job, because the two chips work alike:
 *   plane A, plane B        BG3 and BG2, 8-bit bitmaps of 512 x 256; 384 wide, stepped 1.5 pixels at a time
 *   256 palette entries     the DS's 256, plane A in 0-127 and plane B in 128-255 as on the CD-i
 *   colour key              palette index 0, which a DS bitmap doesn't draw
 *   plane order             BG priority
 *   mixing (the Philips     alpha blending, which adds the two layers
 *   logo's cross-fade)
 *   brightness (fades)      the plane's half of the palette, scaled; a DYUV plane has no palette, so its strength
 *                           in the blend when mixing, and otherwise the screen's master brightness
 *   DYUV (full-colour       a 16-bit bitmap, decoded only when the picture changes
 *   stills)
 * Lines 2-241 of a picture (the TV crop) go to the 192 lines of the top screen by leaving out every fifth one, which
 * together with the 1.5 step gives the picture its 4:3 shape back. CLUT7 lines are plain copies; run-length lines
 * are decoded on the way.
 *
 * That fit is too coarse for the game's 8-pixel font, so:
 *   stages     the score bar is laid out again to fit 256 pixels at 1:1, on black, above the picture. A stage is when
 *              mario() is running (ds_recomp.h); its code is the one thing that tells for certain. X picks the
 *              picture: all of it (the default), or 1:1 with a camera that eases after Mario. The VCOUNT interrupt
 *              splits the screen.
 *   text       on any other picture the game draws itself, plane A (the text) stays 1:1 while plane B stays fitted,
 *              so a menu keeps its whole background and its text stays sharp. The planes are separate layers, so
 *              the text only has to be centred where it was. Its box only ever grows while a screen is up, so the
 *              blinking cursor can't move it. A line too long for 256 pixels loses the empty column of each letter.
 * Cutscenes, logos and DYUV stills always use the fit.
 */
#define display_maybe_dump display_maybe_dump_full
#define display_report display_report_full
#include "../display.c"
#undef display_maybe_dump
#undef display_report

#include "ds_hw.h"

#define TOP_LINE 2
#define BOTTOM_LINE 242             /* lines 2-241 */
#define WORDS (W / 4)

/* The score bar: two rows of text in lines 20-37. STAGE stays on the first, MARIO, TIME and the coins on the second,
   and the score, at the far right of the second, moves up beside STAGE: the columns between are empty. */
#define HUD_LINE 20
#define HUD_ROWS 18
#define HUD_WORD 8                  /* what's kept starts at x = 32 */
#define HUD_WORDS 56                /* and is 224 pixels wide */
#define SCORE_WORD 74               /* x = 296 */
#define SCORE_WORDS 14
#define SCORE_TO_WORD 50
#define SCORE_LINE 30
#define SCORE_UP 9                  /* rows */
#define SHOW_ROWS (192 - HUD_ROWS)
#define CAM_MIN_Y (HUD_LINE + HUD_ROWS)
#define CAM_MAX_X (W - 256)
#define CAM_MAX_Y (BOTTOM_LINE - SHOW_ROWS)
#define STAGE_LINES (BOTTOM_LINE - CAM_MIN_Y)
#define PLAYER_LINK 0x250           /* game record -> player */
#define PLAYER_X 0x10e              /* top left of the 32 x 40 sprite, in picture pixels */
#define PLAYER_Y 0x110
#define LONG_PA 293                 /* 8/7: a text line with every eighth column left out */

enum { VIEW_FIT, VIEW_STAGE, VIEW_CAMERA, VIEW_TEXT };
static int view, stage_on, cam_x = CAM_MAX_X / 2, cam_y = CAM_MIN_Y, text_x, text_y, text_pa = 256;
static int box_x0 = W, box_x1 = -1, box_y0 = BOTTOM_LINE, box_y1 = -1;   /* where plane A has had something */
static int word_first, word_count = WORDS;   /* the part of each line that's copied */
extern int ds_zoom;
unsigned ds_mario_calls;
int ds_menu_reset;                  /* A or B was pressed: a menu may be turning to another page */
volatile unsigned ds_vblanks;

static DTCM_BSS uint32_t line_buf[WORDS];
static DTCM_BSS uint8_t key_lut[2][256];     /* colour -> palette index to draw, 0 = see-through */
static int lut_plain[2];            /* the table changes nothing, so whole words can be copied */
static int plane_on[2];             /* always-transparent planes are switched off */
static int lut_line[2];             /* set up for this frame yet? */
static int drew[2], drew_before[2], dyuv[2];
static uint32_t dyuv_sig[2];        /* what the 16-bit bitmap holds */
static int dyuv_fresh[2];           /* decode this frame's DYUV lines */
static int zero_at_128;             /* plane A's colour 0 is shown: it borrows entry 128 (see make_lut) */

void display_maybe_dump(void) { }
void display_report(void) { }

/* The transparency rule, as in draw_line: colour by colour, once a frame. A DS bitmap never draws index 0, so a
   plane A that must show its colour 0 needs another entry for it: over a DYUV plane B, the half of the palette
   that B would use is free. (The Philips logo's dark background is such a colour 0.) */
static void make_lut(int p)
{
    uint32_t cond = vs.transp_set ? (vs.transp_ctl >> (p ? 8 : 0)) & 0xF : 8;
    uint32_t care = ~vs.mask[p] & 0xFCFCFC, key = vs.transp[p] & care;
    int keyed = (cond & 7) == 1 || (cond & 7) == 5 || (cond & 7) == 6, inv = (cond & 8) != 0;
    int off = p ? 128 : 0, n = vs.coding[p] == 1 ? 256 : 128, i;
    lut_plain[p] = 1;
    plane_on[p] = keyed || ((cond & 7) == 0) == inv;
    if (!p) zero_at_128 = 0;
    for (i = 0; i < n; i++) {
        int index = vs.coding[p] == 1 ? i : off + i, to = index;
        if (keyed && ((vs.clut[index] & care) == key) != inv) {
            to = 0;
        } else if (!index && vs.coding[1] == 5) {
            to = 128;
            zero_at_128 = 1;
        }
        key_lut[p][i] = (uint8_t)to;
        if (to != index) lut_plain[p] = 0;
    }
}

/* One line of palette indexes (bytes) into the plane's bitmap */
__attribute__((noinline)) ARM_CODE ITCM_CODE static void put_line(int p, int out, const uint8_t *src)
{
    uint32_t *to = PLANE8(p) + out * 128;      /* 512 bytes a row */
    int i;
    if (((uintptr_t)src & 3) || !lut_plain[p]) {
        uint8_t *b = (uint8_t *)line_buf;
        int mask = vs.coding[p] == 1 ? 255 : 127;
        for (i = 0; i < W; i++) b[i] = key_lut[p][src[i] & mask];
        for (i = word_first; i < word_first + word_count; i++) to[i] = line_buf[i];
    } else {
        const uint32_t *s = (const uint32_t *)src;
        uint32_t keep = vs.coding[p] == 1 ? 0xFFFFFFFFu : 0x7F7F7F7Fu, set = p && vs.coding[p] != 1 ? 0x80808080u : 0;
        for (i = word_first; i < word_first + word_count; i++) to[i] = (s[i] & keep) | set;
    }
}

/* A line with no picture. Over a DYUV plane B it's drawn in black from the free half of the palette, not left
   empty: only a pixel with something on top of it takes part in the blend that dims plane B. */
static void clear_line(int p, int out)
{
    uint32_t *to = PLANE8(p) + out * 128, fill = !p && vs.coding[1] == 5 ? 0x81818181u : 0;
    int i;
    for (i = 0; i < WORDS; i++) to[i] = fill;
}

/* RL7: colour byte, or 0x80|colour + count (0 = rest of line). The colour table is applied once a run, not once a
   pixel, and an unshown line (out < 0) is only walked past. Returns the bytes it used. */
__attribute__((noinline)) ARM_CODE ITCM_CODE static uint32_t rl_line(int p, uint32_t a, int out)
{
    const uint8_t *start = src_ram + (a & RAM_MASK), *s = start, *lut = key_lut[p];
    uint8_t *b = (uint8_t *)line_buf;
    int x = 0, i;
    while (x < W) {
        int c = *s++, n = 1;
        if (c & 0x80) {
            n = *s++;
            if (!n || x + n > W) n = W - x;
            c &= 0x7F;
        }
        if (out >= 0) {
            int colour = lut[c], end = x + n;
            while (x < end) b[x++] = (uint8_t)colour;
        } else {
            x += n;
        }
    }
    if (out >= 0) {
        uint32_t *to = PLANE8(p) + out * 128;
        for (i = word_first; i < word_first + word_count; i++) to[i] = line_buf[i];
    }
    return (uint32_t)(s - start);
}

/* DYUV to 15-bit colour: every line runs through all its pixels (each is a change from the one before), and two
   pixels out of three go to the 256-pixel row. */
static DTCM_BSS int16_t y_r[256], y_b[256], u_g[256], v_g[256];
static uint8_t clamp5[256 * 3];
static int tables_ready;

static void dyuv_tables(void)
{
    int i;
    for (i = 0; i < 256; i++) {
        y_r[i] = (int16_t)((1371 * (i - 128)) / 1000);
        y_b[i] = (int16_t)((1733 * (i - 128)) / 1000);
        u_g[i] = (int16_t)((336 * (i - 128)) / 1000);
        v_g[i] = (int16_t)((698 * (i - 128)) / 1000);
    }
    for (i = 0; i < 256 * 3; i++) clamp5[i] = (uint8_t)(i < 256 ? 0 : i >= 512 ? 31 : (i - 256) >> 3);
    tables_ready = 1;
}

ARM_CODE static inline uint16_t rgb15(int y, int u, int v)
{
    int r = clamp5[y + y_r[v] + 256], g = clamp5[y - u_g[u] - v_g[v] + 256], b = clamp5[y + y_b[u] + 256];
    return (uint16_t)(0x8000 | b << 10 | g << 5 | r);
}

__attribute__((noinline)) ARM_CODE ITCM_CODE static void dyuv_line(int p, int out, uint32_t a)
{
    uint16_t *to = PLANE16(p) + out * 256;
    int Y = 16, U = 128, V = 128, x, n = 0;
    if (vs.dyuv_set[p]) {
        Y = (int)(vs.dyuv_start[p] >> 16) & 255;
        U = (int)(vs.dyuv_start[p] >> 8) & 255;
        V = (int)vs.dyuv_start[p] & 255;
    }
    for (x = 0; x < W; x += 2) {
        int b0 = (int)SRC8(a + x), b1 = (int)SRC8(a + x + 1), y0;
        U = (U + DQ[b0 >> 4]) & 255;
        y0 = (Y + DQ[b0 & 15]) & 255;
        V = (V + DQ[b1 >> 4]) & 255;
        Y = (y0 + DQ[b1 & 15]) & 255;
        if (x % 3 != 2) to[n++] = rgb15(y0, U, V);
        if ((x + 1) % 3 != 2) to[n++] = rgb15(Y, U, V);
    }
}

/* Enough samples to tell one still from the next, or from itself half-way through arriving from the disc. Each
   line is sampled at its start, because a flat area is all zeros (no change) after the first few pixels. */
static uint32_t picture_signature(uint32_t a)
{
    uint32_t s = a, i;
    for (i = 0; i < BOTTOM_LINE; i++, a += W)
        s = s * 31 + SRC8(a) + (SRC8(a + 1) << 8) + (SRC8(a + 2) << 16) + (SRC8(a + 150 + i) << 24);
    return s;
}

/* Streamed animation runs at about 12 pictures a second and the screen is drawn at up to 25, so half the drawing would
   give the picture that's already there. Planes the game draws into itself (every stage) can change anywhere, so a
   frame with one of those always counts as new. */
static int same_as_shown(void)
{
    static uint32_t shown;
    uint32_t key = vs.transp_ctl + vs.order * 3 + vs.backdrop * 5 + (uint32_t)view;
    int p, i;
    for (p = 0; p < 2; p++) {
        int coding = vs.coding[p];
        if (coding && coding != 5 && !plane_rl[p]) {
            shown = 0;
            return 0;
        }
        key = key * 31 + (uint32_t)coding + (uint32_t)vs.icf[p] * 16;
        if (coding) key = key * 31 + picture_signature(vs.line_addr[p]);
    }
    for (i = 0; i < 256; i++) key = key * 3 + vs.clut[i];
    if (key == shown) return 1;
    shown = key;
    return 0;
}

/* One line of a plane; out < 0 means the line isn't shown, but its data still has to be walked past. */
static uint32_t show_line(int p, int out)
{
    uint32_t a = vs.line_addr[p], used = 0;
    int coding = vs.coding[p];
    if (!a || !coding || (plane_end[p] && a >= plane_end[p])) {
        if (out >= 0 && (drew_before[p] || drew[p])) clear_line(p, out);
        return 0;
    }
    if (!lut_line[p]) {
        lut_line[p] = 1;
        make_lut(p);
    }
    if (plane_rl[p] && coding != 5) {
        used = rl_line(p, a, out);
    } else if (coding == 5) {                           /* DYUV */
        if (!dyuv[p]) {                                 /* the first line of this frame's picture */
            dyuv[p] = 1;
            dyuv_fresh[p] = 0;
            if (vs.icf[p]) {                            /* at brightness 0 it can wait: the Philips logo streams its */
                uint32_t sig = picture_signature(a) + vs.dyuv_start[p];   /* last card in behind the animation */
                dyuv_fresh[p] = sig != dyuv_sig[p];
                dyuv_sig[p] = sig;
            }
        }
        if (out >= 0 && dyuv_fresh[p]) dyuv_line(p, out, a);
        used = W;
    } else if (coding == 0xB) {                         /* CLUT4 */
        if (out >= 0) {
            uint8_t *b = (uint8_t *)line_buf;
            int x;
            for (x = 0; x < W; x += 2) {
                int c = (int)SRC8(a + x / 2);
                b[x] = (uint8_t)(c >> 4);
                b[x + 1] = (uint8_t)(c & 15);
            }
            put_line(p, out, b);
        }
        used = W / 2;
    } else {                                            /* CLUT8 (1) / CLUT7 (3) */
        if (out >= 0) put_line(p, out, src_ram + (a & RAM_MASK));
        used = W;
    }
    drew[p] = 1;
    return used;
}

static uint16_t scaled(uint32_t c, int icf)
{
    uint32_t r = ((c >> 19) & 31) * (uint32_t)icf / 63, g = ((c >> 11) & 31) * (uint32_t)icf / 63;
    uint32_t b = ((c >> 3) & 31) * (uint32_t)icf / 63;
    return (uint16_t)(b << 10 | g << 5 | r);
}

/* A long line loses the column that's empty in most letters: which start makes the skipped columns fall there? */
static int text_start(int x, int pa)
{
    uint32_t a = vs.line_addr[0] + (uint32_t)(box_y0 - TOP_LINE) * W;
    uint32_t empty[8], best_count = 0xFFFFFFFFu;
    int y, k, i, c, best = x, shift;
    memset(empty, 0, sizeof empty);
    for (y = box_y0; y <= box_y1; y += 2, a += W * 2)
        for (k = box_x0; k <= box_x1; k++)
            if (key_lut[0][SRC8(a + k) & 127]) empty[k & 7]++;
    for (shift = 0; shift < 8; shift++) {
        uint32_t cost = 0;
        int last = x + shift;
        for (i = 1; i < 256; i++) {
            c = x + shift + (i * pa >> 8);
            if (c - last > 1) cost += empty[(c - 1) & 7];
            last = c;
        }
        if (cost < best_count) {
            best_count = cost;
            best = x + shift;
        }
    }
    return best;
}

/* Where the text is. 0 = this picture isn't one to treat as text. */
static int find_text(int was)
{
    static int x_from, x_to;
    uint32_t a = vs.line_addr[0];
    int x0 = W, x1 = -1, y0 = BOTTOM_LINE, y1 = -1, y, x, w, centre, row;
    for (y = TOP_LINE; y < BOTTOM_LINE; y += 2, a += W * 2)
        for (x = 0; x < W; x += 2)
            if (key_lut[0][SRC8(a + x) & 127]) {
                if (x < x0) x0 = x;
                if (x > x1) x1 = x;
                if (y < y0) y0 = y;
                y1 = y;
            }
    if (x1 < 0 || was != VIEW_TEXT || ds_menu_reset || vs.icf[0] < 63) {
        box_x0 = W;
        box_x1 = -1;
        box_y0 = BOTTOM_LINE;
        box_y1 = -1;
    }
    ds_menu_reset = 0;
    if (x1 < 0) return 0;
    if (x0 < box_x0) box_x0 = x0;
    if (x1 > box_x1) box_x1 = x1;
    if (y0 < box_y0) box_y0 = y0;
    if (y1 > box_y1) box_y1 = y1;
    w = box_x1 - box_x0 + 2;
    if (w > 256 * 8 / 7 || box_y1 - box_y0 > 180) return 0;
    if (box_x0 != x_from || box_x1 != x_to) {           /* nothing moves unless the box does */
        x_from = box_x0;
        x_to = box_x1;
        text_pa = w > 256 ? LONG_PA : 256;
        text_x = (box_x0 + box_x1 + 1) / 2 - text_pa / 2;
        text_x = text_x < 0 ? 0 : text_x > W - text_pa ? W - text_pa : text_x;
        if (text_pa != 256) text_x = text_start(text_x, text_pa);
    }
    centre = (box_y0 + box_y1) / 2;     /* the middle of the text stays where the fitted picture has it */
    row = (centre - TOP_LINE) * 4 / 5;
    text_y = centre - row;
    return 1;
}

/* One step towards Mario: a quarter of the way, so the camera eases in and never jumps */
static int ease(int cam, int target)
{
    int step = (target - cam) / 4;
    if (!step && cam != target) step = target > cam ? 1 : -1;
    return cam + step;
}

static void follow_mario(int was)
{
    uint32_t player = rd32(GAME_RECORD + PLAYER_LINK);
    int x, y;
    if (player && player + 0x120 < RAM_SIZE) {
        x = (int16_t)rd16(player + PLAYER_X) + 16 - 128;
        y = (int16_t)rd16(player + PLAYER_Y) + 20 - SHOW_ROWS / 2;
        x = x < 0 ? 0 : x > CAM_MAX_X ? CAM_MAX_X : x;
        y = y < CAM_MIN_Y ? CAM_MIN_Y : y > CAM_MAX_Y ? CAM_MAX_Y : y;
        cam_x = was == VIEW_CAMERA ? ease(cam_x, x) : x;
        cam_y = was == VIEW_CAMERA ? ease(cam_y, y) : y;
    }
}

static void choose_view(void)
{
    static unsigned seen_calls;
    static clock_t beat;
    clock_t now = clock();
    int was = view, cutscene = !vs.coding[0] || vs.coding[0] == 5 || vs.coding[1] == 5 || plane_rl[0];
    if (ds_mario_calls != seen_calls) {
        seen_calls = ds_mario_calls;
        beat = now;
        stage_on = 1;
    }
    if (cutscene || now - beat > (clock_t)(vs.icf[0] < 63 ? 200 : 2000)) stage_on = 0;
    view = VIEW_FIT;
    if (!cutscene) {
        if (!lut_line[0]) {
            lut_line[0] = 1;
            make_lut(0);
        }
        if (stage_on) view = ds_zoom ? VIEW_CAMERA : VIEW_STAGE;
        else if (find_text(was)) view = VIEW_TEXT;
    }
    if (view == VIEW_CAMERA) follow_mario(was);
}

/* Which row of plane p's bitmap a picture line goes to, -1 = none, and which part of it. fit = its row in the fit. */
static int row_of(int p, int y, int fit)
{
    word_first = 0;
    word_count = WORDS;
    if (y < TOP_LINE) return -1;
    if (view == VIEW_STAGE || view == VIEW_CAMERA) {
        if (y < CAM_MIN_Y) return -1;                   /* the score bar: hud_line */
        if (view == VIEW_STAGE) {
            int a = (y - CAM_MIN_Y) * SHOW_ROWS / STAGE_LINES, b = (y - 1 - CAM_MIN_Y) * SHOW_ROWS / STAGE_LINES;
            return y == CAM_MIN_Y || a != b ? HUD_ROWS + a : -1;
        }
        word_first = cam_x / 4;
        word_count = 65;
        return y >= cam_y && y < cam_y + SHOW_ROWS ? HUD_ROWS + y - cam_y : -1;
    }
    if (view == VIEW_TEXT && !p) {
        word_first = text_x / 4;
        word_count = text_pa / 4 + 1;
        return y >= text_y && y < text_y + 192 ? y - text_y : -1;
    }
    return fit;
}

/* The score bar's lines, from plane A alone: plane B's rows stay empty, which is black */
static void hud_line(int y)
{
    const uint8_t *src = src_ram + (vs.line_addr[0] & RAM_MASK);
    uint8_t *b = (uint8_t *)line_buf;
    uint32_t *row = PLANE8(0) + (y - HUD_LINE) * 128;
    int i;
    for (i = 0; i < W; i++) b[i] = key_lut[0][src[i] & 127];
    for (i = 0; i < HUD_WORDS; i++) row[i] = line_buf[HUD_WORD + i];
    for (; i < 64; i++) row[i] = 0;
    if (y >= SCORE_LINE) {
        uint32_t *up = row - SCORE_UP * 128;
        for (i = 0; i < SCORE_WORDS; i++) up[SCORE_TO_WORD + i] = line_buf[SCORE_WORD + i];
    }
    for (i = 0; i < 64; i++) PLANE8(1)[(y - HUD_LINE) * 128 + i] = 0;
}

/* The palette, each plane's half at its own brightness (63 = as it is). Rewritten only when it has changed. */
static void put_palette(int icf_a, int icf_b)
{
    static uint32_t shown;
    uint32_t key = (uint32_t)icf_a * 64 + (uint32_t)icf_b + (uint32_t)zero_at_128 * 4096 + vs.backdrop * 8192
                   + (uint32_t)vs.coding[1];
    int i;
    for (i = 0; i < 256; i++) key = key * 3 + vs.clut[i];
    if (key == shown) return;
    shown = key;
    for (i = 0; i < 128; i++) {
        HW_PALETTE[i] = scaled(vs.clut[i], icf_a);
        HW_PALETTE[128 + i] = scaled(vs.clut[128 + i], icf_b);
    }
    if (zero_at_128) HW_PALETTE[128] = scaled(vs.clut[0], icf_a);
    if (vs.coding[1] == 5) HW_PALETTE[129] = 0;     /* clear_line */
    HW_PALETTE[0] = scaled(vs.backdrop, 63);
}

void display_ds_init(void)
{
    hw_init();
    if (!tables_ready) dyuv_tables();
}

/* Draw the frame the game has just finished; 0 = nothing had changed */
int display_present(void)
{
    int p, y, c, out = -1;
    word_first = 0;
    word_count = WORDS;
    src_ram = RAM;
    src_regions = regions;
    for (p = 0; p < 2; p++) {
        src_fct[p] = active_fct[p] >= 0 ? fct[active_fct[p]] : 0;
        src_lct[p] = active_lct[p] >= 0 ? (const uint32_t (*)[LCT_COLS])lct[active_lct[p]] : 0;
        lut_line[p] = 0;
        drew[p] = 0;
        dyuv[p] = 0;
    }
    vs.icf[0] = vs.icf[1] = 63;
    run_fct(0);
    run_fct(1);
    plane_rl[0] = plane_rl[1] = 0;
    for (y = 0; y < BOTTOM_LINE; y++) {
        uint32_t used[2];
        int line = -1;
        for (p = 0; p < 2; p++) {
            uint32_t before = vs.line_addr[p];
            if (src_lct[p] && y < LCT_LINES)
                for (c = 0; c < LCT_COLS; c++) {
                    uint32_t v = src_lct[p][y][c];
                    if ((v >> 24) != 0x10) instr(v, p);
                }
            if (vs.line_addr[p] != before || y == 0) {
                plane_rl[p] = is_run_length(vs.line_addr[p]);
                plane_end[p] = plane_rl[p] ? region_end(vs.line_addr[p]) : 0;
            }
        }
        if (y == 0) { frame_start[0] = vs.line_addr[0]; frame_start[1] = vs.line_addr[1]; }
        if (y == TOP_LINE) {
            choose_view();
            if (same_as_shown()) return 0;
        }
        if (y >= TOP_LINE && (y - TOP_LINE) % 5 != 4) line = ++out;
        if ((view == VIEW_STAGE || view == VIEW_CAMERA) && y >= HUD_LINE && y < CAM_MIN_Y) hud_line(y);
        used[0] = show_line(0, row_of(0, y, line));
        used[1] = show_line(1, row_of(1, y, line));
        vs.line_addr[0] += used[0];
        vs.line_addr[1] += used[1];
    }
    {
        int visible[2], any_dyuv = dyuv[0] || dyuv[1];
        int mix = vs.transp_set && !(vs.transp_ctl & 0x800000);
        int strength[2] = { 16, 16 }, fade = 0;
        for (p = 0; p < 2; p++) {
            visible[p] = drew[p] && plane_on[p];
            drew_before[p] = drew[p];
            if (dyuv[p] && !vs.icf[p]) visible[p] = 0;   /* (a layer with nothing over it can't be dimmed) */
            if (dyuv[p] && mix) strength[p] = vs.icf[p] * 16 / 63;
        }
        if (any_dyuv && !mix) {                          /* one master brightness for the whole screen */
            int icf = 63;
            for (p = 0; p < 2; p++)
                if (drew[p] && vs.icf[p] < icf) icf = vs.icf[p];
            fade = 16 - icf * 16 / 63;
            put_palette(63, 63);
        } else {
            put_palette(vs.icf[0], vs.icf[1]);
        }
        hw_show(visible, dyuv, (int)(vs.order & 1), mix, strength, fade);
        if (view == VIEW_TEXT) {                         /* rows the text's lines don't reach */
            word_first = 0;
            for (y = 0; y < TOP_LINE - text_y; y++) clear_line(0, y);
            for (y = BOTTOM_LINE - text_y; y < 192; y++) clear_line(0, y);
        }
        if (view == VIEW_CAMERA) hw_view(256, 256, cam_x, cam_x, HUD_ROWS);
        else if (view == VIEW_STAGE) hw_view(384, 384, 0, 0, HUD_ROWS);
        else if (view == VIEW_TEXT) hw_view(text_pa, 384, text_x, 0, 0);
        else hw_view(dyuv[0] ? 256 : 384, dyuv[1] ? 256 : 384, 0, 0, 0);
    }
    return 1;
}
