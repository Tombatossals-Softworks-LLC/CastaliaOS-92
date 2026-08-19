/* ======================================================================
 * find.c - Find File (a Win95-style file search) for Castalia 92
 * ----------------------------------------------------------------------
 * The classic "Find: Files or Folders" accessory: give it an 8.3
 * wildcard and it sweeps the whole drive, breadth-first.  A queue of
 * directory paths lives in one far block (DGROUP is nearly full), and
 * each directory is scanned with its own findfirst/findnext run - one
 * find_t at a time, so the DOS DTA is never used re-entrantly.
 * ====================================================================== */
#include <dos.h>       /* _dos_findfirst / _dos_findnext / _dos_allocmem */
#include <direct.h>    /* getcwd                                         */
#include <stdio.h>
#include <string.h>
#include "find.h"
#include "video.h"
#include "ui.h"
#include "font.h"
#include "keyboard.h"
#include "textscan.h"

#define FIND_MAXD  160          /* directory queue capacity               */
#define FIND_MAXR  96           /* result list capacity                   */
#define FIND_PLEN  64           /* one full directory path, incl. NUL     */

typedef struct {
    char          name[13];     /* 8.3 file name                          */
    unsigned long size;         /* bytes                                  */
    u16           dir;          /* index into the directory queue         */
} FRes;

static char   g_pat[13] = "*.EXE";
static bool_t g_ran     = FALSE;   /* a search has completed              */
static bool_t g_nomem   = FALSE;   /* the scratch block could not be had  */
static int    g_nres, g_scanned;
static bool_t g_full;              /* a cap was hit; results are partial  */
static int    g_scroll;
/* The list had no SELECTION at all: arrows scrolled the view, Enter
   re-prompted for a pattern, and there was no way to reach a result.
   Seven files on screen and the applet could not open one of them. */
static int    g_sel = 0;
static Rect   g_list;                  /* the results box, for hit tests  */
static bool_t g_want_open = FALSE;
/* F4: show the result's FOLDER in the Disk Cabinet rather than opening
   the file.  Enter has always opened it through the association route,
   which is the right default - but "where IS this thing" is the other
   half of finding it, and sweeping the whole drive to be told a path you
   then have to navigate to by hand is a poor trade. */
static bool_t g_want_goto = FALSE;
/* g_sel starts at 0, so without this the very first click on the top
   result satisfied "i == g_sel" and opened it immediately - one click,
   not two, and only for that row. */
static bool_t g_clicked = FALSE;
/* Follow the selection only when the KEYBOARD moved it.  Doing it on
   every draw is what made drawer.c's scroll arrows dead: the redraw
   snapped the view straight back to the selected row. */
static bool_t g_follow = FALSE;
static int    g_vis = 8;      /* rows the last draw could show    */
/* Row geometry PUBLISHED BY THE DRAW.  The hit test used to recompute it
   and got it wrong - font_h()+1 against the draw's font_h()+2 - so the
   mapping drifted a pixel per row: row 9 became unclickable and clicking
   a row selected, then OPENED, the one below it.  Never recompute
   geometry the draw already knows. */
static int    g_row_y0 = 0, g_row_h = 1;

/* The scroll strip beside the results. */
#define FSCROLL_W 12
static void fscroll_rects(const Rect *box, Rect *up, Rect *dn, Rect *tr)
{
    int bw = FSCROLL_W;
    rect_set(up, box->x + box->w, box->y, bw, bw);
    rect_set(dn, box->x + box->w, box->y + box->h - bw, bw, bw);
    rect_set(tr, box->x + box->w, box->y + bw, bw, box->h - 2 * bw);
    if (tr->h < 0) tr->h = 0;
}
static bool_t g_want_prompt = FALSE;
static bool_t g_want_tprompt = FALSE;
static Rect   g_search_btn;
static Rect   g_text_btn;

/* The text a matching file must contain, uppercased once at find_run so
   the inner loop compares against it directly; empty = name search only.
   Kept short: it is one dialog_input field and one panel row wide. */
#define FIND_TLEN 24
static char g_text[FIND_TLEN] = "";
static char g_needle[FIND_TLEN];       /* g_text, uppercased  */
static int  g_nlen = 0;                /* strlen(g_needle)    */

/* The far block: the directory queue, then the result records. */
static unsigned  g_seg  = 0;
static char far *g_dirs = (char far *)0;
static FRes far *g_res  = (FRes far *)0;
static int       g_ndir;

#define DIRP(i) (g_dirs + (long)(i) * FIND_PLEN)

static bool_t ensure_mem(void)
{
    unsigned paras;
    if (g_seg != 0)
        return TRUE;
    paras = (unsigned)(((long)FIND_MAXD * FIND_PLEN +
                        (long)FIND_MAXR * sizeof(FRes) + 15L) / 16L);
    if (_dos_allocmem(paras, &g_seg) != 0) {
        g_seg = 0;
        return FALSE;
    }
    g_dirs = (char far *)MK_FP(g_seg, 0);
    g_res  = (FRes far *)MK_FP(g_seg, FIND_MAXD * FIND_PLEN);
    return TRUE;
}

/* near -> far / far -> near bounded string copies (no far strcpy in the
   medium model's small-data world). */
static void to_far(char far *dst, const char *src, int cap)
{
    int i = 0;
    while (src[i] != '\0' && i < cap - 1) { dst[i] = src[i]; ++i; }
    dst[i] = '\0';
}

static void to_near(char *dst, const char far *src, int cap)
{
    int i = 0;
    while (src[i] != '\0' && i < cap - 1) { dst[i] = src[i]; ++i; }
    dst[i] = '\0';
}

void find_open(void)
{
    g_scroll = 0;
    g_sel = 0;
    g_clicked = FALSE;
    g_want_open = FALSE;
    g_want_goto = FALSE;
    g_want_prompt = FALSE;
    /* g_ran survived a close, so reopening the window presented the
       PREVIOUS search's hits and counts as if they were current. */
    g_ran  = FALSE;
    g_nres = 0;
    g_nomem = ensure_mem() ? FALSE : TRUE;
}

bool_t find_poll_prompt(void)
{
    bool_t w = g_want_prompt;
    g_want_prompt = FALSE;
    return w;
}

bool_t find_poll_tprompt(void)
{
    bool_t w = g_want_tprompt;
    g_want_tprompt = FALSE;
    return w;
}

const char *find_text(void)    { return g_text; }
const char *find_pattern(void) { return g_pat; }

/* One directory's sweep: results matching the pattern, then its
   subdirectories onto the queue.  Sequential findfirst runs only. */
static void scan_dir(int qi)
{
    char dir[FIND_PLEN], spec[FIND_PLEN + 14];
    struct find_t ff;
    unsigned rc;
    int dlen;

    to_near(dir, DIRP(qi), (int)sizeof(dir));
    dlen = (int)strlen(dir);
    ++g_scanned;

    /* Files matching the pattern. */
    sprintf(spec, "%s%s", dir, g_pat);
    rc = _dos_findfirst(spec, _A_RDONLY | _A_ARCH, &ff);
    while (rc == 0) {
        if ((ff.attrib & (_A_SUBDIR | _A_VOLID)) == 0) {
            if (g_nres >= FIND_MAXR) { g_full = TRUE; break; }
            if (g_nlen > 0) {
                /* Reuse spec: the name filter has already cut this down
                   to the files worth opening, and building the full path
                   here keeps the read out of the common case entirely. */
                sprintf(spec, "%s%s", dir, ff.name);
                if (!text_in_file(spec, g_needle, g_nlen)) {
                    rc = _dos_findnext(&ff);
                    continue;
                }
            }
            to_far(g_res[g_nres].name, ff.name, 13);
            g_res[g_nres].size = (unsigned long)ff.size;
            g_res[g_nres].dir  = (u16)qi;
            ++g_nres;
        }
        rc = _dos_findnext(&ff);
    }

    /* Subdirectories join the queue (skip "." and ".."). */
    sprintf(spec, "%s*.*", dir);
    rc = _dos_findfirst(spec, _A_SUBDIR, &ff);
    while (rc == 0) {
        if ((ff.attrib & _A_SUBDIR) != 0 && ff.name[0] != '.') {
            if (g_ndir >= FIND_MAXD ||
                dlen + (int)strlen(ff.name) + 2 > FIND_PLEN - 1) {
                g_full = TRUE;
            } else {
                sprintf(spec, "%s%s\\", dir, ff.name);
                to_far(DIRP(g_ndir), spec, FIND_PLEN);
                ++g_ndir;
            }
        }
        rc = _dos_findnext(&ff);
    }
}

void find_run(const char *pattern, const char *text)
{
    char cwd[FIND_PLEN], root[4];
    int qi, i;

    /* A fresh search starts at the top.  find_open reset g_sel but
       find_run did not, so searching again from 90 hits with the last one
       selected left g_sel at 89 - clamped by the draw onto the LAST item
       of the new list rather than the first. */
    g_sel = 0;
    g_clicked = FALSE;
    g_want_open = FALSE;
    g_want_goto = FALSE;

    i = 0;
    while (pattern[i] != '\0' && i < (int)sizeof(g_pat) - 1) {
        g_pat[i] = pattern[i];
        ++i;
    }
    g_pat[i] = '\0';

    /* Uppercase the needle ONCE, here, rather than per byte of every
       file scanned: the inner loop runs over every byte of every
       name-matched file and this runs once per search. */
    g_nlen = text_needle(g_needle, FIND_TLEN, text);
    /* g_text keeps the user's own capitalisation for the panel - and
       `text` may BE g_text, when the Search button re-runs with the
       filter unchanged, so copy forward index by index. */
    i = 0;
    if (text != (const char *)0) {
        while (text[i] != '\0' && i < FIND_TLEN - 1) {
            g_text[i] = text[i];
            ++i;
        }
    }
    g_text[i] = '\0';

    g_nres = g_scanned = 0;
    g_full = FALSE;
    g_scroll = 0;
    g_nomem = FALSE;
    /* Set g_ran only once the search really starts.  Failing the
       allocation used to leave it TRUE with zero results, so the panel
       reported "No matching files." and the user concluded the file was
       not on the disk. */
    if (!ensure_mem()) {
        g_nomem = TRUE;
        g_ran   = TRUE;
        return;
    }
    g_ran = TRUE;

    /* The whole current drive, from its root. */
    cwd[0] = '\0';
    getcwd(cwd, (int)sizeof(cwd));
    root[0] = (cwd[0] != '\0') ? cwd[0] : 'C';
    root[1] = ':'; root[2] = '\\'; root[3] = '\0';
    to_far(DIRP(0), root, FIND_PLEN);
    g_ndir = 1;

    for (qi = 0; qi < g_ndir; ++qi)
        scan_dir(qi);
}

/* "1234" up to 9999 bytes, then "123K", then "12M" - 4 chars, right fit. */
static void fmt_size(char *dst, unsigned long b)
{
    if (b < 10000UL)          sprintf(dst, "%lu", b);
    else if (b < 10240000UL)  sprintf(dst, "%luK", b / 1024UL);
    else                      sprintf(dst, "%luM", b / 1048576UL);
}

void find_draw(const Rect *cl)
{
    int lh = font_h() + 2;
    int x = cl->x + 4, y = cl->y + 3;
    int bw = font_adv() * 9 + 8;
    char b[48];
    Rect box;

    /* Pattern row: label, the pattern in a sunken well, the button. */
    font_draw(x, y + 3, "Pattern", C_TITLE);
    {
        int wx = x + font_adv() * 8;
        int ww = font_adv() * 13 + 6;
        vid_fillrect(wx, y, ww, font_h() + 5, C_WHITE);
        ui_sink(wx, y, ww, font_h() + 5);
        font_draw(wx + 4, y + 3, g_pat, C_BLACK);
        rect_set(&g_search_btn, cl->x + cl->w - bw - 4, y, bw, font_h() + 5);
        ui_button(&g_search_btn, "Search...", FALSE);
    }
    y += font_h() + 8;

    /* Containing row.  Shown always, empty or not: a text filter left
       over from the last search that silently dropped files from this
       one would be the worst kind of hidden mode. */
    font_draw(x, y + 3, "Text", C_TITLE);
    {
        int wx = x + font_adv() * 8;
        int ww = font_adv() * 13 + 6;
        vid_fillrect(wx, y, ww, font_h() + 5, C_WHITE);
        ui_sink(wx, y, ww, font_h() + 5);
        if (g_text[0] != '\0')
            font_draw_n(wx + 4, y + 3, g_text, 13, C_BLACK);
        else
            font_draw(wx + 4, y + 3, "(any)", C_DKGRAY);
        rect_set(&g_text_btn, cl->x + cl->w - bw - 4, y, bw, font_h() + 5);
        ui_button(&g_text_btn, "Text...", FALSE);
    }
    y += font_h() + 9;

    /* Status line. */
    if (!g_ran)
        strcpy(b, "F3 pattern, F2 text, then search");
    else
        sprintf(b, "%d found - %d dirs%s", g_nres, g_scanned,
                g_full ? " (list full)" : "");
    font_draw(x, y, b, g_ran ? C_TITLE : C_DKGRAY);
    y += lh;

    /* Results: NAME | SIZE | FOLDER, one line each, arrows scroll. */
    /* Reserve the scroll strip.  The list scrolls, but with no bar and no
       mouse route the results past the first screenful were reachable by
       keyboard only - and nothing on screen said they existed. */
    rect_set(&box, x, y, cl->w - 8 - FSCROLL_W, cl->y + cl->h - y - 3);
    g_list = box;                      /* remembered for the hit test     */
    vid_fillrect(box.x, box.y, box.w, box.h, C_WHITE);
    ui_sink(box.x, box.y, box.w, box.h);
    {
        int vis = (box.h - 5) / lh;
        int pathcol = 20;                       /* name 12 + gap + size 6  */
        int pathch  = box.w / font_adv() - pathcol - 1;
        int row;
        if (vis < 1) vis = 1;
        g_vis = vis;                   /* the key handler pages by this   */
        if (g_sel >= g_nres) g_sel = g_nres - 1;
        if (g_sel < 0)       g_sel = 0;
        if (g_follow) {
            if (g_sel < g_scroll)        g_scroll = g_sel;
            if (g_sel >= g_scroll + vis) g_scroll = g_sel - vis + 1;
            g_follow = FALSE;
        }
        if (g_scroll > g_nres - vis) g_scroll = g_nres - vis;
        if (g_scroll < 0) g_scroll = 0;
        g_row_y0 = box.y + 2;          /* published for find_click        */
        g_row_h  = lh;
        for (row = 0; row < vis && g_scroll + row < g_nres; ++row) {
            FRes far *e = &g_res[g_scroll + row];
            char nm[13], dir[FIND_PLEN], sz[8];
            int ty = box.y + 3 + row * lh;
            u8  fg = C_BLACK, sub = C_TITLE, dim = C_DKGRAY;
            if (g_scroll + row == g_sel) {      /* the selected row       */
                vid_fillrect(box.x + 1, ty - 1, box.w - 2, lh, C_TITLE);
                fg = C_WHITE; sub = C_WHITE; dim = C_FACE;
            }
            to_near(nm, e->name, (int)sizeof(nm));
            to_near(dir, DIRP(e->dir), (int)sizeof(dir));
            fmt_size(sz, e->size);
            font_draw(box.x + 3, ty, nm, fg);
            font_draw(box.x + 3 + font_adv() * 13, ty, sz, sub);
            /* The folder, tail-first when it is too long to fit. */
            {
                int dl = (int)strlen(dir);
                const char *show = dir;
                char tail[FIND_PLEN];
                if (pathch > 3 && dl > pathch) {
                    tail[0] = tail[1] = '.';
                    strcpy(tail + 2, dir + (dl - (pathch - 2)));
                    show = tail;
                }
                if (pathch > 3)
                    font_draw(box.x + 3 + font_adv() * pathcol, ty,
                              show, dim);
            }
        }
        /* The scrollbar, whenever there is more than one screenful. */
        if (g_nres > vis) {
            Rect up, dn, tr;
            fscroll_rects(&box, &up, &dn, &tr);
            ui_vscroll(&up, &dn, &tr, g_scroll, vis, g_nres);
        }
        if (g_nomem)
            ui_text_center(box.x, box.y + box.h / 2 - font_h() / 2, box.w,
                           "Not enough memory to search.", C_RED);
        else if (g_ran && g_nres == 0)
            ui_text_center(box.x, box.y + box.h / 2 - font_h() / 2, box.w,
                           "No matching files.", C_DKGRAY);
    }
}

bool_t find_click(const Rect *cl, int mx, int my)
{
    (void)cl;
    if (rect_contains(&g_search_btn, mx, my)) {
        g_want_prompt = TRUE;
        return TRUE;
    }
    if (rect_contains(&g_text_btn, mx, my)) {
        g_want_tprompt = TRUE;
        return TRUE;
    }
    if (g_nres > g_vis) {              /* the scroll arrows                */
        Rect up, dn, tr;
        fscroll_rects(&g_list, &up, &dn, &tr);
        if (rect_contains(&up, mx, my)) {
            if (g_scroll > 0) --g_scroll;
            return TRUE;
        }
        if (rect_contains(&dn, mx, my)) {
            if (g_scroll < g_nres - g_vis) ++g_scroll;
            return TRUE;
        }
    }
    if (rect_contains(&g_list, mx, my) && g_nres > 0 && g_row_h > 0) {
        int row = (my - g_row_y0) / g_row_h;
        int i   = g_scroll + row;
        /* row < g_vis as well as i < g_nres: without it, a click on the
           blank band below the last row selected an entry that was not
           on screen, and the next draw scrolled away to it. */
        if (row >= 0 && row < g_vis && i >= 0 && i < g_nres) {
            if (i == g_sel && g_clicked)   /* a SECOND click opens it      */
                g_want_open = TRUE;
            g_sel = i;
            g_clicked = TRUE;
            return TRUE;
        }
    }
    return FALSE;
}

bool_t find_key(int key)
{
    /* Enter OPENS the highlighted result now; F3 (and the Search button)
       ask for a new pattern.  Enter used to re-prompt, which meant the
       only thing the keyboard could do with a result was scroll past it. */
    if (key == KEY_ENTER) {
        if (g_nres > 0) g_want_open = TRUE;
        return TRUE;
    }
    if (key == KEY_F3) { g_want_prompt = TRUE; return TRUE; }
    if (key == KEY_F2) { g_want_tprompt = TRUE; return TRUE; }
    if (key == KEY_F4) {
        if (g_nres > 0) g_want_goto = TRUE;
        return TRUE;
    }
    if (key == KEY_UP   && g_sel > 0)          { --g_sel; g_follow = TRUE; return TRUE; }
    if (key == KEY_DOWN && g_sel < g_nres - 1) { ++g_sel; g_follow = TRUE; return TRUE; }
    if (key == KEY_PGUP && g_sel > 0) {
        g_sel -= g_vis;
        if (g_sel < 0) g_sel = 0;
        g_follow = TRUE;
        return TRUE;
    }
    if (key == KEY_PGDN && g_sel < g_nres - 1) {
        g_sel += g_vis;
        if (g_sel > g_nres - 1) g_sel = g_nres - 1;
        g_follow = TRUE;
        return TRUE;
    }
    if (key == KEY_HOME) { g_sel = 0; g_follow = TRUE; return TRUE; }
    if (key == KEY_END)  { g_sel = (g_nres > 0) ? g_nres - 1 : 0;
                           g_follow = TRUE; return TRUE; }
    return FALSE;
}

/* TRUE once when the user asked to open the highlighted result; *dir and
   *name come back as the folder and the 8.3 name, which main.c routes
   through exactly the association table the Disk Cabinet uses. */
/* Same shape as find_poll_open, and deliberately separate: main.c does
   two different things with the answer. */
bool_t find_poll_goto(char *dir, int dcap)
{
    if (!g_want_goto || g_sel < 0 || g_sel >= g_nres)
        return FALSE;
    g_want_goto = FALSE;
    to_near(dir, DIRP(g_res[g_sel].dir), dcap);
    return TRUE;
}

bool_t find_poll_open(char *dir, int dcap, char *name, int ncap)
{
    if (!g_want_open || g_sel < 0 || g_sel >= g_nres)
        return FALSE;
    g_want_open = FALSE;
    to_near(name, g_res[g_sel].name, ncap);
    to_near(dir, DIRP(g_res[g_sel].dir), dcap);
    return TRUE;
}
