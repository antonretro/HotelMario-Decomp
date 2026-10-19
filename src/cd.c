/* cd.c - real-time disc playback helpers (0x11b4-0x1d42) */
#include "hotel.h"

/* 0x11b4: playback error. Error 0x60a ends the clip normally; otherwise retry, then give up. */
dsERRORcb(a, b)
{
    if (DsPcbError == 0x60a) {
        cd_play_flag = 0;
        if (termfunc)
            (*termfunc)();
    } else if (--cd_retry == 0)
        hm_error(2);
    else
        seekNplay(cd_rtf, cd_eors);
}

/* 0x11fa: stop playback (or finish it, if the play control block says it's done) */
cd_terminate(unused)
{
    DsEorFunction = 0;
    if (!(DsPcb & 0x100))
        ss_abort(DsFile->path);
    else {
        cd_play_flag = 0;
        if (termfunc)
            (*termfunc)();
    }
}

/* 0x1256 */
cd_init(a, b)
{
    ds_init(1, 1, 1);
    ds_dispatcher_init(0x80);
    DsErrorFunction = dsERRORcb;
    DsTriggerFunction = dsTRIGcb;
    DsFlags |= 0x2ee;
    DsFlags &= ~0x10;
    ((char *)&DsFlags)[1] |= 0x10;
}

/* 0x12a2: play real-time file `rtf` (an index into myrtf) from the start */
seekNplay(rtf, eors)
int rtf, eors;
{
    cd_rtf = rtf;
    cd_eors = eors;
    cd_play_flag = 1;
    if (ds_seek(&myrtf[rtf], 1, &ds_index, mychan) == -1)
        hm_error(1);
    else if (ds_play(mychan, eors))
        hm_error(2);
}

/* 0x123c: trigger callback from the play control block */
dsTRIGcb(a, b)
{
    cd_trig_flag = 1;
    cd_trig += 0x5c00;
}

/* 0x19a4: let a queued sound map finish, then switch sound off and stop playback */
cd_smq(unused)
{
    while (gdsP->sound_state == 2)
        ds_dispatch_execute(2);
    gdsP->sound_state = 0;
    sm_off(gdsP->unk004_sound);
    cd_terminate(0);
}

/* 0x130a: run queued disc events until playback stops */
cd_wait(unused)
{
    while (cd_play_flag)
        ds_dispatch_execute(2);
}
