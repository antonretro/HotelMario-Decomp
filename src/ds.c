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

/* 0x16796: link the file's .map module (loading it from the current directory if needed) */
ds_link_map_module(f, name)
DS_FILE *f;
char *name;
{
    int r = 0;
    char *m;
    char *d;
    long *hdr;

    if (f) {
        if ((m = (char *)modlink(name, 0)) == (char *)-1)
            m = (char *)modloadp(name, 0, 0);
        f->map = m;
        if (m != (char *)-1) {
            hdr = (long *)(m + 0x30);   /* module header: offset of the module's data */
            d = m + *hdr;
            ds_link_map_data(f, d);
            return r;
        }
    }
    r = -1;
    return r;
}

/* 0x1681e: point the file at the tables inside the map data (header: count, then three short offsets) */
ds_link_map_data(f, d)
DS_FILE *f;
short *d;
{
    int r = 0;

    if (f) {
        f->map_count = d[0];
        f->map_tab14 = (char *)d + d[1];
        f->map_tab10 = (char *)d + d[2];
        f->map_tab18 = (char *)d + d[3];
        f->map_entries = (char *)(d + 4);
    } else
        r = -1;
    return r;
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
    DS_dispatch_Q_start = (DS_DISPATCH *)srqcmem(0x80, color);
    DS_dispatch_Q_end = (DS_DISPATCH *)((char *)DS_dispatch_Q_start + 0x78);
    DS_dispatch_Q_head = DS_dispatch_Q_start;
    DS_dispatch_Q_tail = DS_dispatch_Q_start;
}

/* 0x16b2e: start decoding a run of the stream map. A negative count byte means "the next byte, repeated
   -n+1 times"; a positive one means "the next n+1 bytes". */
unsigned char *ds_init_decode(p, d)
unsigned char *p;
DS_DECODE *d;
{
    short n;

    if ((char)*p < 0) {
        d->literal = 0;
        n = -(char)*p;
    } else {
        d->literal = 1;
        n = (char)*p;
    }
    d->count = n + 1;
    d->p = p + 1;
    return d->p;
}

/* 0x16b7c: next value from the stream map (a buffer size in sectors) */
ds_decode_size(d)
DS_DECODE *d;
{
    if (d->count == 0)                          /* run finished: the next count byte follows it */
        ds_init_decode(d->literal ? d->p : d->p + 1, d);
    d->count--;
    if (!d->literal)
        return *d->p;
    return *d->p++;
}

/* 0x16fde..0x17076: a small queue of deferred calls, filled from signal handlers and run from the
   main loop (ds_dispatch_execute(n) runs up to n of them; 0 empties the queue). */
ds_dispatch_execute(n)
int n;
{
    int i;

    if (n == 0) {
        DS_dispatch_Q_head = DS_dispatch_Q_start;
        DS_dispatch_Q_tail = DS_dispatch_Q_start;
        return;
    }
    for (i = 0; i < n; i++) {
        if (DS_dispatch_Q_head != DS_dispatch_Q_tail) {
            (*DS_dispatch_Q_tail->fn)(DS_dispatch_Q_tail->arg);
            if (++DS_dispatch_Q_tail > DS_dispatch_Q_end)
                DS_dispatch_Q_tail = DS_dispatch_Q_start;
        }
    }
}

/* 0x17076: queue fn(arg) unless the queue is full */
ds_dispatch(fn, arg)
int (*fn)();
long arg;
{
    DS_DISPATCH *next = DS_dispatch_Q_head + 1;

    if (next > DS_dispatch_Q_end) {
        if (DS_dispatch_Q_tail != DS_dispatch_Q_start) {
            DS_dispatch_Q_head->fn = fn;
            DS_dispatch_Q_head->arg = arg;
            DS_dispatch_Q_head = DS_dispatch_Q_start;
        }
    } else if (DS_dispatch_Q_tail != next) {
        DS_dispatch_Q_head->fn = fn;
        DS_dispatch_Q_head->arg = arg;
        DS_dispatch_Q_head = next;
    }
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
