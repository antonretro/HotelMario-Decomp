/* ds_hw.h - the video hardware display_ds.c draws to: four 128 KB VRAM banks on the main 2D engine */
#ifndef DS_HW_H
#define DS_HW_H

#include <nds.h>

#define PLANE8(p)  ((uint32_t *)(uintptr_t)((p) ? 0x06020000 : 0x06000000))   /* 512 x 256, 8 bits */
#define PLANE16(p) ((uint16_t *)(uintptr_t)((p) ? 0x06060000 : 0x06040000))   /* 256 x 256, 16 bits */

extern volatile unsigned ds_vblanks;

/* What the next VBlank sets up, for planes A and B. pa = source pixels (in 1/256) per screen pixel, x = first source
   pixel. The first top_rows rows of the screen are drawn 1:1 from column 0 (the stage's score bar). */
static volatile struct { int pa[2], x[2], top_rows; } view_hw = { { 384, 384 }, { 0, 0 }, 0 };

static void view_apply(int pa3, int pa2, int x3, int x2)
{
    REG_BG3PA = (s16)pa3;
    REG_BG2PA = (s16)pa2;
    REG_BG3X = x3 << 8;
    REG_BG2X = x2 << 8;
}

static void view_vblank(void)
{
    ds_vblanks++;
    if (view_hw.top_rows) {
        view_apply(256, 256, 0, 0);
        SetYtrigger(view_hw.top_rows - 1);
        irqEnable(IRQ_VCOUNT);
    } else {
        irqDisable(IRQ_VCOUNT);
        view_apply(view_hw.pa[0], view_hw.pa[1], view_hw.x[0], view_hw.x[1]);
    }
}

static void view_split(void)
{
    while (!(REG_DISPSTAT & DISP_IN_HBLANK)) { }
    view_apply(view_hw.pa[0], view_hw.pa[1], view_hw.x[0], view_hw.x[1]);
}

static inline void hw_view(int pa_a, int pa_b, int x_a, int x_b, int top_rows)
{
    view_hw.pa[0] = pa_a;
    view_hw.pa[1] = pa_b;
    view_hw.x[0] = x_a;
    view_hw.x[1] = x_b;
    view_hw.top_rows = top_rows;
}

/* strength[]: each plane in the blend, 0-16. fade16: 0 = full brightness, 16 = black. */
static inline void hw_show(int visible[2], int dyuv[2], int b_in_front, int mix, int strength[2], int fade16)
{
    int c = MODE_5_2D;
    int base3 = dyuv[0] ? BG_BMP16_256x256 | BG_BMP_BASE(16) : BG_BMP8_512x256 | BG_BMP_BASE(0);
    int base2 = dyuv[1] ? BG_BMP16_256x256 | BG_BMP_BASE(24) : BG_BMP8_512x256 | BG_BMP_BASE(8);
    if (visible[0]) c |= DISPLAY_BG3_ACTIVE;
    if (visible[1]) c |= DISPLAY_BG2_ACTIVE;
    REG_BG3CNT = (u16)(base3 | BG_PRIORITY(b_in_front ? 1 : 0));   /* plane A */
    REG_BG2CNT = (u16)(base2 | BG_PRIORITY(b_in_front ? 0 : 1));   /* plane B */
    REG_BLDCNT = mix ? (u16)(BLEND_ALPHA | (b_in_front ? BLEND_SRC_BG2 | BLEND_DST_BG3 : BLEND_SRC_BG3 | BLEND_DST_BG2)) : 0;
    REG_BLDALPHA = (u16)(strength[b_in_front ? 1 : 0] | strength[b_in_front ? 0 : 1] << 8);   /* front, then back */
    REG_MASTER_BRIGHT = (u16)(fade16 ? 2 << 14 | fade16 : 0);
    REG_DISPCNT = (u32)c;
}

static inline void hw_init(void)
{
    vramSetBankA(VRAM_A_MAIN_BG_0x06000000);
    vramSetBankB(VRAM_B_MAIN_BG_0x06020000);
    vramSetBankC(VRAM_C_MAIN_BG_0x06040000);
    vramSetBankD(VRAM_D_MAIN_BG_0x06060000);
    REG_DISPCNT = MODE_5_2D;
    REG_BG3PB = REG_BG2PB = 0;
    REG_BG3PC = REG_BG2PC = 0;
    REG_BG3PD = REG_BG2PD = 256;                                   /* the picture is drawn at 192 lines */
    REG_BG3X = REG_BG2X = 0;
    REG_BG3Y = REG_BG2Y = 0;
    irqSet(IRQ_VBLANK, view_vblank);
    irqSet(IRQ_VCOUNT, view_split);
    irqEnable(IRQ_VBLANK);
}

#define HW_PALETTE ((vu16 *)0x05000000)

#endif
