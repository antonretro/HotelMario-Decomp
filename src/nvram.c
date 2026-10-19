/* nvram.c - save game records in the player's NVRAM (0xf152-0xf35c) */
#include <errno.h>
#include "hotel.h"

#define SAVE_SIZE 62    /* one save record */
#define SAVE_NEED 0x7c  /* NVRAM bytes needed to create the save file */

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

/* 0xf1a8: load the save record; if there is no save file yet (E$PNNF), create it from buf */
nvram_open_read(buf)
char *buf;
{
    char name[0x20];
    char *dev;
    FILE *fp;

    if ((dev = csd_devname(4, 1)) == 0)
        hm_error(0);
    strcpy(name, dev);
    strcat(name, "/hotel_mario");
    if ((fp = fopen(name, "r")) == 0) {
        if (errno == 0xd8 && nvr_available() >= SAVE_NEED && (fp = fopen(name, "w")) != 0)
            nvram_write(fp, buf);
        else
            hm_error(0);
    } else
        nvram_read(fp, buf);
    return fclose(fp) == -1 ? -1 : 0;
}

/* 0xf258: store the save record; on error 0xdb it retries once if there is room */
nvram_open_write(buf)
char *buf;
{
    char name[0x20];
    char *dev;
    FILE *fp;

    if ((dev = csd_devname(4, 1)) == 0)
        hm_error(0);
    strcpy(name, dev);
    strcat(name, "/hotel_mario");
    if ((fp = fopen(name, "w")) != 0
     || (errno == 0xdb && nvr_available() >= SAVE_NEED && (fp = fopen(name, "w")) != 0))
        nvram_write(fp, buf);
    else
        hm_error(0);
    return fclose(fp) == -1 ? -1 : 0;
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
