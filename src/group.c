/* ======================================================================
 * group.c - Program-group launcher windows for CASTALIA/386
 * ----------------------------------------------------------------------
 * A windowed icon grid, like the Program Drawer, but its entries are a
 * fixed list of built-in verbs rather than the INI's [shortcut] list.
 * Two groups are defined: a Toolbox of desk utilities and an Arcade of
 * games.  Selecting one reports the verb up to main.c, which opens it.
 * ====================================================================== */
#include <string.h>
#include "group.h"
#include "video.h"
#include "ui.h"
#include "font.h"
#include "keyboard.h"

typedef struct { const char *name; const char *command; } GItem;

/* Toolbox - desk utilities.  Grows as new tools are added. */
static const GItem g_tools[] = {
    { "Scrap Box",     "scrap"    },
    { "Cardfile",      "cardfile" },
    { "Calc",          "calc"     },
    { "Clock",         "clock"    },
    { "Calendar",      "calendar" },
    { "Sketch Pad",    "paint"    },
    { "Character Map", "charmap"  },
    { "Colors",        "colors"   },
    { "Fractal",       "fractal"  },
    { "Stopwatch",     "stopwatch"},
    { "Eyes",          "eyes"     },
    { "Settings",      "settings" },
    { "Oracle",        "oracle"   },
    { "Agenda",        "agenda"   },
    { "Find File",     "find"     },
    { "Hex Peek",      "peek"     },
    { "Typing Tutor",  "typist"   },
    { "Light Show",    "lightshow"},
    { "Gramophone",    "gram"     },
    { "Music Box",     "music"    },
    { "Picture Show",  "pictures" },
    { "Cinema",        "cinema"   }
};
/* Arcade - games.  Grows as new games are added. */
static const GItem g_arcade[] = {
    { "Fifteen Puzzle", "puzzle"  },
    { "Tic Tac Toe",    "ttt"     },
    { "Minefield",      "mines"   },
    { "Reversi",        "reversi" },
    { "Serpent",        "snake"   },
    { "Breaker",        "breaker" },
    { "Echo",           "echo"    },
    { "Quadrix",        "quadrix" },
    { "Depot",          "depot"   },
    { "Patience",       "patience"},
    { "Lights Out",     "lights"  },
    { "Pong",           "pong"    },
    { "2048",           "2048"    },
    { "Corral",         "corral"  }
};

#define TOOLS_N  (int)(sizeof(g_tools)  / sizeof(g_tools[0]))
#define ARCADE_N (int)(sizeof(g_arcade) / sizeof(g_arcade[0]))

/* Same cell metrics as the desktop / drawer grids, so it scales per mode. */
#define DCELL_W   (ICON_SIZE + 24)
/* Same correction desktop.c already carries: a cell must clear its
   own content - icon, gap, two label lines and the 1px shadow.  The
   old -2 was 6px short, so every wrapping name in the Toolbox, the
   Arcade and the Program Drawer printed its second line over the
   next row's icon. */
#define DCELL_H   (ICON_SIZE + font_h() * 2 + 6)
#define GCOLS_MAX 5
/* The Mode-13h base height of one cell, and it must equal DCELL_H there
   (32 + 8*2 + 6).  It was 46 while the layout used 54, so the window was
   sized for more rows than it could actually draw: a scrollbar appeared
   at runtime that group_window_size() had reserved no width for, and the
   last column's captions were drawn UNDER it - the Arcade really did
   read "Serpen" and "Patienc". */
#define GCELL_BASE_H 54
#define GSCROLL_W    14        /* the scroll strip, always reserved */

static int  g_which   = GRP_TOOLS;
static int  g_sel     = -1;
static int  g_grtop   = 0;             /* first visible row (scrolling)    */
static int  g_lastfit = 3;             /* rows the last draw could show    */
static char g_cmd[32];

static const GItem *items(int which, int *n)
{
    if (which == GRP_ARCADE) { *n = ARCADE_N; return g_arcade; }
    *n = TOOLS_N; return g_tools;
}

static int grid_cols(int count)
{
    int c = (count < GCOLS_MAX) ? count : GCOLS_MAX;
    return (c < 1) ? 1 : c;
}

/* Rows that fit the work area, in the Mode-13h base units the window
   size below is quoted in (open_centered scales for Mode 12h).  Without
   this clamp a full Toolbox opened taller than the screen and its lower
   rows sat unreachable under the taskbar, eating the Start button's
   clicks. */
static int rows_max(void)
{
    int sc = ui_scale();
    int base, r;
    if (sc < 1) sc = 1;
    base = SCREEN_H / sc - 14;         /* minus the taskbar                */
    r = (base - 26) / GCELL_BASE_H;
    return (r < 1) ? 1 : r;
}

/* Rows of cells the CURRENT client can show (runtime units). */
static int rows_fit(const Rect *cl)
{
    int r = (cl->h - 8) / DCELL_H;
    return (r < 1) ? 1 : r;
}

static int rows_total(void)
{
    int n, cols;
    (void)items(g_which, &n);
    cols = grid_cols(n);
    return (n + cols - 1) / cols;
}

void group_open(int which)
{
    g_which = which;
    g_sel   = -1;
    g_grtop = 0;
}

const char *group_title(int which)
{
    return (which == GRP_ARCADE) ? "Arcade" : "Toolbox";
}

void group_window_size(int which, int *w, int *h)
{
    int n, cols, rows, maxr = rows_max();
    (void)items(which, &n);
    cols = grid_cols(n);
    rows = (n + cols - 1) / cols;
    if (rows < 1) rows = 1;
    /* ALWAYS reserve the scroll strip, as drawer.c already did.  Adding it
       only when the size calculation predicted scrolling meant that
       whenever the prediction was wrong - which it always was, 46 against
       54 - the bar landed on top of the last column.  Reserving it
       unconditionally also keeps the grid in the same place whether the
       bar is showing or not. */
    *w = cols * 56 + 12 + GSCROLL_W;      /* Mode-13h base; scaled for 12h  */
    if (rows > maxr)
        rows = maxr;                      /* clamp; the rest scrolls        */
    *h = rows * GCELL_BASE_H + 26;
}

static void cell_xy(const Rect *cl, int i, int cols, int *cx, int *cy)
{
    int col = i % cols, row = i / cols - g_grtop;
    *cx = cl->x + 4 + col * DCELL_W;
    *cy = cl->y + 4 + row * DCELL_H;
}

/* The scrollbar (drawn only when the grid overflows the window). */
static void arrow_rects(const Rect *cl, Rect *up, Rect *dn)
{
    int s  = ui_scale();
    int bw = 12 * s;
    rect_set(up, cl->x + cl->w - bw - 2, cl->y + 2, bw, bw);
    rect_set(dn, cl->x + cl->w - bw - 2, cl->y + cl->h - bw - 2, bw, bw);
}

/* The trough between them - there was none, so the only cue that the grid
   scrolled at all was two tiny carets. */
static void track_rect(const Rect *cl, Rect *tr)
{
    Rect up, dn;
    arrow_rects(cl, &up, &dn);
    rect_set(tr, up.x, up.y + up.h, up.w, dn.y - (up.y + up.h));
    if (tr->h < 0) tr->h = 0;
}

static void clamp_top(const Rect *cl)
{
    int hidden = rows_total() - rows_fit(cl);
    if (hidden < 0) hidden = 0;
    if (g_grtop > hidden) g_grtop = hidden;
    if (g_grtop < 0)      g_grtop = 0;
}

/* Centre a name under a cell, wrapped to at most two lines. */
static void cell_label(int x, int y, int w, const char *s, u8 color)
{
    char a[24], b[24];
    /* -8: at -2 a 9-char caption filled the cell edge to edge and the
       gap between two of them was smaller than the space inside one. */
    ui_wrap2(s, w - 8, w - 2, a, b, (int)sizeof(a));
    ui_text_center(x, y, w, a, color);
    if (b[0] != '\0')
        ui_text_center(x, y + font_h() + 1, w, b, color);
}

void group_draw(const Rect *cl)
{
    int i, n, cols, fit;
    const GItem *it = items(g_which, &n);
    cols = grid_cols(n);
    clamp_top(cl);
    fit = g_lastfit = rows_fit(cl);
    for (i = 0; i < n; ++i) {
        int cx, cy, ix, ly, row = i / cols;
        if (row < g_grtop || row >= g_grtop + fit)
            continue;                  /* scrolled out of the window       */
        cell_xy(cl, i, cols, &cx, &cy);
        ix = cx + (DCELL_W - ICON_SIZE) / 2;
        ly = cy + ICON_SIZE + 2;
        ui_icon(ui_icon_for_command(it[i].command), ix, cy);
        if (i == g_sel) {
            vid_fillrect(cx, ly - 1, DCELL_W, font_h() * 2 + 3, C_TITLE);
            cell_label(cx, ly, DCELL_W, it[i].name, C_WHITE);
            vid_rect(ix - 2, cy - 2, ICON_SIZE + 4, ICON_SIZE + 4, C_WHITE);
        } else {
            cell_label(cx, ly, DCELL_W, it[i].name, C_BLACK);
        }
    }
    if (rows_total() > fit) {          /* a real scrollbar, not two carets */
        Rect up, dn, tr;
        arrow_rects(cl, &up, &dn);
        track_rect(cl, &tr);
        ui_vscroll(&up, &dn, &tr, g_grtop, fit, rows_total());
    }
}

static int hit_index(const Rect *cl, int mx, int my)
{
    int i, n, cols, fit;
    (void)items(g_which, &n);
    cols = grid_cols(n);
    fit  = rows_fit(cl);
    for (i = 0; i < n; ++i) {
        int cx, cy, row = i / cols;
        if (row < g_grtop || row >= g_grtop + fit)
            continue;
        cell_xy(cl, i, cols, &cx, &cy);
        if (mx >= cx && mx < cx + DCELL_W && my >= cy && my < cy + DCELL_H)
            return i;
    }
    return -1;
}

static bool_t arm(int idx)
{
    int n;
    const GItem *it = items(g_which, &n);
    if (idx < 0 || idx >= n)
        return FALSE;
    strncpy(g_cmd, it[idx].command, sizeof(g_cmd) - 1);
    g_cmd[sizeof(g_cmd) - 1] = '\0';
    return TRUE;
}

/* Screen rectangle of the cell the last successful click launched from. */
static Rect   g_launch_rect;
static bool_t g_launch_rect_ok = FALSE;

bool_t group_take_launch_rect(Rect *r)
{
    if (!g_launch_rect_ok)
        return FALSE;
    *r = g_launch_rect;
    g_launch_rect_ok = FALSE;
    return TRUE;
}

bool_t group_click(const Rect *cl, int mx, int my, bool_t dbl)
{
    int idx;
    if (rows_total() > rows_fit(cl)) { /* the arrow strip scrolls a row    */
        Rect up, dn;
        arrow_rects(cl, &up, &dn);
        if (rect_contains(&up, mx, my)) { --g_grtop; clamp_top(cl); return FALSE; }
        if (rect_contains(&dn, mx, my)) { ++g_grtop; clamp_top(cl); return FALSE; }
    }
    idx = hit_index(cl, mx, my);
    if (idx < 0) { g_sel = -1; return FALSE; }
    g_sel = idx;
    if (dbl && arm(idx)) {
        int n, cols, cx, cy;
        (void)items(g_which, &n);
        cols = grid_cols(n);
        cell_xy(cl, idx, cols, &cx, &cy);
        rect_set(&g_launch_rect, cx, cy, DCELL_W, DCELL_H);
        g_launch_rect_ok = TRUE;       /* the applet zooms out of this cell */
        return TRUE;
    }
    return FALSE;
}

bool_t group_key(int key)
{
    int n, cols;
    const GItem *it = items(g_which, &n);
    (void)it;
    if (n == 0) return FALSE;
    cols = grid_cols(n);
    if (key == KEY_ENTER)
        return (g_sel >= 0) ? arm(g_sel) : FALSE;
    if (g_sel < 0)
        g_sel = 0;
    else if (key == KEY_RIGHT && g_sel + 1 < n)     ++g_sel;
    else if (key == KEY_LEFT  && g_sel > 0)         --g_sel;
    else if (key == KEY_DOWN  && g_sel + cols < n)  g_sel += cols;
    else if (key == KEY_UP    && g_sel - cols >= 0) g_sel -= cols;
    else if (key == KEY_PGDN)                       ++g_grtop;
    else if (key == KEY_PGUP)                       --g_grtop;
    /* Keep the selection on a visible row (the draw clamps g_grtop). */
    if (g_sel >= 0) {
        int row = g_sel / cols;
        if (row < g_grtop)              g_grtop = row;
        if (row >= g_grtop + g_lastfit) g_grtop = row - g_lastfit + 1;
    }
    return FALSE;
}

const char *group_launch_command(void) { return g_cmd; }
