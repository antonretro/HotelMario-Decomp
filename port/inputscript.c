/*
 * inputscript.c - --input "ms:action,...": scripted input for headless tests, shared by the PC and 3DS
 * windows. At each time (ms since start) do one action: up/down/left/right nudge the pointer 60 pixels once
 * (one menu step: the game re-centres it), b1/b2 hold a button for 200 ms, and right~1500 (any direction)
 * holds that direction for 1500 ms, moving the pointer like a held key.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "m68k.h"
#include "runtime.h"

const char *input_script;

int script_buttons(int *ptr_x, int *ptr_y)
{
    static int next_event;     /* index of the next action to apply */
    static unsigned long last;
    unsigned long now = (unsigned long)(clock() * 1000 / CLOCKS_PER_SEC);
    long step = last ? (long)(now - last) * 2 / 5 : 0;   /* 400 pointer pixels a second */
    const char *p = input_script;
    int i = 0, held = 0;
    last = now;
    while (p && *p) {
        unsigned long t = strtoul(p, (char **)&p, 10), dur = 0;
        char act[8] = { 0 };
        int n = 0;
        if (*p == ':') p++;
        while (*p && *p != ',' && *p != '~' && n < 7) act[n++] = *p++;
        if (*p == '~') dur = strtoul(p + 1, (char **)&p, 10);
        if (*p == ',') p++;
        if (dur) {                                /* held direction */
            if (now >= t && now < t + dur) {
                if (!strcmp(act, "up")) *ptr_y -= step;
                else if (!strcmp(act, "down")) *ptr_y += step;
                else if (!strcmp(act, "left")) *ptr_x -= step;
                else if (!strcmp(act, "right")) *ptr_x += step;
            }
            if (now >= t && i >= next_event) next_event = i + 1;
            i++;
            continue;
        }
        if (now >= t && i >= next_event) {        /* one-shot pointer nudges, once each */
            if (!strcmp(act, "up")) *ptr_y -= 60;
            else if (!strcmp(act, "down")) *ptr_y += 60;
            else if (!strcmp(act, "left")) *ptr_x -= 60;
            else if (!strcmp(act, "right")) *ptr_x += 60;
            next_event = i + 1;
            if (trace) fprintf(stderr, "  [input] %s at %lu ms\n", act, now);
        }
        if (now >= t && now < t + 200) {         /* buttons are held for 200 ms */
            if (!strcmp(act, "b1")) held |= 1;
            if (!strcmp(act, "b2")) held |= 2;
        }
        i++;
    }
    window_set_pointer(*ptr_x, *ptr_y);
    return held;
}
