/* video.c - display set-up and timing (0x4554-0x13cf6) */
#include <errno.h>
#include "hotel.h"

/* 0x1361a: copy a whole 384x280 screen backwards, ending at dst_end (the areas may overlap) */
move_screen(src, dst_end)
char *src;
long *dst_end;
{
    register long *s = (long *)(src + 0x1a400);
    register long *d = dst_end;
    register short i;

    for (i = 0; i < 0x6900; i++)
        *--d = *--s;
}

/* 0x13c58: set up the video environment, hide the cursor, centre the pointer and start the display */
init_video()
{
    int env;

    env = init_videnv();
    if (gc_hide(gdsP->path) == -1)
        exit(errno);
    if (pt_org(gdsP->path, 0, 0) == -1)
        exit(errno);
    if (pt_pos(gdsP->path, 0x17f, 0xea) == -1)
        exit(errno);
    if (dc_exec(gdsP->path, gdsP->fct, gdsP->fct_b) == -1)
        exit(errno);
    return env;
}

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
