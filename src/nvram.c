/* nvram.c - save game records in the player's NVRAM (0xf2fc-0xf35c) */
#include "hotel.h"

#define SAVE_SIZE 62    /* one save record */

/* 0xf152: free slots left in the OS-9 module directory (16 bytes each, after the last used entry) */
number_of_open_mdir_entries()
{
    long *start, *end, *p;

    if ((start = (long *)_getsys(0x3c, 4)) == (long *)-1 || (end = (long *)_getsys(0x40, 4)) == (long *)-1)
        return -1;
    for (p = start; p < end;) {
        p += 4;
        if (*p == 0)
            break;
    }
    return ((char *)end - (char *)p) / 16;
}

/* 0xf2fc */
nvram_write(fp, buf)
FILE *fp;
char *buf;
{
    if (fwrite(buf, SAVE_SIZE, 1, fp) == 0)
        hm_error(0);
}

/* 0xf32c */
nvram_read(fp, buf)
FILE *fp;
char *buf;
{
    if (fread(buf, SAVE_SIZE, 1, fp) == 0)
        hm_error(0);
}
