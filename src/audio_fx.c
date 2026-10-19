/* audio_fx.c - sound effect ranges inside the stage's audio stream (0x8bb6-0x8c80) */
#include "hotel.h"

/* 0x6632: open the audio device, create the stage's sound map and start it looping, full volume */
init_audio()
{
    char *dev;
    register int sm;
    long info;

    dev = csd_devname(2, 1);
    gdsP->unk004_sound = open(dev, 3);
    free(dev);
    if ((sm = sm_creat(gdsP->unk004_sound, 3, 0x12, &info)) == -1)
        ;   /* the result is compared but never acted on */
    gdsP->sound_info = sm_info(gdsP->unk004_sound, sm);
    sd_loop(gdsP->unk004_sound, *gdsP->sound_info, 0, 1, -1);
    gdsP->sound_unk00e = 0;
    sc_atten(gdsP->unk004_sound, 0);
}

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
