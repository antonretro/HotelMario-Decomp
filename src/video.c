/* video.c - display timing (0x45ea) */
#include "hotel.h"

/* 0x4554: full brightness for plane A (0), plane B (1) or both (2) */
icf_to_max(which)
short which;
{
    switch (which) {
    case 0:
        dc_wrfi(gdsP->path, gdsP->fct, 4, 0xdb000000 | 0x3f);
        break;
    case 2:
        dc_wrfi(gdsP->path, gdsP->fct, 4, 0xdb000000 | 0x3f);
        /* fall through */
    case 1:
        dc_wrfi(gdsP->path, gdsP->fct_b, 4, 0xdc000000 | 0x3f);
        break;
    }
}

/* 0x45ea: wait for the next video line event (vertical blank) */
vBlankWait(unused)
int unused;
{
    int ev;

    ev = _ev_link("line_event");
    ds_dispatch_execute(2);
    _ev_wait(ev, 1, 1);
    _ev_unlink(ev);
}
