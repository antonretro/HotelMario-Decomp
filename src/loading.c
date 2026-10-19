/* loading.c - stage loading screen (0x7d48) */
#include "hotel.h"

/* In the game font, 'g' draws as '!' and 'm' as '?'. */
#define TEXT(x, s) alphablit(gdsP->font, gdsP->screen + (x), s, 0)

/*
 * 0x7d48: mode 1 clears the screen and names the hotel's owner for stage 1-8.
 * mode 2 shows a message for today's date, or "HERE WE GO!" on other days.
 */
loading_screen(mode, stage)
char mode;
short stage;
{
    int show_go;
    long time, date;
    short day;
    int tick;
    long month;

    if (mode == 1) {
        memset(gdsP->screen, 0, 0x1a400);
        set_up_clut(gdsP->path, gdsP->fct, gdsP->clut);
        dc_wrli(gdsP->path, gdsP->lct, 2, 7, ((long)gdsP->screen & 0xffffff) | 0x40000000);
        dc_wrfi(gdsP->path, gdsP->fct, 4, 0xdb000000 | 0x3f);
        switch (stage) {
        case 1: TEXT(0xa53e, "MORTON AND HIS NUTTY GROUP"); break;
        case 2: TEXT(0xa55b, "ROY AND HIS HENCHMEN"); break;
        case 3: TEXT(0xa52a, "LARRY AND HIS UNDERGROUND PALS"); break;
        case 4: TEXT(0xa532, "LEMMY AND HIS FLYING FRIENDS"); break;
        case 5: TEXT(0xa524, "LUDWIG AND HIS SPOOKY SIDEKICKS"); break;
        /* no case 6: there is no hotel 6 on the disc either */
        case 7: TEXT(0xa54c, "WENDY AND HER BRAT PACK"); break;
        case 8: TEXT(0xa565, "BOWSER AND COMPANY"); break;
        }
        TEXT(0xb42f, "ARE PREPARING FOR YOUR ARRIVAL");
    } else if (mode == 2) {
        show_go = 2;    /* also the date format passed to _sysdate */
        _sysdate(show_go, &time, &date, &day, &tick);
        day = date & 0xff;
        month = (date >> 8) & 0xff;
        switch (month) {
        case 1:
            if (day < 6) { TEXT(0xe175, "HAPPY NEW YEARg"); show_go = 0; }
            break;
        case 2:
            if (day == 17) { TEXT(0xe15e, "ITS HOLLIES BIRTHDAYg"); show_go = 0; }
            else if (day > 11 && day < 15) { TEXT(0xe15a, "HAPPY VALENTINES DAYg"); show_go = 0; }
            break;
        case 5:
            if (day > 6 && day < 10) { TEXT(0xe13e, "DO NOT FORGET MOTHERS DAYg"); show_go = 0; }
            else if (day > 9 && day < 31) { TEXT(0xe161, "SCHOOLS ALMOST OUTg"); show_go = 0; }
            break;
        case 6:
            if (day > 16 && day < 21) { TEXT(0xe166, "HAPPY FATHERS DAYg"); show_go = 0; }
            break;
        case 7:
            if (day < 5) { TEXT(0xe157, "HAPPY FOURTH OF JULYg"); show_go = 0; }
            break;
        case 8:
            if (day > 14) { TEXT(0xe139, "READY TO GO BACK TO SCHOOLm"); show_go = 0; }
            break;
        case 10:
            if (day > 28) { TEXT(0xe17f, "BOOOOOOOOOggg"); show_go = 0; }
            break;
        case 11:
            if (day > 16 && day < 26) { TEXT(0xe125, "HAPPY TURKEY DAYg GOBBLE GOBBLEg"); show_go = 0; }
            break;
        case 12:
            if (day > 10 && day < 27) { TEXT(0xe17a, "HAPPY HOLIDAYSg"); show_go = 0; }
            else if (day > 26) { TEXT(0xe175, "HAPPY NEW YEARg"); show_go = 0; }
            break;
        }
        if (show_go)
            TEXT(0xe18b, "HERE WE GOg");
    }
}
