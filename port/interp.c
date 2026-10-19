/*
 * interp.c - run 68000 code that has no recompiled version (the code modules the game streams
 * from the disc) on the Musashi interpreter, sharing RAM and registers with the recompiled code.
 *
 * Musashi 68000 core: Copyright 1998-2002 Karl Stenerud, MIT licence (tools/ext/Musashi).
 */
#include <stdio.h>
#include <stdlib.h>

#include "m68k.h"                          /* the port's CPU state (port/m68k.h) */
#include "runtime.h"
#include "../tools/ext/Musashi/m68k.h"     /* Musashi's API */

#define SENTINEL 0xFFF000u                 /* interpreted calls return here */

static int ready, running, done;
static unsigned hook_count;

/* ---- Musashi memory interface: the same big-endian RAM the recompiled code uses ---- */
unsigned int m68k_read_memory_8(unsigned int a)  { return rd8(a); }
unsigned int m68k_read_memory_16(unsigned int a) { return rd16(a); }
unsigned int m68k_read_memory_32(unsigned int a) { return rd32(a); }
void m68k_write_memory_8(unsigned int a, unsigned int v)  { wr8(a, v); }
void m68k_write_memory_16(unsigned int a, unsigned int v) { wr16(a, v); }
void m68k_write_memory_32(unsigned int a, unsigned int v) { wr32(a, v); }
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

/* trap #n: PC already points at the OS-9 call number word that follows the trap */
int interp_trap(int trap)
{
    unsigned pc = m68k_get_reg(NULL, M68K_REG_PC);
    from_musashi();
    os9_trap(trap, rd16(pc));
    to_musashi();
    m68k_set_reg(M68K_REG_PC, pc + 2);
    return 1;   /* handled: no exception */
}

static unsigned history[64];

void interp_hook(unsigned int pc)
{
    history[hook_count & 63] = pc;
    if ((pc & 1) || pc >= RAM_SIZE) {      /* ran off into nowhere: show how we got here */
        int i;
        fprintf(stderr, "stopped: interpreter at bad address 0x%x (a7=%x). Last instructions:\n", pc,
                m68k_get_reg(NULL, M68K_REG_A7));
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
    if (pc == SENTINEL) {
        done = 1;
        m68k_end_timeslice();
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

/* Called like a jsr: the caller has NOT pushed a return address yet. */
void interp_call(uint32_t addr)
{
    if (running) {
        fprintf(stderr, "stopped: nested interpreted call to 0x%x\n", addr);
        exit(6);
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
