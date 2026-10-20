/*
 * interp.c - run 68000 code that has no recompiled version (the code modules the game streams
 * from the disc) on the Musashi interpreter, sharing RAM and registers with the recompiled code.
 *
 * Musashi 68000 core: Copyright 1998-2002 Karl Stenerud, MIT licence (tools/ext/Musashi).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "m68k.h"                         /* the port's CPU state (port/m68k.h) */
#include "runtime.h"
#include "../tools/ext/Musashi/m68k.h"     /* Musashi's API */

#define SENTINEL 0xFFF000u                 /* interpreted calls return here */

static int ready, running, done;
static int nest_depth;                     /* interpreted calls running inside another (see interp_call) */
static unsigned hook_count;

/* ---- Musashi memory interface: the same big-endian RAM the recompiled code uses ---- */
unsigned int m68k_read_memory_8(unsigned int a)  { return rd8(a); }
unsigned int m68k_read_memory_16(unsigned int a) { return rd16(a); }
unsigned int m68k_read_memory_32(unsigned int a) { return rd32(a); }
/* Nothing may write into cdi_hotel's own code (address 0 up to its size). An intermittent crash after the
   Philips logo program exits showed the code overwritten from 0x92 on: report the first such write. */
static void code_write(unsigned int a, unsigned int v, int size)
{
    static int reported;
    if ((a & RAM_MASK) >= program_size || reported) return;
    reported = 1;
    fprintf(stderr, "  [interp] write of %x (%d bytes) into the program at 0x%x, pc 0x%x, a7 0x%x\n", v, size, a,
            m68k_get_reg(NULL, M68K_REG_PPC), m68k_get_reg(NULL, M68K_REG_A7));
    interp_dump_history();
}
void m68k_write_memory_8(unsigned int a, unsigned int v)  { code_write(a, v, 1); wr8(a, v); }
void m68k_write_memory_16(unsigned int a, unsigned int v) { code_write(a, v, 2); wr16(a, v); }
void m68k_write_memory_32(unsigned int a, unsigned int v) { code_write(a, v, 4); wr32(a, v); }
unsigned int m68k_read_disassembler_8(unsigned int a)  { return rd8(a); }
unsigned int m68k_read_disassembler_16(unsigned int a) { return rd16(a); }
unsigned int m68k_read_disassembler_32(unsigned int a) { return rd32(a); }

/* ---- register exchange (user mode, so A7 is the same stack the recompiled code uses) ---- */
static void to_musashi(void)
{
    int i;
    m68k_set_reg(M68K_REG_SR, (unsigned)(cpu.x << 4 | cpu.n << 3 | cpu.z << 2 | cpu.v << 1 | cpu.c));
    for (i = 0; i < 8; i++) {
        m68k_set_reg(M68K_REG_D0 + i, cpu.d[i]);
        m68k_set_reg(M68K_REG_A0 + i, cpu.a[i]);
    }
}

static void from_musashi(void)
{
    int i;
    unsigned sr = m68k_get_reg(NULL, M68K_REG_SR);
    for (i = 0; i < 8; i++) {
        cpu.d[i] = m68k_get_reg(NULL, M68K_REG_D0 + i);
        cpu.a[i] = m68k_get_reg(NULL, M68K_REG_A0 + i);
    }
    cpu.x = (sr >> 4) & 1; cpu.n = (sr >> 3) & 1; cpu.z = (sr >> 2) & 1; cpu.v = (sr >> 1) & 1; cpu.c = sr & 1;
}

/* ---- signals for interpreted code: like OS-9, save the registers, run the intercept routine with
   d1 = signal and a6 = its data, and restore everything when it calls F$RTE ---- */
static struct { int pending, active; uint32_t sig, handler, data; Cpu saved; unsigned saved_pc; } isig;
static int rte_requested;

/* Signals wait in a queue, like OS-9's, so a frequent one (the bumper's 50 Hz tick) never pushes out a
   rare one (its end-of-play signal). The same signal is never queued twice. */
#define SIGQ 16
static struct { uint32_t sig, handler, data; } sigq[SIGQ];
static int sigq_n;

static void next_signal(void)
{
    if (isig.pending || isig.active || !sigq_n) return;
    isig.pending = 1;
    isig.sig = sigq[0].sig;
    isig.handler = sigq[0].handler;
    isig.data = sigq[0].data;
    memmove(sigq, sigq + 1, (size_t)(--sigq_n) * sizeof sigq[0]);
}

void interp_signal(uint32_t sig, uint32_t handler, uint32_t data)
{
    int i;
    if (!running) return;
    if ((isig.pending || isig.active) && isig.sig == sig) return;
    for (i = 0; i < sigq_n; i++)
        if (sigq[i].sig == sig) return;
    if (sigq_n < SIGQ) {
        sigq[sigq_n].sig = sig;
        sigq[sigq_n].handler = handler;
        sigq[sigq_n].data = data;
        sigq_n++;
    }
    next_signal();
}

void interp_rte(void)
{
    if (isig.active) rte_requested = 1;
}

static void start_signal(unsigned pc)
{
    from_musashi();
    isig.saved = cpu;
    isig.saved_pc = pc;
    cpu.d[1] = isig.sig;
    cpu.a[6] = isig.data;
    cpu.a[7] -= 0x80;
    to_musashi();
    m68k_set_reg(M68K_REG_PC, isig.handler);
    isig.pending = 0;
    isig.active = 1;
}

/* trap #n: PC already points at the OS-9 call number word that follows the trap */
int interp_trap(int trap)
{
    unsigned pc = m68k_get_reg(NULL, M68K_REG_PC);
    from_musashi();
    os9_trap(trap, rd16(pc));
    if (rte_requested) {                    /* F$RTE: back to where the signal interrupted */
        rte_requested = 0;
        isig.active = 0;
        cpu = isig.saved;
        to_musashi();
        m68k_set_reg(M68K_REG_PC, isig.saved_pc);
        next_signal();                      /* the next queued signal, if any, runs at the next instruction */
        return 1;
    }
    to_musashi();
    m68k_set_reg(M68K_REG_PC, pc + 2);
    return 1;   /* handled: no exception */
}

void interp_reset(void)
{
    running = 0;
    done = 0;
    isig.pending = isig.active = 0;
    sigq_n = 0;
    rte_requested = 0;
}

static unsigned history[64];

/* --trace: show the last 64 interpreted instructions (to see what a streamed module is waiting for) */
void interp_dump_history(void)
{
    int i;
    if (!running) return;
    fprintf(stderr, "  [interp] last instructions:\n");
    for (i = 1; i <= 64; i++) {
        unsigned h = history[(hook_count + i) & 63];
        char buf[100];
        m68k_disassemble(buf, h, M68K_CPU_TYPE_68000);
        fprintf(stderr, "    %06x  %s\n", h, buf);
    }
}

void interp_hook(unsigned int pc)
{
    static unsigned real_hist[64], real_n;     /* the last instructions that weren't blank memory */
    static unsigned jump_from[64], jump_to[64], jump_sp[64], jump_n, last_pc;   /* the last 64 jumps */
    history[hook_count & 63] = pc;
    if (rd16(pc)) real_hist[real_n++ & 63] = pc;
    if (pc < last_pc || pc > last_pc + 10) {
        jump_from[jump_n & 63] = last_pc;
        jump_to[jump_n & 63] = pc;
        jump_sp[jump_n & 63] = m68k_get_reg(NULL, M68K_REG_A7);
        jump_n++;
    }
    last_pc = pc;
    if ((pc & 1) || pc >= RAM_SIZE || m68k_get_reg(NULL, M68K_REG_A7) < 0x1000) {   /* ran off into nowhere */
        int i;
        {   /* has the program in memory been overwritten (a write through a null pointer)? */
            uint32_t a, n = 0;
            for (a = 0; a < program_size && n < 24; a++)
                if (RAM[a] != program_copy[a]) {
                    fprintf(stderr, "  program byte %06x changed: %02x -> %02x\n", a, program_copy[a], RAM[a]);
                    n++;
                }
            if (!n) fprintf(stderr, "  program in memory unchanged\n");
        }
        fprintf(stderr, "stopped: interpreter went wrong at 0x%x (a7=%x). The last jumps (from -> to, a7 after):\n",
                pc, m68k_get_reg(NULL, M68K_REG_A7));
        for (i = 0; i < 64; i++) {
            unsigned k = (jump_n + i) & 63;
            char buf[100];
            m68k_disassemble(buf, jump_from[k], M68K_CPU_TYPE_68000);
            fprintf(stderr, "  %06x -> %06x  a7=%06x   (%s)\n", jump_from[k], jump_to[k], jump_sp[k], buf);
        }
        fprintf(stderr, "stopped: interpreter at bad address 0x%x (a7=%x). Last instructions in real code:\n", pc,
                m68k_get_reg(NULL, M68K_REG_A7));
        for (i = 0; i < 64; i++) {
            unsigned h = real_hist[(real_n + i) & 63];
            char buf[100];
            m68k_disassemble(buf, h, M68K_CPU_TYPE_68000);
            fprintf(stderr, "  %06x  %s\n", h, buf);
        }
        fprintf(stderr, "Last instructions:\n");
        for (i = 1; i <= 64; i++) {
            unsigned h = history[(hook_count + i) & 63];
            char buf[100];
            m68k_disassemble(buf, h, M68K_CPU_TYPE_68000);
            fprintf(stderr, "  %06x  %s\n", h, buf);
        }
        exit(7);
    }
    if (pc == watch_pc) {
        from_musashi();
        watch_hit(pc);
    }
    {   /* HM_HIST=ms: once, at that time, show the last 64 interpreted instructions */
        static long hist_at = -2;
        if (hist_at == -2) hist_at = getenv("HM_HIST") ? atol(getenv("HM_HIST")) : -1;
        if (hist_at >= 0 && (long)(clock() * 1000 / CLOCKS_PER_SEC) >= hist_at && !(hook_count & 255)) {
            int was = running;
            running = 1;
            interp_dump_history();
            running = was;
            hist_at = -1;
        }
    }
    {   /* HM_WATCH=addr,addr,...: log the first 5 visits to each interpreted address (branch tracing) */
        static uint32_t wl[16];
        static int nwl = -1, hits[16];
        int i;
        if (nwl < 0) {
            const char *s = getenv("HM_WATCH");
            nwl = 0;
            while (s && *s && nwl < 16) { wl[nwl++] = (uint32_t)strtoul(s, (char **)&s, 16); if (*s == ',') s++; }
        }
        for (i = 0; i < nwl; i++)
            if (pc == wl[i] && hits[i]++ < 5) {
            {
                uint32_t a2 = m68k_get_reg(NULL, M68K_REG_A2), sp = m68k_get_reg(NULL, M68K_REG_A7);
                fprintf(stderr, "  [watch] reached %06x (d0=%x d1=%x d2=%x d4=%x d5=%x a0=%x a2=%x a6=%x: [470]=%x [478]=%d "
                        "[480]=%d; sp+c=%x) at %lu ms\n", pc,
                        m68k_get_reg(NULL, M68K_REG_D0), m68k_get_reg(NULL, M68K_REG_D1), m68k_get_reg(NULL, M68K_REG_D2),
                        m68k_get_reg(NULL, M68K_REG_D4), m68k_get_reg(NULL, M68K_REG_D5), m68k_get_reg(NULL, M68K_REG_A0),
                        a2, m68k_get_reg(NULL, M68K_REG_A6), rd32(a2 + 0x470), (int)rd32(a2 + 0x478),
                        (int)rd32(a2 + 0x480), rd16(sp + 0xc), (unsigned long)(clock() * 1000 / CLOCKS_PER_SEC));
            }
            }
    }
    if (pc == SENTINEL) {
        done = 1;
        m68k_end_timeslice();
        return;
    }
    if (isig.pending && !isig.active && !nest_depth) {   /* a signal arrived for interpreted code */
        start_signal(pc);
        return;
    }
    if (trace && !(hook_count & 0x3FFFFF))  /* every ~4M instructions: where are we? */
        fprintf(stderr, "  [interp] at 0x%x (a7=%x)\n", pc, m68k_get_reg(NULL, M68K_REG_A7));
    if (!(++hook_count & 4095)) {          /* keep disc playback and signals going */
        from_musashi();
        rt_poll();
        to_musashi();
    }
}

/* A nested call: interpreted code is running (a streamed module), a signal arrives, the game's
   recompiled handler calls a routine in another streamed module. Musashi isn't re-entrant, but its whole
   state is its context plus two cycle counters, so save those, run the inner call, and put them back:
   the outer code then carries on exactly where it was. */
extern int m68ki_remaining_cycles, m68ki_initial_cycles;

static void interp_call_nested(uint32_t addr)
{
    static unsigned char ctx[2][4096];
    int rem = m68ki_remaining_cycles, init = m68ki_initial_cycles, was_done = done;
    unsigned char *save;
    if (nest_depth >= 2 || m68k_context_size() > sizeof ctx[0]) {
        fprintf(stderr, "stopped: interpreted calls nested too deep (0x%x)\n", addr);
        exit(6);
    }
    save = ctx[nest_depth++];
    m68k_get_context(save);
    if (trace) fprintf(stderr, "  [interp] nested call 0x%x\n", addr);
    wr16(SENTINEL, 0x4E71);
    PUSH32(SENTINEL);
    to_musashi();
    m68k_set_reg(M68K_REG_PC, addr);
    done = 0;
    while (!done)
        m68k_execute(200000);
    from_musashi();                        /* results of the inner call (d0 etc.) */
    m68k_set_context(save);                /* the outer code's registers and PC */
    nest_depth--;
    m68ki_remaining_cycles = rem;
    m68ki_initial_cycles = init;
    done = was_done;
    if (trace) fprintf(stderr, "  [interp] back from nested 0x%x\n", addr);
}

/* Called like a jsr: the caller has NOT pushed a return address yet. */
void interp_call(uint32_t addr)
{
    if (!rd16(addr) && !rd16(addr + 2)) {
        /* Blank memory is never real code: a streamed module the game calls into hasn't arrived from the disc
           yet (a timing race seen right after the Philips logo program exits). Running it slid through the
           zeros into the next module's header and crashed; skip the call and say who made it. */
        fprintf(stderr, "  [interp] call into blank memory at 0x%x skipped (d0=%x a7=%x, return %x)\n", addr,
                cpu.d[0], cpu.a[7], rd32(cpu.a[7]));
        return;
    }
    if (running) {
        interp_call_nested(addr);
        return;
    }
    if (!ready) {
        m68k_init();
        m68k_set_cpu_type(M68K_CPU_TYPE_68000);
        m68k_pulse_reset();
        ready = 1;
    }
    wr16(SENTINEL, 0x4E71);               /* nop, never actually executed */
    PUSH32(SENTINEL);
    to_musashi();
    m68k_set_reg(M68K_REG_PC, addr);
    if (trace) fprintf(stderr, "  [interp] call 0x%x\n", addr);
    running = 1;
    done = 0;
    while (!done)
        m68k_execute(200000);
    running = 0;
    from_musashi();                        /* the module's rts already popped SENTINEL */
    if (trace) fprintf(stderr, "  [interp] back from 0x%x\n", addr);
}
