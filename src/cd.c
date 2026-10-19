/* cd.c - real-time disc playback helpers (0x11b4-0x1d42) */
#include "hotel.h"

/* 0x123c: trigger callback from the play control block */
dsTRIGcb(a, b)
{
    cd_trig_flag = 1;
    cd_trig += 0x5c00;
}

/* 0x130a: run queued disc events until playback stops */
cd_wait(unused)
{
    while (cd_play_flag)
        ds_dispatch_execute(2);
}
