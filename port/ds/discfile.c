/*
 * discfile.c - the DS port's files: everything the game opens comes straight out of YOUR disc image
 * (one raw 2352-byte/sector .bin on the SD card), so nothing has to be extracted first.
 *
 * It reads the same way as tools/cdi_fs.py: the CD-I disc label, the path table, then each directory. Files then
 * look exactly as tools/cdi_fs.py extract writes them, because the runtime's offsets expect that:
 *   real-time files (.rtf)  raw 2352-byte sectors
 *   everything else         2048-byte blocks, cut to the file's size
 * Anything else (the options file) stays an ordinary file, and the Configuration Status Descriptor is a block of
 * memory. Saved games are the CD-i's NVRAM: a block of memory like the real chip, copied to the SD card whenever it
 * changes. Without a card they last until the power goes off. The game must never find it missing: it creates its
 * save file at start-up, and a failed create shows "your CD-i player's memory is full".
 *
 * port/ds/ds_prefix.h maps fopen, fread and friends here, so os9.c and cdrtos.c need no changes.
 */
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "discfile.h"

#ifdef __BLOCKSDS__
#include <fat.h>
#include <filesystem.h>
#endif

#ifndef O_BINARY
#define O_BINARY 0
#endif

#define RAW 2352
#define BLOCK 2048
#define MAX_FILES 128
#define CACHE_SECTORS 8            /* the SD card reads whole runs at once, and the stream is sequential */

typedef struct {
    char name[32];
    uint32_t lba, size;
    int raw;
} DiscEntry;

struct DFile {
    int open, nvr;
    const DiscEntry *e;            /* 0 = a block of memory */
    const uint8_t *mem;
    uint32_t pos, size;
};
#define MAX_OPEN 16

static int image = -1;
static DiscEntry entries[MAX_FILES];
static int nentries;
/* The slots are never freed. The game can close a file while the disc is still streaming from it (when a stage
   starts), and then the stream has to find it closed, read nothing, and end like any other stream. */
static struct DFile files[MAX_OPEN];
static int next_slot;

#define NVR_SIZE 8192              /* as on a CD-i player */
static uint8_t nvr[NVR_SIZE];
static uint32_t nvr_size;
static int nvr_loaded, nvr_exists, nvr_dirty;
static char nvr_path[64];
static uint8_t *cache;             /* CACHE_SECTORS raw sectors */
static uint32_t cache_lba = 0xFFFFFFFFu;

static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }

static const uint8_t *raw_sector(uint32_t lba)
{
    if (lba < cache_lba || lba >= cache_lba + CACHE_SECTORS) {
        long got;
        lseek(image, (long)lba * RAW, SEEK_SET);
        got = read(image, cache, CACHE_SECTORS * RAW);
        cache_lba = got >= RAW ? lba : 0xFFFFFFFFu;
        if (got < RAW) return 0;
        if (got < CACHE_SECTORS * RAW) memset(cache + got, 0, (size_t)(CACHE_SECTORS * RAW - got));
    }
    return cache + (lba - cache_lba) * RAW;
}

static void add_file(const char *name, uint32_t lba, uint32_t size)
{
    size_t n = strlen(name);
    DiscEntry *e;
    if (nentries == MAX_FILES || n >= sizeof e->name) return;
    e = &entries[nentries++];
    strcpy(e->name, name);
    e->lba = lba;
    e->size = size;
    e->raw = n > 4 && !strcmp(name + n - 4, ".rtf");
}

/* One directory: records of length, ..., start (+6), size (+14), name length (+32), name (+33), then the CD-I
   system use bytes, whose attributes (+4 after the name) say what is a directory. */
static void read_directory(uint32_t lbn)
{
    uint32_t total = 0, lba = lbn, done = 0;
    for (;;) {
        const uint8_t *b = raw_sector(lba);
        int i = 0;
        if (!b) return;
        b += 24;
        while (i < BLOCK && b[i]) {
            const uint8_t *r = b + i;
            int rl = r[0], nlen = r[32];
            uint32_t start = be32(r + 6), size = be32(r + 14);
            const uint8_t *su = r + 33 + nlen + ((nlen + 1) & 1);
            int attr = su + 6 <= r + rl ? su[4] << 8 | su[5] : 0;
            if (!total && nlen == 1 && !r[33]) total = size;
            if (!(nlen == 1 && r[33] <= 1) && !(attr & 0x8000)) {
                char name[33];
                memcpy(name, r + 33, (size_t)nlen);
                name[nlen] = 0;
                add_file(name, start, size);
            }
            i += rl;
        }
        lba++;
        done += BLOCK;
        if (!total || done >= total) return;
    }
}

int disc_mount(const char *path)
{
    uint8_t *pt;
    uint32_t lba, addr = 0, size = 0, i = 0;
    int ok = 0;
    if ((image = open(path, O_RDONLY | O_BINARY)) < 0) return 0;
    if (!cache && !(cache = malloc(CACHE_SECTORS * RAW))) return 0;
#ifdef __BLOCKSDS__
    /* seeking back in a big file would otherwise walk its whole cluster chain */
    if (!strncmp(path, "nitro:", 6)) nitroFSInitLookupCache(16 * 1024);
    else fatInitLookupCache(image, 16 * 1024);
#endif
    for (lba = 16; lba < 32 && !ok; lba++) {
        const uint8_t *b = raw_sector(lba);
        if (!b) return 0;
        b += 24;
        if (!memcmp(b + 1, "CD-I ", 5)) {
            size = be32(b + 136);
            addr = be32(b + 148);
            ok = 1;
        }
    }
    if (!ok || !size || size > 4 * BLOCK || !(pt = malloc(size + BLOCK))) return 0;
    for (lba = 0; lba * BLOCK < size; lba++) {
        const uint8_t *b = raw_sector(addr + lba);
        if (!b) return 0;
        memcpy(pt + lba * BLOCK, b + 24, BLOCK);
    }
    while (i + 8 <= size && pt[i]) {          /* the path table: every directory on the disc */
        int nlen = pt[i];
        read_directory(be32(pt + i + 2));
        i += 8 + (uint32_t)nlen + (nlen & 1);
    }
    free(pt);
    return nentries;
}

static FILE *new_file(const DiscEntry *e, const void *mem, uint32_t size)
{
    int i;
    for (i = 0; i < MAX_OPEN; i++) {
        struct DFile *d = &files[(next_slot + i) % MAX_OPEN];
        if (d->open) continue;
        next_slot = (next_slot + i + 1) % MAX_OPEN;
        d->open = 1;
        d->nvr = 0;
        d->e = e;
        d->mem = mem;
        d->pos = 0;
        d->size = size;
        return (FILE *)d;
    }
    return 0;
}

/* stdin, stdout, stderr and real files are the C library's: they go past this layer */
static struct DFile *is_ours(FILE *f)
{
    struct DFile *d = (struct DFile *)f;
    return d >= files && d < files + MAX_OPEN ? d : 0;
}

static void nvr_store(void)
{
    FILE *f;
    if (!nvr_dirty) return;
    nvr_dirty = 0;
    if ((f = (fopen)(nvr_path, "wb"))) {
        fwrite(nvr, 1, nvr_size, f);
        (fclose)(f);
    }
}

static FILE *nvr_open(const char *path, const char *mode)
{
    FILE *f;
    if (!nvr_loaded) {
        nvr_loaded = 1;
        snprintf(nvr_path, sizeof nvr_path, "%s", path);
        if ((f = (fopen)(path, "rb"))) {
            nvr_size = (uint32_t)(fread)(nvr, 1, NVR_SIZE, f);
            nvr_exists = 1;
            (fclose)(f);
        }
    }
    if (mode[0] == 'w') {
        nvr_exists = nvr_dirty = 1;
        nvr_size = 0;
    }
    if (!nvr_exists) return 0;
    f = new_file(0, nvr, nvr_size);
    if (f) ((struct DFile *)f)->nvr = 1;
    return f;
}

FILE *disc_fopen(const char *path, const char *mode)
{
    const char *base = strrchr(path, '/');
    int i;
    base = base ? base + 1 : path;
    if (!strncmp(path, "saves/", 6)) return nvr_open(path, mode);
    if (mode[0] == 'r' && mode[1] != '+')
        for (i = 0; i < nentries; i++)
            if (!strcmp(entries[i].name, base))
                return new_file(&entries[i], 0, entries[i].raw ? (entries[i].size + BLOCK - 1) / BLOCK * RAW
                                                               : entries[i].size);
    return (fopen)(path, mode);
}

FILE *disc_fmemopen(const void *data, size_t size)
{
    return new_file(0, data, (uint32_t)size);
}

size_t disc_fread(void *out, size_t size, size_t count, FILE *f)
{
    struct DFile *d = is_ours(f);
    uint8_t *to = out;
    uint32_t want = (uint32_t)(size * count), done = 0;
    if (!d) return (fread)(out, size, count, f);
    if (!d->open) return 0;
    if (d->nvr) d->size = nvr_size;
    if (d->pos + want > d->size) want = d->pos < d->size ? d->size - d->pos : 0;
    if (!d->e) {
        memcpy(to, d->mem + d->pos, want);
        done = want;
    }
    while (done < want) {
        uint32_t unit = d->e->raw ? RAW : BLOCK;
        uint32_t off = d->pos + done, in = off % unit, n = unit - in;
        const uint8_t *s = raw_sector(d->e->lba + off / unit);
        if (!s) break;
        if (n > want - done) n = want - done;
        memcpy(to + done, s + (d->e->raw ? 0 : 24) + in, n);
        done += n;
    }
    d->pos += done;
    return size ? done / size : 0;
}

int disc_fgetc(FILE *f)
{
    struct DFile *d = is_ours(f);
    unsigned char c;
    if (!d) return (fgetc)(f);
    return disc_fread(&c, 1, 1, f) == 1 ? c : EOF;
}

int disc_fseek(FILE *f, long off, int whence)
{
    struct DFile *d = is_ours(f);
    if (!d) return (fseek)(f, off, whence);
    if (!d->open) return -1;
    if (d->nvr) d->size = nvr_size;
    d->pos = (uint32_t)(whence == SEEK_SET ? off : whence == SEEK_CUR ? (long)d->pos + off : (long)d->size + off);
    return 0;
}

long disc_ftell(FILE *f)
{
    struct DFile *d = is_ours(f);
    return d ? (long)d->pos : (ftell)(f);
}

int disc_fclose(FILE *f)
{
    struct DFile *d = is_ours(f);
    if (!d) return (fclose)(f);
    if (d->nvr) nvr_store();
    d->open = 0;
    return 0;
}

int disc_fputc(int c, FILE *f)
{
    struct DFile *d = is_ours(f);
    if (!d) return (fputc)(c, f);
    if (!d->open || !d->nvr || d->pos >= NVR_SIZE) return EOF;
    nvr[d->pos++] = (uint8_t)c;
    if (d->pos > nvr_size) nvr_size = d->pos;
    nvr_dirty = 1;
    return c;
}

int disc_fflush(FILE *f)
{
    struct DFile *d = is_ours(f);
    if (!d) return (fflush)(f);
    if (d->nvr) nvr_store();
    return 0;
}
