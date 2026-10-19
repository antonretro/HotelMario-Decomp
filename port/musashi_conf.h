/*
 * musashi_conf.h - our settings for the Musashi 68000 core (MIT licence, (c) Karl Stenerud),
 * used to run the code modules the game streams from the disc. Passed to Musashi with
 * -DMUSASHI_CNF="musashi_conf.h"; everything not set here keeps Musashi's default (m68kconf.h).
 */
#ifndef HOTEL_MUSASHI_CONF_H
#define HOTEL_MUSASHI_CONF_H

/* a plain 68000, like the CD-i's 68070 */
#define M68K_EMULATE_010    M68K_OPT_OFF
#define M68K_EMULATE_EC020  M68K_OPT_OFF
#define M68K_EMULATE_020    M68K_OPT_OFF
#define M68K_EMULATE_030    M68K_OPT_OFF
#define M68K_EMULATE_040    M68K_OPT_OFF
#define M68K_EMULATE_PMMU   M68K_OPT_OFF

/* OS-9 system calls (trap #0 + call word) go to the port's os9_trap() */
#define M68K_TRAP_HAS_CALLBACK   M68K_OPT_SPECIFY_HANDLER
#define M68K_TRAP_CALLBACK(trap) interp_trap(trap)
int interp_trap(int trap);

/* return detection and real-time polling */
#define M68K_INSTRUCTION_HOOK         M68K_OPT_SPECIFY_HANDLER
#define M68K_INSTRUCTION_CALLBACK(pc) interp_hook(pc)
void interp_hook(unsigned int pc);

#include "m68kconf.h"   /* Musashi's own file, found through the include path */

#endif
