/*
 * ds_prefix.h - put in front of every runtime file of the DS build (tools/build_ds.py, gcc -include).
 *
 * The CD-i memory. The game asks for 950 KB in all, so the 16 MB flat image of the other ports shrinks to
 * 1.25 MB, with the data and the heap moved down to fit:
 *   0x000000 program, 0x020000 data, 0x028000 heap, 0x13F000 system globals, 0x13F800 interpreter's return address
 * The mask stays a power of two minus one: addresses above the image are never used.
 */
#ifndef DS_PREFIX_H
#define DS_PREFIX_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "discfile.h"

#define RAM_SIZE   0x140000u
#define RAM_MASK   0x1FFFFFu
#define DATA_BASE  0x20000u
#define HEAP_START 0x28000u
#define HEAP_END   0x13E000u
#define SYSGLOB    0x13F000u
#define SENTINEL   0x13F800u
#define GAME_RECORD (HEAP_START + 0x50)   /* the game's first allocation: gdsP in the decompilation */
#define NTAB       8                /* the game makes 5 display tables, against 32 in the shared code */

/* Same clock as the 3DS and Wii: milliseconds from a hardware timer (window_ds.c) */
#undef CLOCKS_PER_SEC
#define CLOCKS_PER_SEC 1000
clock_t port_clock(void);
#define clock() port_clock()

/* The game's files come out of the disc image */
#define fopen(path, mode)           disc_fopen(path, mode)
#define fread(out, size, count, f)  disc_fread(out, size, count, f)
#define fgetc(f)                    disc_fgetc(f)
#define fseek(f, off, whence)       disc_fseek(f, off, whence)
#define ftell(f)                    disc_ftell(f)
#define fclose(f)                   disc_fclose(f)
#define fputc(c, f)                 disc_fputc(c, f)
#define fflush(f)                   disc_fflush(f)

#define main hotel_main             /* main() in window_ds.c moves to a bigger stack first */

void ds_exit(int code);             /* keeps the last message on screen */
#define exit(code) ds_exit(code)

#endif
