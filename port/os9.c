/*
 * os9.c - emulated OS-9/68000 system calls ("trap #0" + call number word) for the port.
 *
 * Register conventions follow the OS-9/68K technical manual: arguments and results in d0-d3/a0-a2,
 * carry set + d1 = error code on failure. Files on the disc are read from fs_root (your extracted
 * disc), NVRAM saves go to build/nvr. Unknown calls stop the program so they can be added.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "m68k.h"
#include "runtime.h"

#define E_PNNF 0xD8   /* path name not found */
#define E_MNF  0xDD   /* module not found */
#define E_BPNUM 0xC9  /* bad path number */
#define E_EOF  0xD3

#define MAXPATH 32
typedef struct { FILE *f; int device; char name[128]; } Path;
static Path paths[MAXPATH];

static uint32_t heap = 0x200000;   /* memory handed out by F$SRqMem / F$Link */
unsigned press_every;               /* --press N: simulate a button press every N pointer polls */

static void ok(void)            { cpu.c = 0; }
static void fail(uint32_t err)  { cpu.c = 1; cpu.d[1] = err; }

static uint32_t alloc(uint32_t size)
{
    uint32_t p = (heap + 15) & ~15u;
    heap = p + size;
    if (heap >= RAM_SIZE) { fprintf(stderr, "out of emulated memory\n"); exit(4); }
    memset(RAM + p, 0, size);
    return p;
}

static void cstring(uint32_t a, char *out, int max)
{
    int i = 0;
    /* OS-9 names end at a NUL, CR, space or any byte with bit 7 set */
    while (i < max - 1) {
        uint8_t c = RAM[(a + i) & RAM_MASK];
        if (!c || c == '\r' || c == ' ') break;
        out[i++] = (char)(c & 0x7F);
        if (c & 0x80) break;
    }
    out[i] = 0;
}

static void host_path(const char *name, char *out, int max)
{
    const char *p = name;
    if (!strncmp(p, "/nvr", 4)) { snprintf(out, max, "build/nvr%s", p + 4); return; }
    if (!strncmp(p, "/cd/", 4)) p += 4;
    while (*p == '/') p++;
    snprintf(out, max, "%s/%s", fs_root, p);
}

static int new_path(void)
{
    int i;
    for (i = 3; i < MAXPATH; i++)
        if (!paths[i].f && !paths[i].device) return i;
    return -1;
}

void os9_init(void)
{
    memset(paths, 0, sizeof paths);
    paths[0].f = stdin;
    paths[1].f = stdout;
    paths[2].f = stderr;
}

static void io_open(int create)
{
    char name[128], host[512];
    int p = new_path();
    uint32_t a = cpu.a[0];
    cstring(a, name, sizeof name);
    cpu.a[0] = a + (uint32_t)strlen(name);
    if (p < 0) { fail(E_BPNUM); return; }
    /* The player's Configuration Status Descriptor: "<device type>:<device name>:" lines.
       Types the game asks for: 1 pointer, 2 audio, 3 video, 4 NVRAM, 11 second input device. */
    if (!strcmp(name, "/csd") || !strcmp(name, "/nvr/csd")) {
        static const char csd[] = "1:/ptr:\r2:/snd:\r3:/vid:\r4:/nvr:\r11:/ptr2:\r";
        paths[p].f = tmpfile();
        fwrite(csd, 1, sizeof csd - 1, paths[p].f);
        rewind(paths[p].f);
        strcpy(paths[p].name, name);
        if (trace) fprintf(stderr, "  [os9] open /csd -> %d (virtual)\n", p);
        cpu.d[0] = (uint32_t)p;
        ok();
        return;
    }
    /* CD-RTOS devices (video, audio, pointer...) are handled by the CD-i layer later */
    if (!strcmp(name, "/nvr")) {
        paths[p].device = 1;
    } else if (name[0] == '/' && !strchr(name + 1, '/') && !strstr(name, ".")) {
        paths[p].device = 1;
        fprintf(stderr, "  [os9] open device %s -> path %d (stub)\n", name, p);
    } else {
        host_path(name, host, sizeof host);
        paths[p].f = fopen(host, create ? "w+b" : ((cpu.d[0] & 2) ? "r+b" : "rb"));
        if (!paths[p].f && (cpu.d[0] & 2)) paths[p].f = fopen(host, "w+b");
        if (!paths[p].f) {
            if (trace) fprintf(stderr, "  [os9] open %s failed\n", name);
            fail(E_PNNF);
            return;
        }
    }
    strncpy(paths[p].name, name, sizeof paths[p].name - 1);
    if (trace) fprintf(stderr, "  [os9] open %s -> %d\n", name, p);
    cpu.d[0] = (uint32_t)p;
    ok();
}

static Path *path_arg(void)
{
    uint32_t p = cpu.d[0] & 0xFFFF;
    if (p >= MAXPATH || (!paths[p].f && !paths[p].device)) { fail(E_BPNUM); return 0; }
    return &paths[p];
}

static void io_rw(int write, int line)
{
    Path *p = path_arg();
    uint32_t n = cpu.d[1], a = cpu.a[0], done = 0;
    if (!p) return;
    if (!p->f) { cpu.d[1] = write ? n : 0; if (write) ok(); else fail(E_EOF); return; }
    if (write) {
        for (done = 0; done < n; done++) {
            int c = RAM[(a + done) & RAM_MASK];
            fputc(c == '\r' && p->f != stdout && p->f != stderr ? c : (c == '\r' ? '\n' : c), p->f);
            if (line && c == '\r') { done++; break; }
        }
        fflush(p->f);
    } else {
        while (done < n) {
            int c = fgetc(p->f);
            if (c == EOF) break;
            RAM[(a + done++) & RAM_MASK] = (uint8_t)c;
            if (line && c == '\r') break;
        }
        if (!done && n) { fail(E_EOF); return; }
    }
    cpu.d[1] = done;
    ok();
}

static void link_module(void)
{
    char name[128], host[512];
    FILE *f;
    long size;
    uint32_t a = cpu.a[0], m;
    cstring(a, name, sizeof name);
    cpu.a[0] = a + (uint32_t)strlen(name);
    host_path(name, host, sizeof host);
    if (!(f = fopen(host, "rb"))) {
        fprintf(stderr, "  [os9] link %s: module not found\n", name);
        fail(E_MNF);
        return;
    }
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    m = alloc((uint32_t)size);
    fread(RAM + m, 1, (size_t)size, f);
    fclose(f);
    cpu.a[2] = m;
    cpu.a[1] = m + rd32(m + 0x30);
    cpu.d[0] = rd16(m + 0x12);
    cpu.d[1] = rd16(m + 0x14);
    fprintf(stderr, "  [os9] load %s -> 0x%x (%ld bytes)\n", name, m, size);
    ok();
}

/* F$Event: d1.w = function. Events are kept as simple counters; waits return at once for now
   (the real vertical-blank timing comes from the video layer later). */
static int32_t events[16];
static char event_names[16][32];
static int nevents = 1;

static void os9_event(void)
{
    char name[32];
    int i, fn = cpu.d[1] & 0xFFFF;
    switch (fn) {
    case 0:   /* Ev$Link: a0 = name -> d0 = id */
    case 2:   /* Ev$Creat: d0 = initial value, a0 = name -> d0 = id */
        cstring(cpu.a[0], name, sizeof name);
        for (i = 1; i < nevents; i++)
            if (!strcmp(event_names[i], name)) break;
        if (i == nevents && nevents < 16) {
            strcpy(event_names[nevents], name);
            events[nevents] = fn == 2 ? (int32_t)cpu.d[0] : 0;
            nevents++;
        }
        if (trace) fprintf(stderr, "  [os9] event %s %s -> %d\n", fn ? "create" : "link", name, i);
        cpu.d[0] = (uint32_t)i;
        ok();
        break;
    case 4:   /* Ev$Wait: d0 = id, d2/d3 = range -> d1 = value */
    case 5:   /* Ev$WaitR */
    case 6:   /* Ev$Read */
        rt_poll();
        cpu.d[1] = (uint32_t)events[cpu.d[0] & 15];
        ok();
        break;
    default:  /* unlink, delete, info, signal, pulse, set, setr */
        if (fn == 0xA || fn == 0xB) events[cpu.d[0] & 15] = (int32_t)cpu.d[2];
        ok();
        break;
    }
}

void os9_trap(int n, uint32_t code)
{
    if (n != 0) {
        fprintf(stderr, "stopped: trap #%d (call 0x%x) - trap handler modules not emulated yet\n", n, code);
        exit(5);
    }
    if (trace) fprintf(stderr, "  [os9] call 0x%02x\n", code);
    switch (code) {
    case 0x00:                                                         /* F$Link */
    case 0x01: link_module(); break;                                   /* F$Load: same, from the disc */
    case 0x02: ok(); break;                                            /* F$UnLink */
    case 0x03: {                                                       /* F$Fork: a0 = module name */
        char name[64];
        cstring(cpu.a[0], name, sizeof name);
        /* Child programs (the cdi_bumper Philips logo) aren't recompiled: pretend they ran and exited. */
        fprintf(stderr, "  [os9] fork %s skipped (child program not recompiled)\n", name);
        cpu.d[0] = 3;
        ok();
        break;
    }
    case 0x04: cpu.d[0] = 3; cpu.d[1] = 0; ok(); break;                /* F$Wait: the skipped child exited OK */
    case 0x06:                                                         /* F$Exit */
        fprintf(stderr, "program exited with status %u\n", cpu.d[1] & 0xFFFF);
        exit((int)(cpu.d[1] & 0xFF));
    case 0x07: cpu.d[0] = cpu.d[0] ? cpu.d[0] : 0x2600; cpu.a[1] = DATA_BASE + cpu.d[0]; ok(); break; /* F$Mem */
    case 0x09: cdrtos_set_intercept(cpu.a[0], cpu.a[6]); ok(); break;  /* F$Icpt: a0 handler, a6 its data */
    case 0x0A: rt_poll(); cpu.d[0] = 0; ok(); break;                   /* F$Sleep */
    case 0x1E: cdrtos_rte(); ok(); break;                              /* F$RTE: end of signal intercept */
    case 0x0C: cpu.d[0] = 2; cpu.d[1] = 0; ok(); break;                /* F$ID */
    case 0x15: {                                                       /* F$Time: Gregorian */
        time_t t = time(0);
        struct tm *tm = localtime(&t);
        cpu.d[0] = (uint32_t)(tm->tm_hour << 16 | tm->tm_min << 8 | tm->tm_sec);
        cpu.d[1] = (uint32_t)((tm->tm_year + 1900) << 16 | (tm->tm_mon + 1) << 8 | tm->tm_mday);
        cpu.d[2] = (uint32_t)tm->tm_wday;
        cpu.d[3] = 0;
        ok();
        break;
    }
    case 0x1D: ok(); break;                                            /* F$UnLoad */
    case 0x21: cpu.a[1] = cpu.a[2] = 0; ok(); break;                   /* F$TLink: trap handlers are native */
    case 0x27:                                                         /* F$SetSys: d0 offset, d1 size (bit 31 = read) */
        if (trace) fprintf(stderr, "  [os9] system global 0x%x %s\n", cpu.d[0], (cpu.d[1] & 0x80000000u) ? "read" : "write");
        cpu.d[2] = 0;
        ok();
        break;
    case 0x25: {                                                       /* F$DatMod: d0 size, d1 attr/rev, a0 name */
        char name[64];
        uint32_t namelen, hdr, m;
        cstring(cpu.a[0], name, sizeof name);
        namelen = (uint32_t)strlen(name) + 1;
        hdr = 0x48 + ((namelen + 1) & ~1u);
        m = alloc(hdr + cpu.d[0] + 4);
        wr16(m, 0x4AFC);
        wr32(m + 0x04, hdr + cpu.d[0] + 4);    /* M$Size */
        wr32(m + 0x0C, 0x48);                   /* M$Name */
        wr16(m + 0x12, 0x0400);                 /* type data */
        wr16(m + 0x14, cpu.d[1]);
        wr32(m + 0x30, hdr);                    /* data starts after the name */
        memcpy(RAM + m + 0x48, name, namelen);
        cpu.a[0] += (uint32_t)strlen(name);
        cpu.a[1] = m + hdr;
        cpu.a[2] = m;
        cpu.d[0] = 0x0400;
        if (trace) fprintf(stderr, "  [os9] data module %s, %u bytes at 0x%x\n", name, hdr + cpu.d[0], m);
        ok();
        break;
    }
    case 0x28: cpu.a[2] = alloc(cpu.d[0]); ok(); break;                /* F$SRqMem */
    case 0x53: os9_event(); break;                                     /* F$Event */
    case 0x57: ok(); break;                                            /* F$SigMask: signals are delivered by the runtime later */
    case 0x5C:                                                         /* F$SRqCMem: d0 size, d1 colour (video RAM etc.) */
        if (trace) fprintf(stderr, "  [os9] request 0x%x bytes of memory colour 0x%x\n", cpu.d[0], cpu.d[1]);
        cpu.a[2] = alloc(cpu.d[0]);
        ok();
        break;
    case 0x29: ok(); break;                                            /* F$SRtMem */
    case 0x83: io_open(1); break;                                      /* I$Create */
    case 0x84: io_open(0); break;                                      /* I$Open */
    case 0x88: {                                                       /* I$Seek */
        Path *p = path_arg();
        if (p) { if (p->f) fseek(p->f, (long)cpu.d[1], SEEK_SET); ok(); }
        break;
    }
    case 0x89: io_rw(0, 0); break;                                     /* I$Read */
    case 0x8A: io_rw(1, 0); break;                                     /* I$Write */
    case 0x8B: io_rw(0, 1); break;                                     /* I$ReadLn */
    case 0x8C: io_rw(1, 1); break;                                     /* I$WritLn */
    case 0x8D:                                                         /* I$GetStt */
    case 0x8E: {                                                       /* I$SetStt */
        Path *p = path_arg();
        uint32_t fn = cpu.d[1] & 0xFFFF;
        if (!p) break;
        if (p->f && code == 0x8D && fn == 0x02) {                      /* SS_Size */
            long cur = ftell(p->f);
            fseek(p->f, 0, SEEK_END);
            cpu.d[2] = (uint32_t)ftell(p->f);
            fseek(p->f, cur, SEEK_SET);
        } else if (p->f && code == 0x8D && fn == 0x05) {               /* SS_Pos */
            cpu.d[2] = (uint32_t)ftell(p->f);
        } else if (code == 0x8E && fn == 0x56) {                       /* dc_*: video chip display program */
            display_setstt();
        } else if (code == 0x8D && fn == 0x59) {                       /* pointer coordinates (pt_coord) */
            /* d0.w = buttons, d1 = y << 16 | x, from the window's keyboard/gamepad state.
               --press N also simulates a button 1 press every N polls (for headless testing). */
            static unsigned polls;
            int px, py, buttons;
            polls++;
            window_pointer(&px, &py, &buttons);
            if (press_every && polls % press_every < 20) buttons |= 1;
            cpu.d[0] = (uint32_t)buttons;
            cpu.d[1] = ((uint32_t)py << 16) | (uint32_t)px;
        } else if (p->f && code == 0x8E && fn == 0x2D) {               /* SS_Play: a0 = play control block */
            cdrtos_play(p->f, cpu.a[0]);
        } else if (code == 0x8E && (fn == 0x31 || fn == 0x36)) {       /* SS_Abort, SS_Stop */
            cdrtos_stop();
        } else if (code == 0x8E && (fn == 0x33 || fn == 0x37)) {       /* SS_Pause, SS_Cont */
            cdrtos_pause(fn == 0x33);
        } else {
            /* CD-RTOS device control (display, sound, pointer, disc): logged until the CD-i layer exists */
            fprintf(stderr, "  [cd-i] %s %s fn 0x%x (d2=%x d3=%x a0=%x)\n", code == 0x8D ? "GetStt" : "SetStt",
                    p->name, fn, cpu.d[2], cpu.d[3], cpu.a[0]);
            cpu.d[0] = cpu.d[0] & 0xFFFF;
        }
        ok();
        break;
    }
    case 0x8F: {                                                       /* I$Close */
        Path *p = path_arg();
        if (p) {
            if (p->f && p->f != stdin && p->f != stdout && p->f != stderr) fclose(p->f);
            memset(p, 0, sizeof *p);
            ok();
        }
        break;
    }
    default:
        fprintf(stderr, "stopped: OS-9 system call 0x%02x not emulated yet (d0=%x d1=%x a0=%x)\n",
                code, cpu.d[0], cpu.d[1], cpu.a[0]);
        exit(5);
    }
}
