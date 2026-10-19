/*
 * m68k.h - CPU state and helpers used by the recompiled (68000 -> C) game code.
 *
 * The CD-i program is loaded at address 0 of a flat, big-endian 16 MB RAM image, the OS-9 data area
 * at DATA_BASE, with A6 = DATA_BASE + 0x8000 like the real C runtime sets it up.
 */
#ifndef M68K_H
#define M68K_H

#include <stdint.h>

#define RAM_SIZE   0x1000000u
#define RAM_MASK   (RAM_SIZE - 1)
#define DATA_BASE  0x100000u

typedef struct {
    uint32_t d[8], a[8];
    uint8_t  x, n, z, v, c;
} Cpu;

extern Cpu cpu;
extern uint8_t RAM[RAM_SIZE];

#define D(n) cpu.d[n]
#define A(n) cpu.a[n]

#define MSK(sz) ((sz) == 4 ? 0xFFFFFFFFu : (sz) == 2 ? 0xFFFFu : 0xFFu)
#define MSB(sz) ((sz) == 4 ? 0x80000000u : (sz) == 2 ? 0x8000u : 0x80u)

/* ---- memory (big-endian) ---- */
static inline uint32_t rd8(uint32_t a)  { return RAM[a & RAM_MASK]; }
static inline uint32_t rd16(uint32_t a) { return (rd8(a) << 8) | rd8(a + 1); }
static inline uint32_t rd32(uint32_t a) { return (rd16(a) << 16) | rd16(a + 2); }
static inline void wr8(uint32_t a, uint32_t v)  { RAM[a & RAM_MASK] = (uint8_t)v; }
static inline void wr16(uint32_t a, uint32_t v) { wr8(a, v >> 8); wr8(a + 1, v); }
static inline void wr32(uint32_t a, uint32_t v) { wr16(a, v >> 16); wr16(a + 2, v); }
static inline uint32_t rdsz(uint32_t a, int sz) { return sz == 1 ? rd8(a) : sz == 2 ? rd16(a) : rd32(a); }
static inline void wrsz(uint32_t a, uint32_t v, int sz) { if (sz == 1) wr8(a, v); else if (sz == 2) wr16(a, v); else wr32(a, v); }

/* ---- registers ---- */
static inline uint32_t sx(uint32_t v, int sz)
{
    return sz == 1 ? (uint32_t)(int32_t)(int8_t)v : sz == 2 ? (uint32_t)(int32_t)(int16_t)v : v;
}
static inline void setsz(uint32_t *r, uint32_t v, int sz) { *r = (*r & ~MSK(sz)) | (v & MSK(sz)); }

/* ---- flags ---- */
static inline void f_nz(uint32_t r, int sz)
{
    r &= MSK(sz);
    cpu.n = (r & MSB(sz)) != 0;
    cpu.z = r == 0;
    cpu.v = cpu.c = 0;
}

/* d + s (+x); setx: update X as well (ADD/ADDI/ADDQ yes, nothing else uses this) */
static inline uint32_t f_add(uint32_t s, uint32_t d, int sz, int x, int setx)
{
    uint64_t m = MSK(sz), r = (uint64_t)(s & m) + (d & m) + (uint64_t)x;
    uint32_t res = (uint32_t)(r & m);
    cpu.c = r > m;
    if (setx) cpu.x = cpu.c;
    cpu.v = ((s ^ res) & (d ^ res) & MSB(sz)) != 0;
    cpu.n = (res & MSB(sz)) != 0;
    cpu.z = res == 0;
    return res;
}

/* d - s (-x); CMP passes setx = 0 */
static inline uint32_t f_sub(uint32_t s, uint32_t d, int sz, int x, int setx)
{
    uint64_t m = MSK(sz);
    uint32_t res = (uint32_t)(((d & m) - (s & m) - (uint64_t)x) & m);
    cpu.c = (uint64_t)(s & m) + (uint64_t)x > (d & m);
    if (setx) cpu.x = cpu.c;
    cpu.v = ((s ^ d) & (res ^ d) & MSB(sz)) != 0;
    cpu.n = (res & MSB(sz)) != 0;
    cpu.z = res == 0;
    return res;
}

/* ADDX/SUBX/NEGX: Z is only ever cleared */
static inline uint32_t f_addx(uint32_t s, uint32_t d, int sz)
{
    uint8_t z = cpu.z;
    uint32_t r = f_add(s, d, sz, cpu.x, 1);
    cpu.z = z && (r == 0);
    return r;
}
static inline uint32_t f_subx(uint32_t s, uint32_t d, int sz)
{
    uint8_t z = cpu.z;
    uint32_t r = f_sub(s, d, sz, cpu.x, 1);
    cpu.z = z && (r == 0);
    return r;
}

/* shifts and rotates; count already taken mod 64 by the caller */
static inline uint32_t f_lsl(uint32_t v, int cnt, int sz)
{
    uint64_t r = v & MSK(sz);
    uint8_t c = 0;
    if (cnt) {
        /* last bit shifted out = top bit of the operand after shifting by cnt-1 */
        c = cnt <= sz * 8 ? (uint8_t)(((r << (cnt - 1)) >> (sz * 8 - 1)) & 1) : 0;
        r = cnt < 64 ? (r << cnt) : 0;
        cpu.x = c;
    }
    r &= MSK(sz);
    f_nz((uint32_t)r, sz);
    cpu.c = c;
    return (uint32_t)r;
}
static inline uint32_t f_lsr(uint32_t v, int cnt, int sz)
{
    uint64_t r = v & MSK(sz);
    uint8_t c = 0;
    if (cnt) { c = cnt <= sz * 8 ? (uint8_t)((r >> (cnt - 1)) & 1) : 0; r >>= cnt; cpu.x = c; }
    f_nz((uint32_t)r, sz);
    cpu.c = c;
    return (uint32_t)r;
}
static inline uint32_t f_asr(uint32_t v, int cnt, int sz)
{
    int64_t s = (int64_t)(int32_t)sx(v, sz);
    uint8_t c = 0;
    if (cnt) { c = (uint8_t)((s >> (cnt > 63 ? 63 : cnt - 1)) & 1); s >>= (cnt > 63 ? 63 : cnt); cpu.x = c; }
    f_nz((uint32_t)s, sz);
    cpu.c = c;
    return (uint32_t)s & MSK(sz);
}
static inline uint32_t f_asl(uint32_t v, int cnt, int sz)
{
    uint32_t m = MSK(sz), r = v & m;
    uint8_t c = 0, ov = 0;
    int i;
    for (i = 0; i < cnt; i++) {
        uint32_t old = r;
        c = (r & MSB(sz)) != 0;
        r = (r << 1) & m;
        if ((old ^ r) & MSB(sz)) ov = 1;
    }
    f_nz(r, sz);
    if (cnt) { cpu.c = cpu.x = c; }
    cpu.v = ov;
    return r;
}
static inline uint32_t f_rol(uint32_t v, int cnt, int sz)
{
    int bits = sz * 8, k = cnt % bits;
    uint32_t m = MSK(sz), r = v & m;
    if (k) r = ((r << k) | (r >> (bits - k))) & m;
    f_nz(r, sz);
    cpu.c = cnt ? (r & 1) : 0;
    return r;
}
static inline uint32_t f_ror(uint32_t v, int cnt, int sz)
{
    int bits = sz * 8, k = cnt % bits;
    uint32_t m = MSK(sz), r = v & m;
    if (k) r = ((r >> k) | (r << (bits - k))) & m;
    f_nz(r, sz);
    cpu.c = cnt ? ((r & MSB(sz)) != 0) : 0;
    return r;
}
static inline uint32_t f_roxl(uint32_t v, int cnt, int sz)
{
    uint32_t m = MSK(sz), r = v & m;
    int i;
    for (i = 0; i < cnt; i++) {
        uint8_t out = (r & MSB(sz)) != 0;
        r = ((r << 1) | cpu.x) & m;
        cpu.x = out;
    }
    f_nz(r, sz);
    cpu.c = cpu.x;
    return r;
}
static inline uint32_t f_roxr(uint32_t v, int cnt, int sz)
{
    uint32_t m = MSK(sz), r = v & m;
    int i;
    for (i = 0; i < cnt; i++) {
        uint8_t out = r & 1;
        r = (r >> 1) | (cpu.x ? MSB(sz) : 0);
        cpu.x = out;
    }
    f_nz(r, sz);
    cpu.c = cpu.x;
    return r;
}

/* ---- multiply / divide (word forms) ---- */
static inline void f_mulu(uint32_t s, int dn)
{
    uint32_t r = (s & 0xFFFF) * (cpu.d[dn] & 0xFFFF);
    cpu.d[dn] = r;
    f_nz(r, 4);
}
static inline void f_muls(uint32_t s, int dn)
{
    uint32_t r = (uint32_t)((int32_t)(int16_t)s * (int32_t)(int16_t)cpu.d[dn]);
    cpu.d[dn] = r;
    f_nz(r, 4);
}
void m68k_divzero(void);
static inline void f_divu(uint32_t s, int dn)
{
    uint32_t q, r;
    s &= 0xFFFF;
    if (!s) { m68k_divzero(); return; }
    q = cpu.d[dn] / s;
    r = cpu.d[dn] % s;
    cpu.c = 0;
    if (q > 0xFFFF) { cpu.v = 1; return; }
    cpu.d[dn] = (r << 16) | q;
    cpu.n = (q & 0x8000) != 0;
    cpu.z = q == 0;
    cpu.v = 0;
}
static inline void f_divs(uint32_t s, int dn)
{
    int32_t dv = (int32_t)cpu.d[dn], sv = (int16_t)s, q, r;
    if (!sv) { m68k_divzero(); return; }
    q = dv / sv;
    r = dv % sv;
    cpu.c = 0;
    if (q > 32767 || q < -32768) { cpu.v = 1; return; }
    cpu.d[dn] = ((uint32_t)(r & 0xFFFF) << 16) | (q & 0xFFFF);
    cpu.n = q < 0;
    cpu.z = q == 0;
    cpu.v = 0;
}

/* ---- control flow ---- */
#define PUSH32(v) (cpu.a[7] -= 4, wr32(cpu.a[7], (v)))
#define RTS()     do { cpu.a[7] += 4; return; } while (0)

typedef void (*RecompFn)(void);
void     call_addr(uint32_t addr, uint32_t ret);   /* jsr through a computed address */
void     jump_far(uint32_t addr);                  /* jmp out of the function: tail call, or a return (jmp (a5)) */
void     os9_trap(int n, uint32_t code);           /* trap #n followed by a call-number word */
void     unimpl(uint32_t addr, const char *what);  /* reached code the recompiler couldn't translate */

#endif
