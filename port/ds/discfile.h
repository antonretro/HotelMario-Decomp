/* discfile.h - the game's files, read straight from your disc image (see discfile.c) */
#ifndef DISCFILE_H
#define DISCFILE_H

#include <stdio.h>
#include <stddef.h>

int disc_mount(const char *path);      /* open the .bin and read its directories; returns how many files, 0 = no */
FILE *disc_fopen(const char *path, const char *mode);
FILE *disc_fmemopen(const void *data, size_t size);
size_t disc_fread(void *out, size_t size, size_t count, FILE *f);
int disc_fgetc(FILE *f);
int disc_fseek(FILE *f, long off, int whence);
long disc_ftell(FILE *f);
int disc_fclose(FILE *f);
int disc_fputc(int c, FILE *f);
int disc_fflush(FILE *f);

#endif
