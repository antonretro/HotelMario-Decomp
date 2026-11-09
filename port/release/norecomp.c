/*
 * norecomp.c - stands in for build/recomp/*.c in a release build (tools/build_port.py --release).
 *
 * A release build carries none of the game's code: no translated functions at all. runtime.c then finds no
 * recompiled _cstart and runs the whole program on the 68000 interpreter, straight from cdi_hotel as read
 * from the player's own disc.
 */
#include <stdint.h>
#include <stdlib.h>

#include "m68k.h"
#include "runtime.h"

const struct { uint32_t addr; RecompFn fn; const char *name; } recomp_table[1] = { { 0, 0, "" } };
const int recomp_count = 0;

void f_000052(void)       /* never called: recomp_lookup(0x52) finds nothing, so main() interprets */
{
    unimpl(0x52, "release build has no recompiled code");
}
