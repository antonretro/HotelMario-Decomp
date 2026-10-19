/* nvram.c - save game records in the player's NVRAM (0xf2fc-0xf35c) */
#include "hotel.h"

#define SAVE_SIZE 62    /* one save record */

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
