/* init.c - device set-up at start-up (0x6a40) */
#include "hotel.h"

/* 0x6a40: enable device 1 for signals and ask device 0xb to signal 0x400 on the pause key.
   The -1 checks on csd_devname/open/ss_enable/kb_ssig are compiled in but never acted on. */
init_pause()
{
    char *dev, *pause_dev;
    register int path;

    dev = csd_devname(1, 1);
    strcat(dev, "@");                   /* the raw device */
    path = open(dev, 1);
    pause_dev = csd_devname(0xb, 1);
    gdsP->pause_path = open(pause_dev, 1);
    ss_enable(path);
    free(pause_dev);
    free(dev);
    gdsP->sound_state = 0;
    sigmask(1);
    kb_ssig(gdsP->pause_path, 0x400);
    sigmask(0);
}
