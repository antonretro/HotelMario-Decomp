/* video.c - display timing (0x45ea) */
#include "hotel.h"

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
