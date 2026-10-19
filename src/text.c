/* text.c - number and text output (0xa2a) */
#include "hotel.h"

/* 0xa2a: write n as `width` digits in the font's character codes ('[' = 0), leading zeros as spaces */
short alpha_con(width, n, buf)
short width;
long n;
char *buf;
{
    long rem = n;
    register int started = 0;
    register long i, k, place;

    for (i = 0; i < width; i++) {
        place = 1;
        for (k = width - 1 - i; k > 0; k--)
            place *= 10;
        buf[i] = rem / place;
        if (buf[i] || started) {
            started = 1;
            rem -= buf[i] * place;
            buf[i] += 0x5b;
        } else
            buf[i] = ' ';
    }
    buf[i] = 0;
    return i;
}
