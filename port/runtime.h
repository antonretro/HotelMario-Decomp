/* runtime.h - shared by the port's runtime files */
#ifndef RUNTIME_H
#define RUNTIME_H

#include <stdint.h>

extern const char *fs_root;   /* folder with the files extracted from your disc */
extern int trace;

void os9_init(void);

#endif
