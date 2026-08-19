/* ======================================================================
 * menu.c - The Dominus Start menu for CASTALIA/386 (Windows-95 cascade)
 * ----------------------------------------------------------------------
 * A single-column pop-up in the Windows-95 shape: the pinned [shortcut]
 * entries and built-in apps live under a "Programs" submenu, and Documents
 * / Settings / Media cascade to the right on hover.  Help, Run and Shut
 * Down sit at the foot, and a vertical CASTALIA brand stripe runs down the
 * left, just like the real thing.
 * ====================================================================== */
#include "menu.h"
#include "video.h"
#include "ui.h"
#include "font.h"
#include "keyboard.h"
#include "desktop.h"   /* TASKBAR_Y: the submenu must not cover the bar */
#include "recent.h"    /* the Documents menu leads with what you last opened */

#define ITEM_H   (font_h() + 4)
#define PAD_X    (font_adv() + 2)
#define BANNER_W (font_adv() + 6)
#define ARROW_W  (font_adv())
#define IGUT     (ITEM_H)         /* per-row icon gutter (a square cell)     */

/* A menu entry: a leaf that runs `cmd` (or is the real INI `sc`), a
   separator ("-"), or a submenu that points at a child array. */
typedef struct MEntry {
    const char           *label;
    const char           *cmd;     /* built-in verb, or NULL              */
    const struct MEntry  *sub;     /* submenu array, or NULL              */
    int                   subn;
    const CfgShortcut    *sc;      /* the real INI shortcut, or NULL      */
    int                   rec;     /* recent-document index, or -1        */
} MEntry;

/* ---- the built-in submenus ------------------------------------------- */
/* The Documents submenu is assembled at open time now: the recently
   opened files first (the whole point of that menu in Windows 95), a
   rule, then the document applets.  DOCS_BUILTIN is the tail. */
#define DOCS_MAX (RECENT_MAX + 9)
static MEntry g_docs[DOCS_MAX];
static int    g_docsn;

static const MEntry DOCS_BUILTIN[] = {
    { "Scrap Box",     "scrap",    0, 0, 0 },
    { "Cardfile",      "cardfile", 0, 0, 0 },
    { "Agenda",        "agenda",   0, 0, 0 },
    { "Calendar",      "calendar", 0, 0, 0 },
    { "Hex Peek",      "peek",     0, 0, 0 }
};
#define DOCS_BUILTIN_N ((int)(sizeof(DOCS_BUILTIN)/sizeof(DOCS_BUILTIN[0])))

static const MEntry SUB_SET[] = {
    { "Settings",      "settings", 0, 0, 0 },
    { "Colors",        "colors",   0, 0, 0 },
    { "Character Map", "charmap",  0, 0, 0 }
};
static const MEntry SUB_MEDIA[] = {
    { "Gramophone",    "gram",     0, 0, 0 },
    { "Cinema",        "cinema",   0, 0, 0 }
};
/* The always-present shell entries.  The user's own pinned [shortcut] items
   sit above these under Programs; kept short so the default list is a single
   tidy column (the Program Drawer window lists every shortcut when there are
   more than a screen can hold). */
static const MEntry PROG_BUILTIN[] = {
    { "Program Drawer","drawer",      0, 0, 0 },
    { "Command Room",  "COMMAND.COM", 0, 0, 0 }
};

/* Programs submenu, assembled at open time: the INI shortcuts, a rule, then
   the built-in apps. */
#define PROG_MAX (CFG_MAX_SHORTCUTS + 8)
static MEntry g_prog[PROG_MAX];
static int    g_progn;

/* The top level, also assembled at open time (so Programs' count is live). */
static MEntry g_top[12];
static int    g_topn;

/* ---- state ----------------------------------------------------------- */
static bool_t g_open = FALSE;
static int    g_x, g_y, g_w, g_h;         /* top menu box                  */
static int    g_thover = -1;              /* hovered top row               */
static int    g_subidx = -1;              /* expanded top row, or -1       */
static int    g_sx, g_sy, g_sw, g_sh;     /* submenu box                   */
static int    g_shover = -1;              /* hovered submenu row           */
static int    g_srows, g_sncols, g_scolw; /* submenu wraps into columns    */

static Rect   g_click_rect;
static bool_t g_click_rect_ok = FALSE;
static CfgShortcut g_result;              /* returned for built-in leaves  */
/* TRUE when the leaf just returned was a recent DOCUMENT rather than a
   verb or a program - the shell routes those to open_document. */
static bool_t g_result_doc = FALSE;

bool_t menu_result_is_document(void) { return g_result_doc; }

/* Case-blind equality (streqi lives in main.c and is static there). */
static bool_t label_eq(const char *a, const char *b)
{
    while (*a != '\0' && *b != '\0') {
        char ca = *a, cb = *b;
        if (ca >= 'a' && ca <= 'z') ca = (char)(ca - 32);
        if (cb >= 'a' && cb <= 'z') cb = (char)(cb - 32);
        if (ca != cb) return FALSE;
        ++a; ++b;
    }
    return (*a == *b) ? TRUE : FALSE;
}

static bool_t is_sep(const MEntry *e) { return (e->label[0] == '-' && e->label[1] == '\0'); }

/* Width of a menu column that must hold every label in `arr`. */
static int col_width(const MEntry *arr, int n, bool_t arrows)
{
    int i, maxw = font_text_width("Shut Down...");
    for (i = 0; i < n; ++i) {
        int w = font_text_width(arr[i].label);
        if (arr[i].sub != 0) w += ARROW_W;
        if (w > maxw) maxw = w;
    }
    (void)arrows;
    return maxw + IGUT + PAD_X;         /* icon gutter + label + right pad    */
}

void menu_open(const CfgShortcut *items, int count,
               int anchor_x, int anchor_bottom)
{
    int i, colw;

    /* Build the Programs submenu: pinned shortcuts, a rule, the built-ins. */
    g_progn = 0;
    for (i = 0; i < count && g_progn < PROG_MAX - 7; ++i) {
        g_prog[g_progn].label = items[i].name;
        g_prog[g_progn].cmd   = 0;
        g_prog[g_progn].sub   = 0;
        g_prog[g_progn].subn  = 0;
        g_prog[g_progn].sc    = &items[i];
        g_prog[g_progn].rec   = -1;
        ++g_progn;
    }
    if (count > 0) {                       /* a rule between yours and ours  */
        g_prog[g_progn].label = "-"; g_prog[g_progn].cmd = 0;
        g_prog[g_progn].sub = 0; g_prog[g_progn].subn = 0; g_prog[g_progn].sc = 0;
        g_prog[g_progn].rec = -1;
        ++g_progn;
    }
    /* Skip a built-in the user has already pinned.  The shipped INI pins
       "Command Room", so it appeared TWICE under Programs - once above the
       rule and once below it. */
    for (i = 0; i < (int)(sizeof(PROG_BUILTIN)/sizeof(PROG_BUILTIN[0])); ++i) {
        int j, dup = 0;
        for (j = 0; j < count; ++j)
            /* label OR command: pinning COMMAND.COM as "DOS Prompt"
               brought the duplicate straight back. */
            if (label_eq(items[j].name, PROG_BUILTIN[i].label) ||
                (PROG_BUILTIN[i].cmd != 0 &&
                 label_eq(items[j].command, PROG_BUILTIN[i].cmd)))
                { dup = 1; break; }
        if (!dup)
            g_prog[g_progn++] = PROG_BUILTIN[i];
    }
    if (g_progn > 0 && is_sep(&g_prog[g_progn - 1]))
        --g_progn;                     /* no rule with nothing under it     */

    /* Build the Documents submenu: what you last opened, then the apps. */
    {
        int rn = recent_count(), k;
        g_docsn = 0;
        for (k = 0; k < rn && g_docsn < DOCS_MAX - DOCS_BUILTIN_N - 1; ++k) {
            g_docs[g_docsn].label = recent_name(k);
            g_docs[g_docsn].cmd   = 0;
            g_docs[g_docsn].sub   = 0;
            g_docs[g_docsn].subn  = 0;
            g_docs[g_docsn].sc    = 0;
            g_docs[g_docsn].rec   = k;
            ++g_docsn;
        }
        if (rn > 0) {
            /* Windows put "Clear the list" at the foot of this menu, and
               a list of files you opened is exactly the sort of thing a
               user wants to be able to forget. */
            g_docs[g_docsn].label = "Clear the list";
            g_docs[g_docsn].cmd   = "recentclear";
            g_docs[g_docsn].sub = 0; g_docs[g_docsn].subn = 0;
            g_docs[g_docsn].sc = 0; g_docs[g_docsn].rec = -1;
            ++g_docsn;
            g_docs[g_docsn].label = "-"; g_docs[g_docsn].cmd = 0;
            g_docs[g_docsn].sub = 0; g_docs[g_docsn].subn = 0;
            g_docs[g_docsn].sc = 0; g_docs[g_docsn].rec = -1;
            ++g_docsn;
        }
        for (k = 0; k < DOCS_BUILTIN_N; ++k)
            g_docs[g_docsn++] = DOCS_BUILTIN[k];
    }

    /* Build the top level. */
    {
        static const MEntry SEP = { "-", 0, 0, 0, 0 };
        MEntry programs; programs.label = "Programs"; programs.cmd = 0;
        programs.sub = g_prog; programs.subn = g_progn; programs.sc = 0;
        programs.rec = -1;
        g_topn = 0;
        g_top[g_topn++] = programs;
        g_top[g_topn].label="Documents"; g_top[g_topn].cmd=0; g_top[g_topn].sub=g_docs;    g_top[g_topn].subn=g_docsn; g_top[g_topn].sc=0; g_top[g_topn].rec=-1; ++g_topn;
        g_top[g_topn].label="Settings";  g_top[g_topn].cmd=0; g_top[g_topn].sub=SUB_SET;   g_top[g_topn].subn=3; g_top[g_topn].sc=0; g_top[g_topn].rec=-1; ++g_topn;
        g_top[g_topn].label="Media";     g_top[g_topn].cmd=0; g_top[g_topn].sub=SUB_MEDIA; g_top[g_topn].subn=2; g_top[g_topn].sc=0; g_top[g_topn].rec=-1; ++g_topn;
        g_top[g_topn++] = SEP;
        g_top[g_topn].label="Help";      g_top[g_topn].cmd="help"; g_top[g_topn].sub=0; g_top[g_topn].subn=0; g_top[g_topn].sc=0; g_top[g_topn].rec=-1; ++g_topn;
        g_top[g_topn].label="Run...";    g_top[g_topn].cmd="run";  g_top[g_topn].sub=0; g_top[g_topn].subn=0; g_top[g_topn].sc=0; g_top[g_topn].rec=-1; ++g_topn;
        g_top[g_topn++] = SEP;
        g_top[g_topn].label="Shut Down..."; g_top[g_topn].cmd="exit"; g_top[g_topn].sub=0; g_top[g_topn].subn=0; g_top[g_topn].sc=0; g_top[g_topn].rec=-1; ++g_topn;
    }

    g_thover = -1; g_subidx = -1; g_shover = -1;

    colw = col_width(g_top, g_topn, TRUE);
    g_w = BANNER_W + colw;
    g_h = g_topn * ITEM_H + 3;
    g_x = anchor_x;
    g_y = anchor_bottom - g_h;
    if (g_x < 0) g_x = 0;
    if (g_y < 0) g_y = 0;
    if (g_x + g_w > SCREEN_W) g_x = SCREEN_W - g_w;
    if (g_y + g_h > SCREEN_H) g_y = SCREEN_H - g_h;
    g_open = TRUE;
}

void menu_close(void)     { g_open = FALSE; g_subidx = -1; }
bool_t menu_is_open(void) { return g_open; }

/* ---- geometry -------------------------------------------------------- */
static void top_row_rect(int i, Rect *r)
{
    rect_set(r, g_x + BANNER_W, g_y + 2 + i * ITEM_H,
             g_w - BANNER_W - 2, ITEM_H);
}
static int top_at(int x, int y)
{
    int i;
    if (x < g_x + BANNER_W || x >= g_x + g_w) return -1;
    i = (y - (g_y + 2)) / ITEM_H;
    if (i < 0 || i >= g_topn) return -1;
    if (y < g_y + 2) return -1;
    return is_sep(&g_top[i]) ? -1 : i;
}

/* Lay out the submenu box for expanded top row g_subidx.  A long list (more
   than one screen tall) wraps into balanced columns, exactly the way the real
   Windows-95 Programs menu spills sideways instead of running off-screen. */
static void layout_sub(void)
{
    int n, rows_avail;
    if (g_subidx < 0) return;
    n = g_top[g_subidx].subn;
    g_scolw = col_width(g_top[g_subidx].sub, n, FALSE);

    /* The WORK AREA, not the screen.  Using SCREEN_H let an 11-row
       Programs submenu run to the bottom edge and paint over the clock
       and the task buttons instead of wrapping into the second column
       this very function goes on to compute. */
    rows_avail = (TASKBAR_Y - 6) / ITEM_H;      /* rows that fit above bar   */
    if (rows_avail < 1) rows_avail = 1;
    g_sncols = (n + rows_avail - 1) / rows_avail;
    if (g_sncols < 1) g_sncols = 1;
    g_srows  = (n + g_sncols - 1) / g_sncols;   /* balance the columns       */

    g_sw = g_sncols * g_scolw;
    g_sh = g_srows * ITEM_H + 3;
    g_sx = g_x + g_w - 1;                        /* prefer: right of the top  */
    g_sy = g_y + 2 + g_subidx * ITEM_H - 2;
    if (g_sx + g_sw > SCREEN_W) {               /* does not fit to the right */
        int left = g_x - g_sw + 1;              /* try the left side...      */
        g_sx = (left >= 0) ? left : SCREEN_W - g_sw;  /* ...else hug the edge */
    }
    if (g_sx < 0) g_sx = 0;
    if (g_sy + g_sh > TASKBAR_Y) g_sy = TASKBAR_Y - g_sh;
    if (g_sy < 0) g_sy = 0;
}
static void sub_row_rect(int i, Rect *r)
{
    int col = i / g_srows, row = i % g_srows;
    rect_set(r, g_sx + col * g_scolw, g_sy + 2 + row * ITEM_H,
             g_scolw - 2, ITEM_H);
}
static int sub_at(int x, int y)
{
    int col, row, i;
    if (g_subidx < 0) return -1;
    if (x < g_sx || x >= g_sx + g_sw) return -1;
    if (y < g_sy + 2) return -1;
    col = (x - g_sx) / g_scolw;
    row = (y - (g_sy + 2)) / ITEM_H;
    if (col < 0 || col >= g_sncols) return -1;
    if (row < 0 || row >= g_srows) return -1;
    i = col * g_srows + row;
    if (i < 0 || i >= g_top[g_subidx].subn) return -1;
    return is_sep(&g_top[g_subidx].sub[i]) ? -1 : i;
}

void menu_bounds(Rect *r)
{
    int s = 3 * ui_scale();
    int x0, y0, x1, y1;
    if (!g_open) { rect_set(r, 0, 0, 0, 0); return; }
    x0 = g_x; y0 = g_y; x1 = g_x + g_w + s; y1 = g_y + g_h + s;
    if (g_subidx >= 0) {
        if (g_sx < x0) x0 = g_sx;
        if (g_sy < y0) y0 = g_sy;
        if (g_sx + g_sw + s > x1) x1 = g_sx + g_sw + s;
        if (g_sy + g_sh + s > y1) y1 = g_sy + g_sh + s;
    }
    rect_set(r, x0, y0, x1 - x0, y1 - y0);
}

/* ---- drawing --------------------------------------------------------- */
static void draw_arrow(int x, int y, u8 col)   /* a small right triangle    */
{
    int i, h = font_h() - 2;
    for (i = 0; i < h; ++i) {
        int len = (i <= h / 2) ? i + 1 : h - i;
        if (len > 0) vid_hline(x, y + i, len, col);
    }
}

/* ---- small Start-menu icons (drawn in an sz x sz cell) --------------- */
#define MI_PROG   0
#define MI_DOCS   1
#define MI_SET    2
#define MI_MEDIA  3
#define MI_HELP   4
#define MI_RUN    5
#define MI_SHUT   6
#define MI_DRAWER 7
#define MI_FIND   8
#define MI_APP    9

static void mi_win(int x, int y, int sz, u8 title)     /* a tiny window    */
{
    vid_fillrect(x, y, sz, sz, C_WHITE);
    vid_rect    (x, y, sz, sz, C_DKGRAY);
    vid_fillrect(x + 1, y + 1, sz - 2, sz / 3, title);
}

static void menu_icon(int kind, int x, int y, int sz)
{
    int fh = font_h(), fa = font_adv();
    switch (kind) {
    case MI_PROG:  mi_win(x, y, sz, C_TITLE); break;
    case MI_APP:   mi_win(x, y, sz, C_BLUE);  break;
    case MI_DOCS: {
        int i;
        vid_fillrect(x + 1, y, sz - 2, sz, C_WHITE);
        vid_rect    (x + 1, y, sz - 2, sz, C_DKGRAY);
        for (i = 0; i < 3; ++i)
            vid_hline(x + 3, y + 3 + i * 3, sz - 6, C_SHADOW);
        break;
    }
    case MI_SET:
        vid_fillrect(x, y, sz, sz, C_FACE);
        ui_raise    (x, y, sz, sz);
        vid_hline   (x + 2, y + sz / 3, sz - 4, C_SHADOW);
        vid_fillrect(x + 3, y + sz / 3 - 1, 2, 3, C_DKGRAY);
        vid_hline   (x + 2, y + 2 * sz / 3, sz - 4, C_SHADOW);
        vid_fillrect(x + sz - 5, y + 2 * sz / 3 - 1, 2, 3, C_DKGRAY);
        break;
    case MI_MEDIA:
        vid_fillrect(x + 1, y + sz - 4, 3, 3, C_BLUE);     /* note head      */
        vid_fillrect(x + 3, y + 1, 2, sz - 4, C_BLACK);    /* stem           */
        vid_fillrect(x + 3, y + 1, sz / 2, 2, C_BLACK);    /* flag           */
        break;
    case MI_HELP:
        vid_fillrect(x, y, sz, sz, C_WHITE);
        vid_rect    (x, y, sz, sz, C_DKGRAY);
        font_draw_char(x + (sz - fa) / 2 + 1, y + (sz - fh) / 2, '?', C_BLUE);
        break;
    case MI_RUN:
        vid_fillrect(x, y, sz, sz, C_FACE);
        ui_raise    (x, y, sz, sz);
        vid_fillrect(x + 2, y + 2, sz - 4, sz - 4, C_BLACK);
        font_draw_char(x + 2, y + 1, '>', C_GREEN);
        break;
    case MI_SHUT:                                          /* a power symbol */
        vid_hline   (x + 2, y + sz - 2, sz - 4, C_RED);
        vid_vline   (x + 2, y + 4, sz - 6, C_RED);
        vid_vline   (x + sz - 3, y + 4, sz - 6, C_RED);
        vid_hline   (x + 2, y + 4, 2, C_RED);
        vid_hline   (x + sz - 4, y + 4, 2, C_RED);
        vid_fillrect(x + sz / 2 - 1, y + 1, 2, sz / 2, C_RED);
        break;
    case MI_DRAWER:
        vid_fillrect(x, y + 1, sz, sz - 2, C_DKYELLOW);
        ui_raise    (x, y + 1, sz, sz - 2);
        vid_hline   (x + 1, y + sz / 2, sz - 2, C_SHADOW);
        vid_fillrect(x + sz / 2 - 1, y + 2, 3, 2, C_DKGRAY);
        vid_fillrect(x + sz / 2 - 1, y + sz / 2 + 2, 3, 2, C_DKGRAY);
        break;
    case MI_FIND:                                    /* a magnifier      */
        vid_fillrect(x + 1, y, sz - 4, sz - 4, C_WHITE);
        vid_rect    (x + 1, y, sz - 4, sz - 4, C_BLACK);
        vid_pixel   (x + 3, y + 2, C_LTBLUE);
        vid_fillrect(x + sz - 4, y + sz - 5, 2, 2, C_BLACK);
        vid_fillrect(x + sz - 3, y + sz - 3, 2, 2, C_BLACK);
        break;
    default:       mi_win(x, y, sz, C_BLUE);  break;
    }
}

/* Case-insensitive "does hay contain needle" (needle lower-case). */
static bool_t mhas(const char *hay, const char *needle)
{
    int i, j;
    if (hay == 0) return FALSE;
    for (i = 0; hay[i]; ++i) {
        for (j = 0; needle[j]; ++j) {
            char a = hay[i + j];
            if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
            if (a != needle[j]) break;
        }
        if (needle[j] == '\0') return TRUE;
    }
    return FALSE;
}

/* Pick a mini-icon for a menu entry from its role or command. */
static int menu_icon_for(const MEntry *e)
{
    const char *c;
    if (e->sub != 0) {                     /* a cascade header               */
        if (mhas(e->label, "document")) return MI_DOCS;
        if (mhas(e->label, "setting"))  return MI_SET;
        if (mhas(e->label, "media"))    return MI_MEDIA;
        return MI_PROG;
    }
    c = e->cmd ? e->cmd : (e->sc ? e->sc->command : "");
    if (mhas(c, "exit"))    return MI_SHUT;
    if (mhas(c, "help"))    return MI_HELP;
    if (mhas(c, "run"))     return MI_RUN;
    if (mhas(c, "find"))    return MI_FIND;
    if (mhas(c, "drawer"))  return MI_DRAWER;
    if (mhas(c, "command") || mhas(c, "prompt"))                 return MI_RUN;
    if (mhas(c, "setting")|| mhas(c, "color") || mhas(c, "char"))return MI_SET;
    if (mhas(c, "scrap")  || mhas(c, "card")  || mhas(c, "agenda") ||
        mhas(c, "peek")   || mhas(c, "note")  || mhas(c, "calendar"))
                                                                 return MI_DOCS;
    if (mhas(c, "gram")   || mhas(c, "media") || mhas(c, "cinema") ||
        mhas(c, "demo")   || mhas(c, "light"))                   return MI_MEDIA;
    return MI_APP;
}

static void draw_column(int bx, int by, int bw, const MEntry *arr, int n,
                        int hover)
{
    int i, sz = font_h(), goff = (ITEM_H - font_h()) / 2;
    for (i = 0; i < n; ++i) {
        int ry = by + i * ITEM_H, ty = ry + goff;
        u8  fg;
        if (is_sep(&arr[i])) {                 /* an etched separator line   */
            vid_hline(bx + 2, ry + ITEM_H / 2, bw - 4, C_SHADOW);
            vid_hline(bx + 2, ry + ITEM_H / 2 + 1, bw - 4, C_HILIGHT);
            continue;
        }
        if (i == hover) {
            vid_fillrect(bx, ry, bw, ITEM_H, C_TITLE);
            fg = C_WHITE;
        } else {
            fg = C_BLACK;
        }
        menu_icon(menu_icon_for(&arr[i]), bx + 2, ry + goff, sz);
        font_draw(bx + IGUT, ty, arr[i].label, fg);
        if (arr[i].sub != 0)
            draw_arrow(bx + bw - ARROW_W, ty + 1, fg);
    }
}

void menu_draw(void)
{
    if (!g_open) return;

    ui_shadow(g_x, g_y, g_w, g_h);
    ui_fill_face(g_x, g_y, g_w, g_h);
    ui_raise(g_x, g_y, g_w, g_h);

    /* The vertical CASTALIA brand stripe down the left edge: a Windows-95
       gradient banner with the name set in raised, embossed capitals. */
    {
        int bx = g_x + 2, by = g_y + 2, bh = g_h - 4, ly;
        const char *nm = "CASTALIA92";
        vid_title_bar_v(bx, by, BANNER_W - 1, bh, TRUE);   /* navy->sky sweep */
        vid_vline(bx, by, bh, C_TITLE);                    /* seat left edge  */
        vid_vline(bx + BANNER_W - 1, by, bh, C_HILIGHT);   /* bright inner rim*/
        ly = by + 3;
        while (*nm != '\0' && ly + font_h() < by + bh - 2) {
            font_draw_char(bx + 4, ly + 1, *nm, C_BLACK);  /* drop shadow     */
            font_draw_char(bx + 3, ly,     *nm, C_WHITE);  /* bright face     */
            ly += font_h() + 1;
            ++nm;
        }
    }

    draw_column(g_x + BANNER_W, g_y + 2, g_w - BANNER_W, g_top, g_topn, g_thover);

    if (g_subidx >= 0) {
        const MEntry *sarr = g_top[g_subidx].sub;
        int sn = g_top[g_subidx].subn, c;
        layout_sub();
        ui_shadow(g_sx, g_sy, g_sw, g_sh);
        ui_fill_face(g_sx, g_sy, g_sw, g_sh);
        ui_raise(g_sx, g_sy, g_sw, g_sh);
        for (c = 0; c < g_sncols; ++c) {
            int base = c * g_srows;
            int cnt  = sn - base;
            int lh   = (g_shover >= base && g_shover < base + g_srows)
                       ? g_shover - base : -1;
            if (cnt > g_srows) cnt = g_srows;
            if (cnt <= 0) break;
            if (c > 0)                        /* etch a divider between cols */
                vid_vline(g_sx + c * g_scolw, g_sy + 2, g_sh - 4, C_SHADOW);
            draw_column(g_sx + c * g_scolw, g_sy + 2, g_scolw, sarr + base,
                        cnt, lh);
        }
    }
}

/* ---- interaction ----------------------------------------------------- */
bool_t menu_hover(int x, int y)
{
    int t, s, changed = 0;
    if (!g_open) return FALSE;

    s = sub_at(x, y);                     /* submenu first (it is on top)    */
    if (g_subidx >= 0 && s != g_shover) { g_shover = s; changed = 1; }

    t = top_at(x, y);
    if (t != g_thover) {
        g_thover = t;
        changed = 1;
        if (t >= 0 && g_top[t].sub != 0) {     /* expand a submenu            */
            if (g_subidx != t) { g_subidx = t; g_shover = -1; layout_sub(); }
        } else if (t >= 0 && s < 0) {          /* a leaf on the top: collapse */
            g_subidx = -1;
        }
    }
    return changed ? TRUE : FALSE;
}

/* Fill g_result from a leaf entry and return it. */
static const CfgShortcut *pick(const MEntry *e, const Rect *rr)
{
    if (rr != 0) { g_click_rect = *rr; g_click_rect_ok = TRUE; }
    menu_close();
    g_result_doc = FALSE;
    if (e->sc != 0)
        return e->sc;                     /* the real INI shortcut           */
    if (e->rec >= 0) {                    /* a recently opened document      */
        recent_fill(e->rec, &g_result);
        g_result_doc = TRUE;
        return &g_result;
    }
    {   int i = 0;
        while (e->cmd[i] && i < (int)sizeof(g_result.command) - 1) {
            g_result.command[i] = e->cmd[i]; ++i;
        }
        g_result.command[i] = '\0';
    }
    g_result.name[0] = '\0';
    g_result.path[0] = '\0';
    g_result.icon[0] = '\0';
    g_result.freemem = FALSE;
    return &g_result;
}

const CfgShortcut *menu_click(int x, int y)
{
    int t, s;
    if (!g_open) return NULL;

    s = sub_at(x, y);
    if (s >= 0) {                          /* a submenu leaf: launch it       */
        Rect r; sub_row_rect(s, &r);
        return pick(&g_top[g_subidx].sub[s], &r);
    }
    t = top_at(x, y);
    if (t >= 0) {
        if (g_top[t].sub != 0) {           /* a header: expand, keep open     */
            g_subidx = t; g_shover = -1; layout_sub();
            return NULL;
        }
        {   Rect r; top_row_rect(t, &r);
            return pick(&g_top[t], &r); }  /* a top leaf: launch              */
    }
    menu_close();                          /* clicked outside: dismiss        */
    return NULL;
}

/* ---- keyboard navigation ----------------------------------------------
 * The Dominus menu used to be pure mouse: nothing opened it from the
 * keyboard, and main.c swallowed every key while it was up.  Arrows walk
 * it, Right/Enter opens a submenu, Left/Esc backs out, Enter launches.
 * Selection reuses the same g_thover/g_shover the mouse drives, so the
 * highlight and the drawing code need no changes at all.
 * -------------------------------------------------------------------- */

/* Step `dir` (+1/-1) through a list, skipping separators, and clamp. */
static int step_row(const MEntry *list, int n, int cur, int dir)
{
    int i = cur;
    for (;;) {
        i += dir;
        if (i < 0 || i >= n)
            return cur;                    /* stay put at the ends          */
        if (!is_sep(&list[i]))
            return i;
    }
}

bool_t menu_key(int key, const CfgShortcut **out)
{
    *out = NULL;
    if (!g_open)
        return FALSE;

    if (key == KEY_ESC) {
        if (g_subidx >= 0) { g_subidx = -1; g_shover = -1; }  /* back out   */
        else               menu_close();
        return TRUE;
    }

    if (g_subidx >= 0) {                   /* inside an expanded submenu    */
        const MEntry *list = g_top[g_subidx].sub;
        int n = g_top[g_subidx].subn;
        if (key == KEY_DOWN || key == KEY_UP) {
            int start = (g_shover < 0)
                      ? (key == KEY_DOWN ? -1 : n)
                      : g_shover;
            g_shover = step_row(list, n, start, (key == KEY_DOWN) ? 1 : -1);
            if (g_shover < 0 || g_shover >= n) g_shover = 0;
            return TRUE;
        }
        if (key == KEY_LEFT) { g_subidx = -1; g_shover = -1; return TRUE; }
        if (key == KEY_ENTER || key == KEY_RIGHT) {
            if (g_shover >= 0 && g_shover < n && !is_sep(&list[g_shover])) {
                Rect r; sub_row_rect(g_shover, &r);
                *out = pick(&list[g_shover], &r);
                return TRUE;
            }
            return FALSE;
        }
        return FALSE;
    }

    if (key == KEY_DOWN || key == KEY_UP) {
        int start = (g_thover < 0)
                  ? (key == KEY_DOWN ? -1 : g_topn)
                  : g_thover;
        g_thover = step_row(g_top, g_topn, start, (key == KEY_DOWN) ? 1 : -1);
        if (g_thover < 0 || g_thover >= g_topn) g_thover = 0;
        return TRUE;
    }
    if (key == KEY_RIGHT || key == KEY_ENTER) {
        if (g_thover < 0 || g_thover >= g_topn)
            return FALSE;
        if (g_top[g_thover].sub != 0) {    /* a header: open it             */
            g_subidx = g_thover; g_shover = 0; layout_sub();
            if (g_top[g_subidx].subn > 0 && is_sep(&g_top[g_subidx].sub[0]))
                g_shover = step_row(g_top[g_subidx].sub,
                                    g_top[g_subidx].subn, 0, 1);
            return TRUE;
        }
        if (key == KEY_ENTER) {
            Rect r; top_row_rect(g_thover, &r);
            *out = pick(&g_top[g_thover], &r);
            return TRUE;
        }
    }
    return FALSE;
}

bool_t menu_take_click_rect(Rect *r)
{
    if (!g_click_rect_ok) return FALSE;
    *r = g_click_rect;
    g_click_rect_ok = FALSE;
    return TRUE;
}
