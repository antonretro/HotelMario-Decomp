/*
 * patcher.c - the Hotel Mario setup program for releases ("bring your own disc").
 *
 * The player picks their own disc image; the patcher checks it is a supported Hotel Mario disc (SHA-1), reads
 * the CD-i file system out of it, and builds a ready-to-use folder for each platform they tick:
 *
 *   Hotel Mario (PC)\       hotel.exe, SDL2.dll, game\ (the disc's files)
 *   Hotel Mario (3DS)\      3ds\HotelMario\HotelMario.3dsx + game\   (copy the 3ds folder to the SD card)
 *   Hotel Mario (Android)\  HotelMario.apk + HotelMario\game\        (copy HotelMario to the device's storage)
 *
 * The programs come from the "files" folder next to the patcher (files\pc, files\3ds, files\android); a
 * platform whose program isn't there can't be ticked. Nothing from the game is in the patcher or in those
 * programs: the game's files, and its code, come only from the disc the player supplies.
 *
 * Build: python -I tools/build_patcher.py  (MinGW gcc, no other libraries)
 */
#define WIN32_LEAN_AND_MEAN
#define _WIN32_IE 0x0600
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shlobj.h>
#include <shellapi.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#define RAW 2352

/* ---- the discs we know ---- */
static const struct { const char *sha1, *title; int supported; } KNOWN[] = {
    { "8d671f7bc729ed8d6a07d1d40ba48cbc9530bd30", "Hotel Mario (Europe)", 1 },
    { "183b2efd1f5b926757a8fb514cc861fdf7a244c5", "Hotel Mario (USA) (Beta) (1993-11-23)", 0 },
};

/* ---- SHA-1 ---- */
typedef struct { uint32_t h[5]; uint64_t len; uint8_t buf[64]; size_t n; } Sha1;

#define ROL(x, c) (((x) << (c)) | ((x) >> (32 - (c))))
static void sha1_block(Sha1 *s, const uint8_t *p)
{
    uint32_t w[80], a, b, c, d, e, t;
    int i;
    for (i = 0; i < 16; i++) w[i] = (uint32_t)p[i * 4] << 24 | p[i * 4 + 1] << 16 | p[i * 4 + 2] << 8 | p[i * 4 + 3];
    for (; i < 80; i++) w[i] = ROL(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    a = s->h[0]; b = s->h[1]; c = s->h[2]; d = s->h[3]; e = s->h[4];
    for (i = 0; i < 80; i++) {
        uint32_t f, k;
        if (i < 20)      { f = (b & c) | (~b & d);           k = 0x5A827999; }
        else if (i < 40) { f = b ^ c ^ d;                    k = 0x6ED9EBA1; }
        else if (i < 60) { f = (b & c) | (b & d) | (c & d);  k = 0x8F1BBCDC; }
        else             { f = b ^ c ^ d;                    k = 0xCA62C1D6; }
        t = ROL(a, 5) + f + e + k + w[i];
        e = d; d = c; c = ROL(b, 30); b = a; a = t;
    }
    s->h[0] += a; s->h[1] += b; s->h[2] += c; s->h[3] += d; s->h[4] += e;
}
static void sha1_init(Sha1 *s)
{
    static const uint32_t h0[5] = { 0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0 };
    memcpy(s->h, h0, sizeof h0);
    s->len = 0;
    s->n = 0;
}
static void sha1_add(Sha1 *s, const uint8_t *p, size_t n)
{
    s->len += n;
    while (n) {
        size_t k = 64 - s->n < n ? 64 - s->n : n;
        memcpy(s->buf + s->n, p, k);
        s->n += k; p += k; n -= k;
        if (s->n == 64) { sha1_block(s, s->buf); s->n = 0; }
    }
}
static void sha1_hex(Sha1 *s, char out[41])
{
    uint64_t bits = s->len * 8;
    uint8_t pad = 0x80, z = 0, l[8];
    int i;
    sha1_add(s, &pad, 1);
    while (s->n != 56) sha1_add(s, &z, 1);
    for (i = 0; i < 8; i++) l[i] = (uint8_t)(bits >> (56 - 8 * i));
    sha1_add(s, l, 8);
    for (i = 0; i < 5; i++) sprintf(out + i * 8, "%08x", s->h[i]);
}

/* ---- the CD-i disc (Green Book): label, path table, directories (as tools/cdi_fs.py) ---- */
static FILE *disc;

static int raw(uint32_t lba, uint8_t *out)
{
    if (_fseeki64(disc, (long long)lba * RAW, SEEK_SET)) return 0;
    return fread(out, 1, RAW, disc) == RAW;
}
static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }
static uint16_t be16(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }

typedef struct { char path[256]; uint32_t start, size; uint16_t attr; } Entry;
static Entry *entries;
static int nentries, maxentries;
typedef struct { char name[64]; uint32_t lbn; uint16_t parent; char path[256]; } Dir;

static int read_dir(uint32_t lbn, const char *path)
{
    uint8_t sec[RAW];
    uint32_t lba = lbn, total = 0;
    int first = 1;
    for (;;) {
        int i = 0;
        if (!raw(lba, sec)) return 0;
        while (i < 2048) {
            const uint8_t *r = sec + 24 + i;
            int rl = r[0], nlen, su;
            if (!rl) break;
            nlen = r[32];
            if (first && nlen == 1 && r[33] == 0) total = be32(r + 14);
            first = 0;
            if (!(nlen == 1 && (r[33] == 0 || r[33] == 1))) {
                uint16_t attr;
                su = 33 + nlen + ((nlen + 1) & 1);
                attr = rl >= su + 6 ? be16(r + su + 4) : 0;
                if (!(attr & 0x8000)) {                         /* a file, not a directory */
                    Entry *e;
                    if (nentries == maxentries) {
                        maxentries = maxentries ? maxentries * 2 : 64;
                        entries = realloc(entries, maxentries * sizeof *entries);
                    }
                    e = &entries[nentries++];
                    snprintf(e->path, sizeof e->path, "%s/%.*s", path, nlen, (const char *)r + 33);
                    e->start = be32(r + 6);
                    e->size = be32(r + 14);
                    e->attr = attr;
                }
            }
            i += rl;
        }
        lba++;
        if (!total || (lba - lbn) * 2048 >= total) break;
    }
    return 1;
}

static int read_disc(char *err, size_t errlen)
{
    uint8_t sec[RAW], *pt;
    uint32_t lba, pt_size = 0, pt_addr = 0, got;
    Dir *dirs;
    int ndirs = 0, i;
    for (lba = 16; lba < 32; lba++) {
        if (!raw(lba, sec)) break;
        if (!memcmp(sec + 24 + 1, "CD-I ", 5)) { pt_size = be32(sec + 24 + 136); pt_addr = be32(sec + 24 + 148); break; }
    }
    if (!pt_size || pt_size > 1 << 20) { snprintf(err, errlen, "No CD-i disc label found in this image."); return 0; }
    pt = malloc(pt_size + 2048);
    for (got = 0, lba = pt_addr; got < pt_size; got += 2048, lba++) {
        if (!raw(lba, sec)) { free(pt); snprintf(err, errlen, "The disc image is cut short."); return 0; }
        memcpy(pt + got, sec + 24, 2048);
    }
    dirs = calloc(pt_size / 8 + 1, sizeof *dirs);
    for (i = 0; i < (int)pt_size;) {
        int nlen = pt[i];
        Dir *d;
        if (!nlen) break;
        d = &dirs[ndirs++];
        d->lbn = be32(pt + i + 2);
        d->parent = be16(pt + i + 6);
        snprintf(d->name, sizeof d->name, "%.*s", nlen, (const char *)pt + i + 8);
        if (ndirs == 1) d->path[0] = 0;
        else if (d->parent >= 1 && d->parent <= ndirs)
            snprintf(d->path, sizeof d->path, "%s/%s", dirs[d->parent - 1].path, d->name);
        i += 8 + nlen + (nlen & 1);
    }
    nentries = 0;
    for (i = 0; i < ndirs; i++)
        if (!read_dir(dirs[i].lbn, dirs[i].path)) { snprintf(err, errlen, "Couldn't read a folder on the disc."); break; }
    free(dirs);
    free(pt);
    return nentries > 0;
}

/* ---- files and folders ---- */
static char here[MAX_PATH];                 /* the patcher's own folder */

static void make_dirs(const char *path)     /* every folder above a file path, and the folder itself if it ends in \ */
{
    char p[MAX_PATH * 2], *s;
    snprintf(p, sizeof p, "%s", path);
    for (s = p; *s; s++)
        if ((*s == '\\' || *s == '/') && s > p + 2) {
            char c = *s;
            *s = 0;
            CreateDirectoryA(p, 0);
            *s = c;
        }
}

static int file_exists(const char *p) { DWORD a = GetFileAttributesA(p); return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY); }

/* write every file from the disc into dir (real-time files as raw sectors, as tools/cdi_fs.py) */
static int extract_to(const char *dir, void (*progress)(int done, int total))
{
    static uint8_t sec[RAW];
    int i, total = 0, done = 0;
    for (i = 0; i < nentries; i++) total += (entries[i].size + 2047) / 2048;
    for (i = 0; i < nentries; i++) {
        Entry *e = &entries[i];
        char dst[MAX_PATH * 2], *s;
        uint32_t n = (e->size + 2047) / 2048, l, left = e->size;
        int rawmode = (e->attr & 0x2000) != 0;
        FILE *o;
        for (l = 0; l < n && !rawmode; l++) {               /* any form-2 sector: keep raw sectors */
            if (!raw(e->start + l, sec)) return 0;
            if (sec[18] & 0x20) rawmode = 1;
        }
        snprintf(dst, sizeof dst, "%s%s", dir, e->path);
        for (s = dst; *s; s++) if (*s == '/') *s = '\\';
        make_dirs(dst);
        if (!(o = fopen(dst, "wb"))) return 0;
        for (l = 0; l < n; l++) {
            if (!raw(e->start + l, sec)) { fclose(o); return 0; }
            if (rawmode) fwrite(sec, 1, RAW, o);
            else { uint32_t k = left < 2048 ? left : 2048; fwrite(sec + 24, 1, k, o); left -= k; }
            if (!(++done & 255)) progress(done, total);
        }
        fclose(o);
    }
    progress(total, total);
    return 1;
}

static int copy_file(const char *from, const char *to)
{
    make_dirs(to);
    return CopyFileA(from, to, FALSE);
}

static int copy_tree(const char *from, const char *to)     /* a folder of game files, already extracted once */
{
    char pat[MAX_PATH * 2], a[MAX_PATH * 2], b[MAX_PATH * 2];
    WIN32_FIND_DATAA fd;
    HANDLE h;
    int ok = 1;
    snprintf(pat, sizeof pat, "%s\\*", from);
    if ((h = FindFirstFileA(pat, &fd)) == INVALID_HANDLE_VALUE) return 0;
    do {
        if (!strcmp(fd.cFileName, ".") || !strcmp(fd.cFileName, "..")) continue;
        snprintf(a, sizeof a, "%s\\%s", from, fd.cFileName);
        snprintf(b, sizeof b, "%s\\%s", to, fd.cFileName);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ok &= copy_tree(a, b);
        else ok &= copy_file(a, b);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return ok;
}

static const char *in(const char *dir, const char *name)      /* dir + name, in a buffer that lasts one call */
{
    static char buf[MAX_PATH * 2];
    snprintf(buf, sizeof buf, "%s%s", dir, name);
    return buf;
}

static void write_text(const char *path, const char *text)
{
    FILE *f;
    make_dirs(path);
    if ((f = fopen(path, "w"))) { fputs(text, f); fclose(f); }
}

/* ---- platforms ---- */
enum { PC, N3DS, ANDROID, NPLAT };
static const char *PLAT_NAME[NPLAT] = { "PC (Windows)", "Nintendo 3DS", "Android" };
static const char *PLAT_PROGRAM[NPLAT] = { "files\\pc\\hotel.exe", "files\\3ds\\HotelMario.3dsx", "files\\android\\HotelMario.apk" };
static int have_program(int p) { char f[MAX_PATH * 2]; snprintf(f, sizeof f, "%s\\%s", here, PLAT_PROGRAM[p]); return file_exists(f); }

/* ---- the window ---- */
enum { ID_DISC = 100, ID_DISC_BROWSE, ID_OUT, ID_OUT_BROWSE, ID_GO, ID_STATUS, ID_PROGRESS, ID_PLAT0 = 200 };
#define WM_STATUS   (WM_APP + 1)
#define WM_PROGRESS (WM_APP + 2)
#define WM_FINISHED (WM_APP + 3)

static HWND win, h_disc, h_out, h_go, h_status, h_progress, h_plat[NPLAT];
static HFONT font;
static char job_disc[MAX_PATH], job_out[MAX_PATH];
static int job_plat[NPLAT], job_ok;
static char status_text[512];

static void status(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(status_text, sizeof status_text, fmt, ap);
    va_end(ap);
    if (win) SendMessageA(win, WM_STATUS, 0, 0);
    else fprintf(stderr, "%s\n", status_text);       /* command-line mode */
}
static int prog_base, prog_span;           /* this step's share of the bar, in 1000ths */
static void progress(int done, int total)
{
    if (win) PostMessageA(win,WM_PROGRESS, prog_base + (total ? (int)((long long)done * prog_span / total) : prog_span), 0);
}

static DWORD WINAPI job(LPVOID unused)
{
    static uint8_t buf[1 << 20];
    char sha[41], err[256] = "", game[MAX_PATH * 2], from[MAX_PATH * 2], to[MAX_PATH * 2];
    long long size, readn = 0;
    Sha1 s;
    size_t n;
    int i, known = -1, p, nsteps = 0, step = 0;
    (void)unused;

    if (!(disc = fopen(job_disc, "rb"))) { status("Couldn't open the disc image."); goto out; }
    _fseeki64(disc, 0, SEEK_END);
    size = _ftelli64(disc);
    _fseeki64(disc, 0, SEEK_SET);
    status("Checking your disc image...");
    prog_base = 0; prog_span = 300;
    sha1_init(&s);
    while ((n = fread(buf, 1, sizeof buf, disc)) > 0) {
        sha1_add(&s, buf, n);
        readn += n;
        if (!(readn & ((8 << 20) - 1))) progress((int)(readn >> 10), (int)(size >> 10));
    }
    sha1_hex(&s, sha);
    for (i = 0; i < (int)(sizeof KNOWN / sizeof *KNOWN); i++) if (!strcmp(sha, KNOWN[i].sha1)) known = i;
    if (known < 0) {
        status("This isn't a disc image the patcher knows (SHA-1 %s). It needs a raw .bin (2352 bytes a sector) "
               "of Hotel Mario (Europe), as dumped from your own disc.", sha);
        goto out;
    }
    if (!KNOWN[known].supported) { status("%s is recognised, but not supported yet. Please use Hotel Mario (Europe).", KNOWN[known].title); goto out; }

    status("Found %s. Reading the disc...", KNOWN[known].title);
    if (!read_disc(err, sizeof err)) { status("%s", err[0] ? err : "Couldn't read the disc's files."); goto out; }

    for (p = 0; p < NPLAT; p++) nsteps += job_plat[p];
    snprintf(game, sizeof game, "%s\\Hotel Mario (game files)", job_out);   /* extracted once, copied per platform */
    status("Extracting the game files...");
    prog_base = 300; prog_span = 400;
    if (!extract_to(game, progress)) { status("Couldn't write the game files to %s.", game); goto out; }

    for (p = 0; p < NPLAT; p++) {
        if (!job_plat[p]) continue;
        prog_base = 700 + 300 * step / nsteps; prog_span = 300 / nsteps;
        step++;
        status("Building the %s version...", PLAT_NAME[p]);
        if (p == PC) {
            snprintf(to, sizeof to, "%s\\Hotel Mario (PC)", job_out);
            snprintf(from, sizeof from, "%s\\files\\pc\\hotel.exe", here);
            copy_file(from, in(to, "\\hotel.exe"));
            snprintf(from, sizeof from, "%s\\files\\pc\\SDL2.dll", here);
            copy_file(from, in(to, "\\SDL2.dll"));
            copy_tree(game, in(to, "\\game"));
            write_text(in(to, "\\README.txt"),
                       "Hotel Mario for PC\r\n\r\nDouble-click hotel.exe to play.\r\n\r\n"
                       "Arrows / stick move, Z or A is button 1, X or B is button 2, F1 settings, F11 fullscreen, Esc quits.\r\n"
                       "Saves go in the saves folder next to hotel.exe.\r\n");
        } else if (p == N3DS) {
            snprintf(to, sizeof to, "%s\\Hotel Mario (3DS)\\3ds\\HotelMario", job_out);
            snprintf(from, sizeof from, "%s\\files\\3ds\\HotelMario.3dsx", here);
            copy_file(from, in(to, "\\HotelMario.3dsx"));
            copy_tree(game, in(to, "\\game"));
            snprintf(to, sizeof to, "%s\\Hotel Mario (3DS)", job_out);
            write_text(in(to, "\\README.txt"), "Hotel Mario for Nintendo 3DS (homebrew)\r\n\r\n"
                            "Copy the 3ds folder to the root of your 3DS SD card, then start Hotel Mario from the Homebrew Launcher.\r\n"
                            "Sound on a real 3DS needs sdmc:/3ds/dspfirm.cdc (dump it once with DSP1).\r\n");
        } else {
            snprintf(to, sizeof to, "%s\\Hotel Mario (Android)", job_out);
            snprintf(from, sizeof from, "%s\\files\\android\\HotelMario.apk", here);
            copy_file(from, in(to, "\\HotelMario.apk"));
            copy_tree(game, in(to, "\\HotelMario\\game"));
            write_text(in(to, "\\README.txt"),
                       "Hotel Mario for Android\r\n\r\n"
                       "1. Copy the HotelMario folder to the device's internal storage, so the files are in /sdcard/HotelMario/game.\r\n"
                       "2. Install HotelMario.apk and allow it to read storage.\r\n");
        }
        progress(1, 1);
    }
    {   /* the shared copy of the game files has been copied into each version: remove it */
        SHFILEOPSTRUCTA op;
        static char from2[MAX_PATH * 2 + 2];
        memset(&op, 0, sizeof op);
        memset(from2, 0, sizeof from2);
        snprintf(from2, sizeof from2 - 1, "%s", game);                /* double-zero ended, as SHFileOperation needs */
        op.wFunc = FO_DELETE;
        op.pFrom = from2;
        op.fFlags = FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_SILENT;
        SHFileOperationA(&op);
    }
    status("Done. Your Hotel Mario is in %s", job_out);
    job_ok = 1;
    if (win) {
        PostMessageA(win, WM_PROGRESS, 1000, 0);
        ShellExecuteA(0, "open", job_out, 0, 0, SW_SHOWNORMAL);
    }
out:
    if (disc) { fclose(disc); disc = 0; }
    if (win) PostMessageA(win, WM_FINISHED, 0, 0);
    return 0;
}

static void browse_disc(void)
{
    char file[MAX_PATH] = "";
    OPENFILENAMEA o;
    memset(&o, 0, sizeof o);
    o.lStructSize = sizeof o;
    o.hwndOwner = win;
    o.lpstrFilter = "CD-i disc image (*.bin)\0*.bin\0All files\0*.*\0";
    o.lpstrFile = file;
    o.nMaxFile = sizeof file;
    o.lpstrTitle = "Your Hotel Mario disc image";
    o.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (GetOpenFileNameA(&o)) SetWindowTextA(h_disc, file);
}

static void browse_out(void)
{
    BROWSEINFOA bi;
    char name[MAX_PATH], path[MAX_PATH];
    LPITEMIDLIST id;
    memset(&bi, 0, sizeof bi);
    bi.hwndOwner = win;
    bi.pszDisplayName = name;
    bi.lpszTitle = "Where should Hotel Mario go?";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    if ((id = SHBrowseForFolderA(&bi))) {
        if (SHGetPathFromIDListA(id, path)) SetWindowTextA(h_out, path);
        CoTaskMemFree(id);
    }
}

static HWND add(const char *cls, const char *text, DWORD style, int x, int y, int w, int h, int id)
{
    HWND c = CreateWindowExA(!strcmp(cls, "EDIT") ? WS_EX_CLIENTEDGE : 0, cls, text, WS_CHILD | WS_VISIBLE | style,
                             x, y, w, h, win, (HMENU)(INT_PTR)id, GetModuleHandleA(0), 0);
    SendMessageA(c, WM_SETFONT, (WPARAM)font, TRUE);
    return c;
}

static LRESULT CALLBACK proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    int p;
    switch (m) {
    case WM_CREATE: {
        NONCLIENTMETRICSA nm;
        char out[MAX_PATH * 2];
        int y = 14;
        win = h;
        nm.cbSize = sizeof nm;
        SystemParametersInfoA(SPI_GETNONCLIENTMETRICS, sizeof nm, &nm, 0);
        font = CreateFontIndirectA(&nm.lfMessageFont);
        add("STATIC", "Hotel Mario needs your own copy of the game. Pick the disc image you made from your disc "
                      "(a .bin, Hotel Mario Europe); the patcher checks it and builds the versions you tick below.",
            0, 14, y, 512, 34, 0);
        y += 44;
        add("STATIC", "Disc image:", 0, 14, y + 3, 90, 20, 0);
        h_disc = add("EDIT", "", ES_AUTOHSCROLL, 104, y, 330, 22, ID_DISC);
        add("BUTTON", "Browse...", BS_PUSHBUTTON, 442, y - 1, 84, 24, ID_DISC_BROWSE);
        y += 34;
        add("STATIC", "Make versions for:", 0, 14, y, 200, 20, 0);
        y += 22;
        for (p = 0; p < NPLAT; p++) {
            char label[128];
            int ok = have_program(p);
            snprintf(label, sizeof label, ok ? "%s" : "%s (not in this release)", PLAT_NAME[p]);
            h_plat[p] = add("BUTTON", label, BS_AUTOCHECKBOX, 30, y, 300, 20, ID_PLAT0 + p);
            if (ok && p == PC) SendMessageA(h_plat[p], BM_SETCHECK, BST_CHECKED, 0);
            EnableWindow(h_plat[p], ok);
            y += 22;
        }
        y += 10;
        add("STATIC", "Put them in:", 0, 14, y + 3, 90, 20, 0);
        snprintf(out, sizeof out, "%s\\Hotel Mario", here);
        h_out = add("EDIT", out, ES_AUTOHSCROLL, 104, y, 330, 22, ID_OUT);
        add("BUTTON", "Browse...", BS_PUSHBUTTON, 442, y - 1, 84, 24, ID_OUT_BROWSE);
        y += 38;
        h_go = add("BUTTON", "Make Hotel Mario", BS_DEFPUSHBUTTON, 14, y, 512, 30, ID_GO);
        y += 40;
        h_progress = add(PROGRESS_CLASSA, "", 0, 14, y, 512, 16, ID_PROGRESS);
        SendMessageA(h_progress, PBM_SETRANGE, 0, MAKELPARAM(0, 1000));
        y += 24;
        h_status = add("STATIC", "", 0, 14, y, 512, 50, ID_STATUS);
        return 0;
    }
    case WM_COMMAND:
        switch (LOWORD(w)) {
        case ID_DISC_BROWSE: browse_disc(); break;
        case ID_OUT_BROWSE: browse_out(); break;
        case ID_GO: {
            int any = 0;
            GetWindowTextA(h_disc, job_disc, sizeof job_disc);
            GetWindowTextA(h_out, job_out, sizeof job_out);
            for (p = 0; p < NPLAT; p++) any |= job_plat[p] = SendMessageA(h_plat[p], BM_GETCHECK, 0, 0) == BST_CHECKED;
            if (!job_disc[0]) { MessageBoxA(h, "Please pick your disc image first.", "Hotel Mario", MB_ICONINFORMATION); break; }
            if (!any) { MessageBoxA(h, "Please tick at least one version to make.", "Hotel Mario", MB_ICONINFORMATION); break; }
            EnableWindow(h_go, FALSE);
            SendMessageA(h_progress, PBM_SETPOS, 0, 0);
            CloseHandle(CreateThread(0, 0, job, 0, 0, 0));
            break;
        }
        }
        return 0;
    case WM_STATUS: SetWindowTextA(h_status, status_text); return 0;
    case WM_PROGRESS: SendMessageA(h_progress, PBM_SETPOS, w, 0); return 0;
    case WM_FINISHED: EnableWindow(h_go, TRUE); return 0;
    case WM_CTLCOLORSTATIC: SetBkMode((HDC)w, TRANSPARENT); return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcA(h, m, w, l);
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show)
{
    WNDCLASSA wc;
    MSG msg;
    INITCOMMONCONTROLSEX icc = { sizeof icc, ICC_PROGRESS_CLASS };
    RECT r = { 0, 0, 540, 362 };
    char *slash;
    (void)prev; (void)cmd;
    InitCommonControlsEx(&icc);
    CoInitialize(0);
    GetModuleFileNameA(0, here, sizeof here);
    if ((slash = strrchr(here, '\\'))) *slash = 0;
    {   /* command line, for scripts and tests: --disc <image> --out <folder> [--pc] [--3ds] [--android] */
        int i, p;
        for (i = 1; i < __argc; i++) {
            if (!strcmp(__argv[i], "--disc") && i + 1 < __argc) snprintf(job_disc, sizeof job_disc, "%s", __argv[++i]);
            else if (!strcmp(__argv[i], "--out") && i + 1 < __argc) snprintf(job_out, sizeof job_out, "%s", __argv[++i]);
            else if (!strcmp(__argv[i], "--pc")) job_plat[PC] = 1;
            else if (!strcmp(__argv[i], "--3ds")) job_plat[N3DS] = 1;
            else if (!strcmp(__argv[i], "--android")) job_plat[ANDROID] = 1;
        }
        if (job_disc[0]) {
            if (!job_out[0]) snprintf(job_out, sizeof job_out, "%s\\Hotel Mario", here);
            for (p = 0; p < NPLAT; p++)
                if (job_plat[p] && !have_program(p)) { fprintf(stderr, "no %s program in this release\n", PLAT_NAME[p]); return 2; }
            job(0);
            return job_ok ? 0 : 1;
        }
    }
    memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = proc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(0, IDC_ARROW);
    wc.hIcon = LoadIcon(inst, MAKEINTRESOURCE(1));
    wc.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
    wc.lpszClassName = "HotelMarioPatcher";
    RegisterClassA(&wc);
    AdjustWindowRect(&r, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE);
    CreateWindowA("HotelMarioPatcher", "Hotel Mario setup", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_VISIBLE,
                  CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left, r.bottom - r.top, 0, 0, inst, 0);
    while (GetMessageA(&msg, 0, 0, 0) > 0) {
        if (!IsDialogMessageA(win, &msg)) { TranslateMessage(&msg); DispatchMessageA(&msg); }
    }
    return 0;
}
