/* ds.c - disc streaming ("ds") layer: real-time file playback (0x15f70-0x170de) */
#include "hotel.h"

/* 0x15f70: fill in a buffer descriptor (five longs, two flag bytes) */
ds_buffer_init(buf, a, b, c, d, e)
long *buf;
long a, b, c, d, e;
{
    if (buf == 0)
        return -1;
    buf[0] = a;
    buf[1] = b;
    buf[2] = c;
    buf[3] = d;
    buf[4] = e;
    ((char *)buf)[0x14] = 0;
    ((char *)buf)[0x15] = 0;
    return 0;
}

/* 0x15fba: fill in a buffer-set descriptor (three longs) */
ds_buffer_set_init(set, a, b, c)
long *set;
long a, b, c;
{
    if (set == 0)
        return -1;
    set[0] = a;
    set[1] = b;
    set[2] = c;
    return 0;
}

/* 0x15fee */
ds_close(f)
DS_FILE *f;
{
    if (f == 0)
        return -1;
    close(f->path);
    f->path = -1;
    return munlink(f->map);
}

/* 0x16024 */
ds_cont()
{
    if (DsFile == 0)
        return -1;
    return ss_cont(DsFile->path);
}

/* 0x1672c */
ds_init(a, b)
{
    int r;

    r = ds_init_channels(a, b);
    DsPcb = 0x100;
    return r;
}

/* 0x16746: wait until the disc position moves, then stop playback */
ds_abort()
{
    long pos;

    if (DsFile == 0)
        return -1;
    pos = _gs_pos(DsFile->path);
    if (DsPcb & 0x100)
        return pos;
    while (_gs_pos(DsFile->path) == pos)
        ;
    return ss_abort(DsFile->path);
}

/* 0x168a2 */
ds_open(f, name)
DS_FILE *f;
char *name;
{
    int path;

    if (f == 0)
        path = -1;
    else {
        memset(f, 0xff, sizeof(DS_FILE));
        path = open(name, 1);
    }
    if (path != -1)
        f->path = path;
    return path;
}

/* 0x168f0 */
ds_pause()
{
    if (DsFile == 0)
        return -1;
    return ss_pause(DsFile->path);
}

/* 0x16fde: a 0x80-byte queue of dispatch entries in the given memory colour */
ds_dispatcher_init(color)
int color;
{
    DS_dispatch_Q_start = srqcmem(0x80, color);
    DS_dispatch_Q_end = DS_dispatch_Q_start + 0x78;
    DS_dispatch_Q_head = DS_dispatch_Q_start;
    DS_dispatch_Q_tail = DS_dispatch_Q_start;
}

/* 0x170de: free space on the player's NVRAM, or -1 if there is none */
nvr_available()
{
    int path, free;

    if ((path = open("/nvr", 0x80)) == -1)
        return -1;
    free = gs_free(path);
    close(path);
    return free;
}
