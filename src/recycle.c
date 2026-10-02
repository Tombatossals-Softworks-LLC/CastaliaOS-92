/* ======================================================================
 * recycle.c - the Recycle Bin for CASTALIA/386
 * ----------------------------------------------------------------------
 * The bin on drive X is the hidden folder X:\RECYCLED.  A recycled file
 * keeps its contents and its extension and is renamed DCn.EXT there (a
 * rename on one drive: instant, whatever the size), and X:\RECYCLED\
 * INFO.TXT gains a line "DCn.EXT<tab>C:\FULL\ORIGINAL.PTH".  Records are
 * only ever appended, or removed by streaming the file through a temp
 * copy, so a bin with more entries than the window can list is never
 * truncated by it.
 * ====================================================================== */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>    /* _fullpath                                       */
#include <dos.h>       /* _dos_findfirst, _dos_setfileattr, drive count   */
#include <direct.h>    /* mkdir                                           */
#include <i86.h>       /* int86, MK_FP                                    */
#include "recycle.h"
#include "video.h"
#include "ui.h"
#include "font.h"
#include "keyboard.h"
#include "dialog.h"
#include "system.h"    /* sys_save_open / sys_commit_file                 */
#include "files.h"     /* files_rescan: a restore shows up in the Cabinet */

#define RB_MAX   64                    /* entries the window lists        */
#define RB_PLEN  80                    /* original path, with its NUL     */
#define RB_FULL  96                    /* _fullpath scratch               */
#define RB_LINE  (RB_PLEN + 20)

typedef struct {
    char          bin[13];             /* DCn.EXT inside X:\RECYCLED      */
    char          orig[RB_PLEN];       /* where it came from              */
    unsigned long size;
} RbEnt;

/* The list lives in a DOS block taken when the window opens and given
   back when it closes: 6 KB nobody needs while the bin is not on screen. */
static unsigned    g_seg = 0;
static RbEnt far  *g_rb  = (RbEnt far *)0;
static int         g_n, g_sel, g_scroll, g_vis, g_row_y0, g_row_h;
static bool_t      g_full;
static unsigned long g_bytes;
static Rect        g_list, g_btn[3];

static const char * const BTN[3] = { "Restore", "Delete", "Empty" };

static char upc(char c)
{
    return (char)((c >= 'a' && c <= 'z') ? c - 32 : c);
}

static void to_far(char far *d, const char *s, int cap)
{
    int i = 0;
    while (s[i] != '\0' && i < cap - 1) { d[i] = s[i]; ++i; }
    d[i] = '\0';
}

static void to_near(char *d, const char far *s, int cap)
{
    int i = 0;
    while (s[i] != '\0' && i < cap - 1) { d[i] = s[i]; ++i; }
    d[i] = '\0';
}

/* INT 21h AX=4408h: AX = 1 for a fixed disk.  CF (a network drive, a
   CD-ROM, no such drive) and 0 (removable) both mean: no bin here. */
static bool_t drive_fixed(char drv)
{
    union REGS r;
    r.x.ax = 0x4408;
    r.h.bl = (unsigned char)(drv - 'A' + 1);
    int86(0x21, &r, &r);
    return (!r.x.cflag && r.x.ax == 1) ? TRUE : FALSE;
}

static bool_t exists(const char *path)
{
    struct find_t ff;
    return (_dos_findfirst(path, _A_NORMAL | _A_RDONLY | _A_HIDDEN |
                           _A_SYSTEM | _A_ARCH | _A_SUBDIR, &ff) == 0)
           ? TRUE : FALSE;
}

bool_t recycle_shift_held(void)
{
    return (*(const unsigned char far *)MK_FP(0x40, 0x17) & 0x03)
           ? TRUE : FALSE;
}

bool_t recycle_available(const char *name)
{
    char full[RB_FULL];
    if (_fullpath(full, name, RB_FULL) == NULL)
        return FALSE;
    return drive_fixed(upc(full[0]));
}

bool_t recycle_file(const char *name)
{
    char full[RB_FULL], dir[16], dst[32], info[32], ext[5];
    FILE *f;
    int i, n, dot = -1;
    bool_t ok;

    if (_fullpath(full, name, RB_FULL) == NULL ||
        (int)strlen(full) >= RB_PLEN)
        return FALSE;
    full[0] = upc(full[0]);
    if (!drive_fixed(full[0]))
        return FALSE;
    sprintf(dir, "%c:\\RECYCLED", full[0]);
    if (!exists(dir)) {
        if (mkdir(dir) != 0)
            return FALSE;
        (void)_dos_setfileattr(dir, _A_HIDDEN);
    }
    for (i = 0; full[i] != '\0'; ++i) {
        if (full[i] == '.')       dot = i;
        else if (full[i] == '\\') dot = -1;
    }
    ext[0] = '\0';
    if (dot >= 0) {
        strncpy(ext, full + dot, 4);
        ext[4] = '\0';
    }
    for (n = 1; n < 1000; ++n) {
        sprintf(dst, "%s\\DC%d%s", dir, n, ext);
        if (!exists(dst))
            break;
    }
    if (n >= 1000 || rename(name, dst) != 0)
        return FALSE;

    /* Record it.  A file in the bin with no record is a file nobody can
       find again, so if the line cannot be written it goes back. */
    sprintf(info, "%s\\INFO.TXT", dir);
    f = fopen(info, "a");
    ok = (f != NULL) ? TRUE : FALSE;
    if (ok && fprintf(f, "%s\t%s\n", dst + strlen(dir) + 1, full) < 0)
        ok = FALSE;
    if (f != NULL && fclose(f) != 0)
        ok = FALSE;
    if (!ok)
        (void)rename(dst, name);
    return ok;
}

/* ---- the window's list ------------------------------------------------ */

static bool_t ensure_mem(void)
{
    if (g_seg != 0)
        return TRUE;
    if (_dos_allocmem((unsigned)((RB_MAX * sizeof(RbEnt) + 15U) / 16U),
                      &g_seg) != 0) {
        g_seg = 0;
        return FALSE;
    }
    g_rb = (RbEnt far *)MK_FP(g_seg, 0);
    return TRUE;
}

void recycle_release(void)
{
    if (g_seg != 0)
        _dos_freemem(g_seg);
    g_seg = 0;
    g_rb  = (RbEnt far *)0;
    g_n   = 0;
}

static void load_drive(char drv)
{
    char info[24], line[RB_LINE], path[32];
    struct find_t ff;
    FILE *f;

    sprintf(info, "%c:\\RECYCLED\\INFO.TXT", drv);
    f = fopen(info, "r");
    if (f == NULL)
        return;
    while (fgets(line, (int)sizeof(line), f) != NULL) {
        char *tab = strchr(line, '\t'), *e;
        if (tab == NULL)
            continue;
        *tab++ = '\0';
        for (e = tab; *e != '\0' && *e != '\n' && *e != '\r'; ++e)
            ;
        *e = '\0';
        if (strlen(line) > 12 || tab[0] == '\0')
            continue;
        sprintf(path, "%c:\\RECYCLED\\%s", drv, line);
        if (_dos_findfirst(path, _A_NORMAL | _A_RDONLY | _A_HIDDEN |
                           _A_SYSTEM | _A_ARCH, &ff) != 0)
            continue;                  /* record outlived its file        */
        if (g_n >= RB_MAX) {
            g_full = TRUE;
            break;
        }
        to_far(g_rb[g_n].bin, line, 13);
        to_far(g_rb[g_n].orig, tab, RB_PLEN);
        g_rb[g_n].size = (unsigned long)ff.size;
        g_bytes += (unsigned long)ff.size;
        ++g_n;
    }
    fclose(f);
}

void recycle_open(void)
{
    unsigned cur, total;
    char d;
    g_n = 0;
    g_sel = g_scroll = 0;
    g_full = FALSE;
    g_bytes = 0;
    if (!ensure_mem())
        return;
    _dos_getdrive(&cur);
    _dos_setdrive(cur, &total);        /* total = LASTDRIVE               */
    for (d = 'C'; d < (char)('A' + total) && d <= 'Z'; ++d)
        if (drive_fixed(d))
            load_drive(d);
}

/* Drop record `bin` from drive drv's INFO.TXT, streaming it through a
   temp copy (so records past what the window holds are kept). */
static void info_drop(char drv, const char *bin)
{
    char info[24], tmp[24], line[RB_LINE];
    FILE *in, *out;
    int n = (int)strlen(bin);
    sprintf(info, "%c:\\RECYCLED\\INFO.TXT", drv);
    in = fopen(info, "r");
    if (in == NULL)
        return;
    out = sys_save_open(info, tmp, (int)sizeof(tmp), "w");
    if (out == NULL) {
        fclose(in);
        return;
    }
    while (fgets(line, (int)sizeof(line), in) != NULL)
        if (!(strncmp(line, bin, (size_t)n) == 0 && line[n] == '\t'))
            fputs(line, out);
    fclose(in);
    (void)sys_commit_file(out, tmp, info);
}

static void entry(int i, char *bin, char *orig, char *src)
{
    to_near(bin, g_rb[i].bin, 13);
    to_near(orig, g_rb[i].orig, RB_PLEN);
    sprintf(src, "%c:\\RECYCLED\\%s", orig[0], bin);
}

static void forget(int i)
{
    g_bytes -= g_rb[i].size;
    for (; i < g_n - 1; ++i)
        g_rb[i] = g_rb[i + 1];
    if (g_n > 0) --g_n;
    if (g_sel >= g_n && g_sel > 0) --g_sel;
}

/* Every missing folder on the way to `path`, as Windows 95 recreated
   the folder a restored file had lived in. */
static void make_parents(const char *path)
{
    char p[RB_PLEN];
    int i;
    strcpy(p, path);
    for (i = 3; p[i] != '\0'; ++i)
        if (p[i] == '\\') {
            p[i] = '\0';
            (void)mkdir(p);
            p[i] = '\\';
        }
}

static void do_restore(void)
{
    char bin[13], orig[RB_PLEN], src[32];
    if (g_n == 0)
        return;
    entry(g_sel, bin, orig, src);
    if (exists(orig)) {
        dialog_message("Restore", "A file of that name is", "already there.");
        return;
    }
    if (rename(src, orig) != 0) {
        make_parents(orig);
        if (rename(src, orig) != 0) {
            dialog_message("Restore", "Could not restore to", orig);
            return;
        }
    }
    info_drop(orig[0], bin);
    forget(g_sel);
    files_rescan();
}

static void do_delete(void)
{
    char bin[13], orig[RB_PLEN], src[32];
    const char *base;
    if (g_n == 0)
        return;
    entry(g_sel, bin, orig, src);
    base = strrchr(orig, '\\');
    base = (base != NULL) ? base + 1 : orig;
    if (dialog_confirm("Delete", "Delete this file for good?", base)
        != DLG_YES)
        return;
    (void)_dos_setfileattr(src, _A_NORMAL);   /* a read-only one too   */
    if (remove(src) != 0) {
        dialog_message("Delete", "Could not delete.", base);
        return;
    }
    info_drop(orig[0], bin);
    forget(g_sel);
}

/* Empty every drive's bin: every file in X:\RECYCLED goes, INFO.TXT and
   any stray included, so even a bin too full for the list is emptied. */
static void do_empty(void)
{
    unsigned cur, total;
    char d, spec[20], path[32], q[40];
    struct find_t ff;
    int failed = 0;
    if (g_n == 0 && !g_full)
        return;
    sprintf(q, "%d item%s, deleted for good.", g_n, (g_n == 1) ? "" : "s");
    if (dialog_confirm("Empty Recycle Bin", q, "Are you sure?") != DLG_YES)
        return;
    _dos_getdrive(&cur);
    _dos_setdrive(cur, &total);
    for (d = 'C'; d < (char)('A' + total) && d <= 'Z'; ++d) {
        unsigned rc;
        if (!drive_fixed(d))
            continue;
        sprintf(spec, "%c:\\RECYCLED\\*.*", d);
        rc = _dos_findfirst(spec, _A_NORMAL | _A_RDONLY | _A_HIDDEN |
                            _A_SYSTEM | _A_ARCH, &ff);
        while (rc == 0) {
            sprintf(path, "%c:\\RECYCLED\\%s", d, ff.name);
            (void)_dos_setfileattr(path, _A_NORMAL);
            if (remove(path) != 0)
                ++failed;
            rc = _dos_findnext(&ff);
        }
    }
    if (failed > 0)
        dialog_message("Empty Recycle Bin", "Some files could not be",
                       "deleted.");
    recycle_open();
}

/* ---- drawing ------------------------------------------------------------ */

void recycle_draw(const Rect *cl)
{
    int lh = font_h() + 2, x = cl->x + 4, y = cl->y + 3, i, bw;
    char b[48];

    bw = (cl->w - 8 - 8) / 3;
    for (i = 0; i < 3; ++i) {
        rect_set(&g_btn[i], x + i * (bw + 4), y, bw, font_h() + 5);
        ui_button(&g_btn[i], BTN[i], FALSE);
    }
    y += font_h() + 9;

    if (g_rb == (RbEnt far *)0)
        strcpy(b, "Not enough memory to list it.");
    else if (g_n == 0)
        strcpy(b, "The Recycle Bin is empty.");
    else
        sprintf(b, "%d item%s, %lu KB%s", g_n, (g_n == 1) ? "" : "s",
                (g_bytes + 1023UL) / 1024UL, g_full ? " (list full)" : "");
    font_draw(x, y, b, (g_n > 0) ? C_TITLE : C_DKGRAY);
    y += lh;

    rect_set(&g_list, x, y, cl->w - 8, cl->y + cl->h - y - 3);
    vid_fillrect(g_list.x, g_list.y, g_list.w, g_list.h, C_WHITE);
    ui_sink(g_list.x, g_list.y, g_list.w, g_list.h);
    g_vis = (g_list.h - 4) / lh;
    if (g_vis < 1) g_vis = 1;
    if (g_sel < g_scroll) g_scroll = g_sel;
    if (g_sel >= g_scroll + g_vis) g_scroll = g_sel - g_vis + 1;
    if (g_scroll < 0) g_scroll = 0;
    g_row_y0 = g_list.y + 2;
    g_row_h  = lh;
    for (i = 0; i < g_vis && g_scroll + i < g_n; ++i) {
        char orig[RB_PLEN], line[RB_PLEN + 16];
        char *base;
        int k = g_scroll + i, ry = g_row_y0 + i * lh;
        int cols = (g_list.w - 6) / font_adv();
        to_near(orig, g_rb[k].orig, RB_PLEN);
        base = strrchr(orig, '\\');
        if (base != NULL) *base++ = '\0'; else base = orig;
        /* NAME.EXT  folder - the name is what you are looking for. */
        sprintf(line, "%-12s %s", base, orig);
        if (k == g_sel)
            vid_fillrect(g_list.x + 2, ry, g_list.w - 4, lh, C_TITLE);
        font_draw_n(g_list.x + 3, ry + 1, line, cols,
                    (k == g_sel) ? C_WHITE : C_BLACK);
    }
}

/* ---- input ---------------------------------------------------------------- */

static void act(int b)
{
    if (b == 0)      do_restore();
    else if (b == 1) do_delete();
    else             do_empty();
}

bool_t recycle_key(int key)
{
    switch (key) {
    case KEY_UP:   if (g_sel > 0) --g_sel;            return TRUE;
    case KEY_DOWN: if (g_sel < g_n - 1) ++g_sel;      return TRUE;
    case KEY_PGUP: g_sel -= g_vis; if (g_sel < 0) g_sel = 0; return TRUE;
    case KEY_PGDN: g_sel += g_vis;
                   if (g_sel > g_n - 1) g_sel = (g_n > 0) ? g_n - 1 : 0;
                   return TRUE;
    case KEY_HOME: g_sel = 0;                         return TRUE;
    case KEY_END:  g_sel = (g_n > 0) ? g_n - 1 : 0;   return TRUE;
    case KEY_ENTER: case 'r': case 'R': act(0);      return TRUE;
    case KEY_DEL:                       act(1);      return TRUE;
    case 'e': case 'E':                 act(2);      return TRUE;
    case KEY_F5:   recycle_open();                    return TRUE;
    default:       break;
    }
    return FALSE;
}

bool_t recycle_click(const Rect *cl, int mx, int my)
{
    int i;
    (void)cl;
    for (i = 0; i < 3; ++i)
        if (rect_contains(&g_btn[i], mx, my)) {
            act(i);
            return TRUE;
        }
    if (rect_contains(&g_list, mx, my) && g_row_h > 0) {
        i = g_scroll + (my - g_row_y0) / g_row_h;
        if (i >= 0 && i < g_n) {
            g_sel = i;
            return TRUE;
        }
    }
    return FALSE;
}
