/*
 * runtime.c - runs the recompiled cdi_hotel program.
 *
 * Loads the program and its initialised data from the files extracted from YOUR disc
 * (build/eu/fs), sets up the registers the way OS-9 starts a process, and calls _cstart.
 * OS-9 system calls are emulated in os9.c; anything missing stops the run with a message.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "m68k.h"
#include "runtime.h"

Cpu cpu;
uint8_t RAM[RAM_SIZE];

extern const struct { uint32_t addr; RecompFn fn; const char *name; } recomp_table[];
extern const int recomp_count;

const char *fs_root = "build/eu/fs";
int trace = 0;
uint32_t watch_pc = 0xFFFFFFFFu;

void watch_hit(uint32_t addr)
{
    fprintf(stderr, "  [watch] 0x%x: d0=%08x d1=%08x a0=%08x a7=%08x ret=%08x\n", addr, cpu.d[0], cpu.d[1], cpu.a[0],
            cpu.a[7], rd32(cpu.a[7]));
}

static const char *name_of(uint32_t addr)
{
    int lo = 0, hi = recomp_count - 1, best = -1;
    while (lo <= hi) {                       /* last entry at or below addr */
        int mid = (lo + hi) / 2;
        if (recomp_table[mid].addr <= addr) { best = mid; lo = mid + 1; } else hi = mid - 1;
    }
    return best >= 0 ? recomp_table[best].name : "?";
}

RecompFn recomp_lookup(uint32_t addr);
#define lookup recomp_lookup

RecompFn recomp_lookup(uint32_t addr)
{
    int lo = 0, hi = recomp_count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (recomp_table[mid].addr == addr) return recomp_table[mid].fn;
        if (recomp_table[mid].addr < addr) lo = mid + 1; else hi = mid - 1;
    }
    return 0;
}

void call_addr(uint32_t addr, uint32_t ret)
{
    RecompFn fn;
    /* calls through _jmptbl land on a "jmp abs.l" in the data area */
    if (rd16(addr) == 0x4EF9 && !lookup(addr))
        addr = rd32(addr + 2);
    fn = lookup(addr);
    if (!fn) {
        /* no recompiled version (e.g. a code module streamed from the disc): interpret it */
        interp_call(addr);
        return;
    }
    if (trace) fprintf(stderr, "  call %s\n", name_of(addr));
    PUSH32(ret);
    fn();
}

void jump_far(uint32_t addr)
{
    RecompFn fn = lookup(addr);
    if (fn) {                               /* jmp to a function start = tail call */
        if (trace) fprintf(stderr, "  jump %s\n", name_of(addr));
        fn();
        return;
    }
    /* Otherwise the code popped its own return address and jumped back to it (e.g. _initarg's
       movea.l (a7)+,a5 ... jmp (a5)); returning from the C function does the same. */
    if (trace) fprintf(stderr, "  jump back to 0x%x in %s\n", addr, name_of(addr));
}

void unimpl(uint32_t addr, const char *what)
{
    fprintf(stderr, "stopped: untranslated code at 0x%x in %s (%s)\n", addr, name_of(addr), what);
    exit(3);
}

void m68k_divzero(void)
{
    fprintf(stderr, "stopped: division by zero\n");
    exit(3);
}

static long load_file(const char *name, uint8_t *buf, long max)
{
    char path[512];
    FILE *f;
    long n;
    snprintf(path, sizeof path, "%s/%s", fs_root, name);
    if (!(f = fopen(path, "rb"))) {
        fprintf(stderr, "can't open %s - run tools/setup.py with your disc image first\n", path);
        exit(1);
    }
    n = (long)fread(buf, 1, max, f);
    fclose(f);
    return n;
}

int main(int argc, char **argv)
{
    static uint8_t datamod[0x10000];
    uint32_t off, size, memsize, params;
    int i;
    void f_000052(void); /* _cstart */

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--trace")) trace = 1;
        else if (!strcmp(argv[i], "--watch") && i + 1 < argc) watch_pc = (uint32_t)strtoul(argv[++i], 0, 16);
        else if (!strcmp(argv[i], "--press") && i + 1 < argc) press_every = (unsigned)atoi(argv[++i]);
        else if (!strcmp(argv[i], "--headless")) headless = 1;
        else if (!strcmp(argv[i], "--dump-at") && i + 1 < argc) dump_at_ms = (unsigned)atoi(argv[++i]);
        else if (!strcmp(argv[i], "--dump-every") && i + 1 < argc) dump_every_ms = (unsigned)atoi(argv[++i]);
        else fs_root = argv[i];
    }

    /* program module at address 0, initialised data from the cdi_hotel_data module */
    load_file("cdi_hotel", RAM, 0x40000);
    load_file("cdi_hotel_data", datamod, sizeof datamod);
    off = (datamod[0x44] << 24) | (datamod[0x45] << 16) | (datamod[0x46] << 8) | datamod[0x47];
    size = (datamod[0x48] << 24) | (datamod[0x49] << 16) | (datamod[0x4a] << 8) | datamod[0x4b];
    memcpy(RAM + DATA_BASE + off, datamod + 0x4c, size);

    /* Relocate pointers in the initialised data, as OS-9 does at load time. After the data come two
       tables (code references, then data references), each a list of groups:
       u16 high word, u16 count, count x u16 low word (offsets into the data area); count 0 ends it. */
    {
        uint32_t p = 0x4c + size, bases[2] = { 0 /* program is loaded at 0 */, DATA_BASE };
        int t, fixed[2] = { 0, 0 };
        for (t = 0; t < 2; t++) {
            for (;;) {
                uint32_t hi = (datamod[p] << 8) | datamod[p + 1];
                uint32_t cnt = (datamod[p + 2] << 8) | datamod[p + 3];
                p += 4;
                if (!cnt) break;
                while (cnt--) {
                    uint32_t at = DATA_BASE + ((hi << 16) | (datamod[p] << 8) | datamod[p + 1]);
                    wr32(at, rd32(at) + bases[t]);
                    fixed[t]++;
                    p += 2;
                }
            }
        }
        fprintf(stderr, "relocated %d code and %d data pointers in the initialised data\n", fixed[0], fixed[1]);
    }

    /* OS-9 process start: a3 = module, a6 = data + 0x8000, a5/d5 = parameter string, d6 = memory size */
    memsize = rd32(0x38) + rd32(0x3c);     /* M$Mem (data) + M$Stack */
    memsize = (memsize + 0xFFF) & ~0xFFFu;
    params = DATA_BASE + memsize - 0x100;
    strcpy((char *)RAM + params, "\r");
    memset(&cpu, 0, sizeof cpu);
    cpu.a[3] = 0;
    cpu.a[6] = DATA_BASE + 0x8000;
    cpu.a[5] = params;
    cpu.d[5] = 2;
    cpu.d[6] = memsize;
    cpu.d[3] = 3;                           /* stdin, stdout, stderr inherited */
    cpu.a[1] = DATA_BASE + memsize;         /* top of the process memory */
    cpu.a[7] = params;                      /* stack grows down below the parameters */
    os9_init();
    window_init();
    if (trace) atexit(display_report);

    fprintf(stderr, "starting cdi_hotel: data 0x%x-0x%x, stack top 0x%x\n", DATA_BASE, DATA_BASE + memsize, cpu.a[7]);
    PUSH32(0xFFFFFFFFu);                    /* return address for _cstart; it should exit via F$Exit */
    f_000052();
    fprintf(stderr, "_cstart returned (d0=%x)\n", cpu.d[0]);
    return 0;
}
