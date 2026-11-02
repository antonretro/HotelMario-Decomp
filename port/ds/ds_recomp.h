/*
 * ds_recomp.h - put in front of the recompiled game on the DS (tools/build_ds.py, gcc -include): the --watch
 * test at the top of every function costs code size the DS can use better, so it compiles away here, leaving only
 * the one in mario(), the routine that runs Mario through every update of a stage and at no other time. That's how
 * the display knows a stage is on (display_ds.c).
 */
#ifndef DS_RECOMP_H
#define DS_RECOMP_H

#include "ds_prefix.h"
#include "m68k.h"

extern unsigned ds_mario_calls;
#define MARIO 0x96beu               /* mario() in the European disc's cdi_hotel */
#undef watch_pc
#define watch_pc MARIO
#define watch_hit(addr) ((void)(ds_mario_calls++))

#endif
