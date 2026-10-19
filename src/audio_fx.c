/* audio_fx.c - sound effect ranges inside the stage's audio stream (0x8bb6-0x8c80) */
#include "hotel.h"

/* 0x8bb6: turn the per-effect sizes into absolute start offsets */
getAFXsizes()
{
    register long *sizes = gdsP->afx_sizes;
    register long *ofs = gdsP->afx;
    register long base = gdsP->afx_base;
    register int i;

    *ofs++ = base;
    for (i = 0; i < 16; i++)
        *ofs++ = *sizes++ + base;
}

/* 0x8bf4: play effect n once */
playAFX(n)
int n;
{
    if (n >= 0 && n <= 15) {
        cd_xbuf = gdsP->afx[n];
        cd_xend = gdsP->afx[n + 1];
    }
}

/* 0x8c30: play effect n and keep looping it */
loopAFX(n)
int n;
{
    cd_xbuf = gdsP->afx[n];
    cd_xend = gdsP->afx[n + 1];
    cd_lpstart = gdsP->afx[n];
    cd_lpend = gdsP->afx[n + 1];
}
