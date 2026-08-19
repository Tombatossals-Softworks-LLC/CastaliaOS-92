/* ======================================================================
 * drawer.c - Program Drawer for CASTALIA/386 (v0.7)
 * ----------------------------------------------------------------------
 * A windowed grid of the configured launch entries.  It mirrors the
 * desktop's icon grid (icon.c bitmaps with a procedural fallback, the
 * same cell metrics so it scales between Mode 13h and Mode 12h), but it
 * lives inside a window and reports launches up to main.c rather than
 * spawning anything itself.
 * ====================================================================== */
#include <string.h>
#include <stdio.h>     /* sprintf (scan specs)                   */
#include <dos.h>      /* _dos_allocmem / _dos_findfirst / _dos_findnext */
#include <i86.h>      /* MK_FP */
#include "drawer.h"
#include "video.h"
#include "ui.h"
#include "font.h"
#include "icon.h"
#include "keyboard.h"

/* Cell metrics: identical to the desktop grid, so one asset set and one
   layout serve both video modes (ICON_SIZE and font_h() are per-mode). */
#define DCELL_W   (ICON_SIZE + 24)
/* Same correction desktop.c already carries: a cell must clear its
   own content - icon, gap, two label lines and the 1px shadow.  The
   old -2 was 6px short, so every wrapping name in the Toolbox, the
   Arcade and the Program Drawer printed its second line over the
   next row's icon. */
#define DCELL_H   (ICON_SIZE + font_h() * 2 + 6)
#define DCOLS_MAX 5
/* Mode-13h base height of one cell; must equal DCELL_H there (32+8*2+6).
   It was 46 while the layout stepped 54, so the window was sized for more
   rows than it could draw - with the shipped nine shortcuts the drawer
   claimed two rows, drew one, and left half the client dead grey.  The
   same mismatch clipped the Arcade's last column. */
#define DCELL_BASE_H 54

static const Config *g_cfg = NULL;
static int        g_sel = -1;
static int g_top = 0;    /* first visible row - see rows_fit() below */
static bool_t g_follow = FALSE;  /* scroll to the selection on next draw */
/* The 16 shortcut bitmaps are ~16 KB: far too much for the nearly-full
   DGROUP, and a far static would pad the EXE with 16 KB of zeros - so the
   table is grabbed from DOS the first time the drawer opens.  If that
   fails, the procedural icons keep serving. */
static IconBitmap far *g_bm = (IconBitmap far *)0;
static char       g_cmd[CFG_CMD_LEN];
static char       g_path[CFG_PATH_LEN];
static bool_t     g_freemem = FALSE;

static void scopy(char *d, const char *s, int max)
{
    int i = 0;
    while (s[i] != '\0' && i < max - 1) { d[i] = s[i]; ++i; }
    d[i] = '\0';
}

/* Entries scanned from the [drawer] scan= directory: bare 8.3 names of
   the EXE, COM and BAT files found there, after the INI shortcuts.
   They all share one working directory (the scan dir itself). */
#define DSCAN_MAX 16
static char g_scan[DSCAN_MAX][13];
static int  g_scan_n = 0;

static int entry_total(void)
{
    return ((g_cfg != NULL) ? g_cfg->shortcut_count : 0) + g_scan_n;
}

/* Name / command / icon command of grid entry i (INI first, then scan). */
static const char *entry_name(int i)
{
    int sc = (g_cfg != NULL) ? g_cfg->shortcut_count : 0;
    if (i < sc)
        return g_cfg->shortcuts[i].name;
    return g_scan[i - sc];
}

static const char *entry_command(int i)
{
    int sc = (g_cfg != NULL) ? g_cfg->shortcut_count : 0;
    if (i < sc)
        return g_cfg->shortcuts[i].command;
    return g_scan[i - sc];
}

/* Case-blind name order for the scanned block (FAT order is arbitrary). */
static int scan_cmp(const char *a, const char *b)
{
    while (*a != '\0' && *b != '\0') {
        char ca = *a, cb = *b;
        if (ca >= 'a' && ca <= 'z') ca = (char)(ca - 32);
        if (cb >= 'a' && cb <= 'z') cb = (char)(cb - 32);
        if (ca != cb)
            return (int)(unsigned char)ca - (int)(unsigned char)cb;
        ++a; ++b;
    }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

/* Fill g_scan from the configured directory - three findfirst passes
   (EXE, COM, BAT), capped to the cells the current mode can SHOW, so a
   fat directory never buries the grid off-screen. */
static void scan_dir_entries(void)
{
    static const char * const PATS[3] = { "*.EXE", "*.COM", "*.BAT" };
    struct find_t ff;
    unsigned rc;
    char spec[CFG_PATH_LEN + 8];
    int pass, i, j, cap, dl;

    g_scan_n = 0;
    if (g_cfg == NULL || g_cfg->drawer_scan[0] == '\0')
        return;

    /* The drawer scrolls now, so the scan no longer has to fit one
       screenful.  The old cap was rows*cols - shortcut_count, which with
       the shipped INI's ten shortcuts admitted only five scanned
       programs, and with sixteen shortcuts went <= 0 and ignored
       [drawer] scan= entirely. */
    cap = DSCAN_MAX;

    dl = (int)strlen(g_cfg->drawer_scan);
    for (pass = 0; pass < 3; ++pass) {
        sprintf(spec, "%s%s%s", g_cfg->drawer_scan,
                (dl > 0 && g_cfg->drawer_scan[dl - 1] == '\\') ? "" : "\\",
                PATS[pass]);
        rc = _dos_findfirst(spec, _A_RDONLY | _A_ARCH, &ff);
        while (rc == 0 && g_scan_n < cap) {
            scopy(g_scan[g_scan_n], ff.name, 13);
            ++g_scan_n;
            rc = _dos_findnext(&ff);
        }
    }
    for (i = 1; i < g_scan_n; ++i) {          /* insertion sort by name  */
        char tmp[13];
        scopy(tmp, g_scan[i], 13);
        j = i - 1;
        while (j >= 0 && scan_cmp(g_scan[j], tmp) > 0) {
            scopy(g_scan[j + 1], g_scan[j], 13);
            --j;
        }
        scopy(g_scan[j + 1], tmp, 13);
    }
}

static int grid_cols(int count)
{
    int c = (count < DCOLS_MAX) ? count : DCOLS_MAX;
    return (c < 1) ? 1 : c;
}

void drawer_open(const Config *cfg)
{
    int i;
    g_cfg = cfg;
    g_sel = -1;
    g_top = 0;
    if (g_bm == (IconBitmap far *)0) {
        unsigned seg;
        unsigned paras = (unsigned)
            ((CFG_MAX_SHORTCUTS * sizeof(IconBitmap) + 15U) / 16U);
        if (_dos_allocmem(paras, &seg) == 0)
            g_bm = (IconBitmap far *)MK_FP(seg, 0);
    }
    if (g_bm == (IconBitmap far *)0) {
        scan_dir_entries();
        return;
    }
    for (i = 0; i < CFG_MAX_SHORTCUTS; ++i) {
        g_bm[i].loaded = FALSE;
        if (cfg != NULL && i < cfg->shortcut_count &&
            cfg->shortcuts[i].icon[0] != '\0')
            icon_load(cfg->shortcuts[i].icon, &g_bm[i]);
    }
    scan_dir_entries();
}

int drawer_entry_count(void)
{
    return entry_total();
}

void drawer_window_size(int count, int *w, int *h)
{
    int cols = grid_cols(count);
    int rows = (count + cols - 1) / cols;
    int sc   = ui_scale(), maxr;
    if (rows < 1) rows = 1;
    /* Never taller than the work area: an overgrown drawer used to bury
       its bottom row (and the taskbar's clicks) under the taskbar.  The
       draw already clips overflow rows. */
    if (sc < 1) sc = 1;
    maxr = ((SCREEN_H / sc - 14) - 26) / DCELL_BASE_H;
    if (maxr < 1) maxr = 1;
    if (rows > maxr) rows = maxr;
    /* Mode-13h base pixels (56x46 per cell); open_centered() scales these
       by the icon/font scale for Mode 12h. */
    *w = cols * 56 + 12;
    /* Reserve the scroll strip, or the arrows sit ON the last column's
       cells and drawer_click tests them first - so clicking the right
       edge of the top-right icon scrolled instead of launching it.
       group.c already reserves it this way. */
    *w += 14;
    *h = rows * DCELL_BASE_H + 26;
}

/* First visible ROW.  drawer_window_size() clamps the window to the work
   area and the draw clipped any overflow row, so with more entries than
   fit - the INI allows sixteen shortcuts plus sixteen scanned programs -
   the later ones were invisible AND unclickable, while drawer_key
   happily walked the selection onto them and the cursor vanished off the
   bottom.  The Arcade group already solved this; the drawer does now. */
/* Published for drawer_key, which has no client rect of its own and
   needs to know how big a page is - the same trick scrap.c uses for its
   own PgUp/PgDn.  Set on every call, so it always describes the window
   the user is actually looking at, resized or not. */
static int g_vis_rows = 1;

static int rows_fit(const Rect *cl)
{
    int r = (cl->h - 8) / DCELL_H;
    if (r < 1) r = 1;
    g_vis_rows = r;
    return r;
}

static int rows_total(int n, int cols)
{
    return (n + cols - 1) / cols;
}

/* The scrollbar, drawn only when the grid overflows. */
static void arrow_rects(const Rect *cl, Rect *up, Rect *dn)
{
    int bw = 10 * ui_scale();
    rect_set(up, cl->x + cl->w - bw - 2, cl->y + 2, bw, bw);
    rect_set(dn, cl->x + cl->w - bw - 2, cl->y + cl->h - bw - 2, bw, bw);
}

static void track_rect(const Rect *cl, Rect *tr)
{
    Rect up, dn;
    arrow_rects(cl, &up, &dn);
    rect_set(tr, up.x, up.y + up.h, up.w, dn.y - (up.y + up.h));
    if (tr->h < 0) tr->h = 0;
}

static void clamp_top(const Rect *cl, int n, int cols)
{
    int hidden = rows_total(n, cols) - rows_fit(cl);
    if (hidden < 0) hidden = 0;
    if (g_top > hidden) g_top = hidden;
    if (g_top < 0) g_top = 0;
}

/* Keep the selected cell on screen (the keyboard moves it). */
static void follow_sel(const Rect *cl, int cols)
{
    int row, fit;
    if (g_sel < 0)
        return;
    row = g_sel / cols;
    fit = rows_fit(cl);
    if (row < g_top) g_top = row;
    if (row >= g_top + fit) g_top = row - fit + 1;
}

/* Top-left of cell i within the client rectangle. */
static void cell_xy(const Rect *cl, int i, int cols, int *cx, int *cy)
{
    int col = i % cols, row = i / cols;
    *cx = cl->x + 4 + col * DCELL_W;
    *cy = cl->y + 4 + (row - g_top) * DCELL_H;
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

void drawer_draw(const Rect *cl)
{
    int i, n, cols;
    if (g_cfg == NULL)
        return;
    n    = entry_total();
    cols = grid_cols(n);
    /* Follow the selection only when the KEYBOARD moved it.  Running this
       on every draw snapped g_top straight back to the selected row, so
       the scroll arrows did nothing at all: click Down with the first
       entry selected and the next redraw undid it.  Everything past the
       first screenful was unreachable by mouse. */
    if (g_follow) {
        follow_sel(cl, cols);
        g_follow = FALSE;
    }
    clamp_top(cl, n, cols);
    for (i = 0; i < n; ++i) {
        int cx, cy, ix, ly;
        if (i / cols < g_top)                  /* scrolled off the top   */
            continue;
        cell_xy(cl, i, cols, &cx, &cy);
        if (cy + DCELL_H > cl->y + cl->h)      /* clip any overflow row */
            break;
        ix = cx + (DCELL_W - ICON_SIZE) / 2;
        ly = cy + ICON_SIZE + 2;

        if (i < g_cfg->shortcut_count &&
            g_bm != (IconBitmap far *)0 && g_bm[i].loaded)
            icon_draw(&g_bm[i], ix, cy, ui_scale());
        else
            ui_icon(ui_icon_for_command(entry_command(i)), ix, cy);

        if (i == g_sel) {
            vid_fillrect(cx, ly - 1, DCELL_W, font_h() * 2 + 3, C_TITLE);
            cell_label(cx, ly, DCELL_W, entry_name(i), C_WHITE);
            vid_rect(ix - 2, cy - 2, ICON_SIZE + 4, ICON_SIZE + 4, C_WHITE);
        } else {
            cell_label(cx, ly, DCELL_W, entry_name(i), C_BLACK);
        }
    }
    /* A real scrollbar, only when the grid actually overflows.  These
       were the LETTERS "^" and "v" on two buttons with nothing between
       them - the v read as a U, and nothing indicated how much was
       below the fold. */
    if (rows_total(n, cols) > rows_fit(cl)) {
        Rect up, dn, tr;
        arrow_rects(cl, &up, &dn);
        track_rect(cl, &tr);
        ui_vscroll(&up, &dn, &tr, g_top, rows_fit(cl), rows_total(n, cols));
    }
}

static int hit_index(const Rect *cl, int mx, int my)
{
    int i, cols, n;
    if (g_cfg == NULL)
        return -1;
    n    = entry_total();
    cols = grid_cols(n);
    for (i = 0; i < n; ++i) {
        int cx, cy;
        if (i / cols < g_top)
            continue;                       /* scrolled off the top       */
        cell_xy(cl, i, cols, &cx, &cy);
        if (cy + DCELL_H > cl->y + cl->h)
            break;                          /* and off the bottom          */
        if (mx >= cx && mx < cx + DCELL_W && my >= cy && my < cy + DCELL_H)
            return i;
    }
    return -1;
}

/* Record entry idx as the launch target. */
static bool_t arm_launch(int idx)
{
    if (g_cfg == NULL || idx < 0 || idx >= entry_total())
        return FALSE;
    if (idx < g_cfg->shortcut_count) {
        scopy(g_cmd,  g_cfg->shortcuts[idx].command, CFG_CMD_LEN);
        scopy(g_path, g_cfg->shortcuts[idx].path,    CFG_PATH_LEN);
        g_freemem = g_cfg->shortcuts[idx].freemem;
    } else {                       /* scanned: run it from the scan dir  */
        scopy(g_cmd,  g_scan[idx - g_cfg->shortcut_count], CFG_CMD_LEN);
        scopy(g_path, g_cfg->drawer_scan, CFG_PATH_LEN);
        g_freemem = FALSE;
    }
    return TRUE;
}

/* Screen rectangle of the cell the last successful click launched from. */
static Rect   g_launch_rect;
static bool_t g_launch_rect_ok = FALSE;

bool_t drawer_take_launch_rect(Rect *r)
{
    if (!g_launch_rect_ok)
        return FALSE;
    *r = g_launch_rect;
    g_launch_rect_ok = FALSE;
    return TRUE;
}

bool_t drawer_click(const Rect *cl, int mx, int my, bool_t dbl)
{
    int idx;
    {
        int n = entry_total(), cols = grid_cols(n);
        if (rows_total(n, cols) > rows_fit(cl)) {
            Rect up, dn;
            arrow_rects(cl, &up, &dn);
            if (rect_contains(&up, mx, my)) {
                if (g_top > 0) --g_top;
                return FALSE;
            }
            if (rect_contains(&dn, mx, my)) {
                ++g_top;
                clamp_top(cl, n, cols);
                return FALSE;
            }
        }
    }
    idx = hit_index(cl, mx, my);
    if (idx < 0) {
        g_sel = -1;
        return FALSE;
    }
    g_sel = idx;
    if (dbl && arm_launch(idx)) {
        int cols = grid_cols(entry_total()), cx, cy;
        cell_xy(cl, idx, cols, &cx, &cy);
        rect_set(&g_launch_rect, cx, cy, DCELL_W, DCELL_H);
        g_launch_rect_ok = TRUE;       /* the launch zooms out of this cell */
        return TRUE;
    }
    return FALSE;
}

bool_t drawer_key(int key)
{
    int n, cols;
    if (g_cfg == NULL || entry_total() == 0)
        return FALSE;
    n    = entry_total();
    cols = grid_cols(n);

    if (key == KEY_ENTER)
        return (g_sel >= 0) ? arm_launch(g_sel) : FALSE;

    if (g_sel < 0)
        g_sel = 0;
    else if (key == KEY_RIGHT && g_sel + 1 < n)        ++g_sel;
    else if (key == KEY_LEFT  && g_sel > 0)            --g_sel;
    else if (key == KEY_DOWN  && g_sel + cols < n)     g_sel += cols;
    else if (key == KEY_UP    && g_sel - cols >= 0)    g_sel -= cols;
    /* A page is a screenful of ROWS, so it is cols entries per row times
       however many rows currently fit.  The grid scrolled and the arrows
       walked it one row at a time; with the INI's sixteen shortcuts plus
       sixteen scanned programs that is a long walk. */
    else if (key == KEY_PGDN || key == KEY_PGUP) {
        int page = g_vis_rows * cols;
        if (page < 1) page = 1;
        g_sel += (key == KEY_PGDN) ? page : -page;
        if (g_sel >= n) g_sel = n - 1;
        if (g_sel < 0)  g_sel = 0;
    }
    else if (key == KEY_HOME) g_sel = 0;
    else if (key == KEY_END)  g_sel = n - 1;
    else
        return FALSE;
    g_follow = TRUE;                   /* scroll to it on the next draw    */
    return FALSE;
}

const char *drawer_launch_command(void) { return g_cmd; }
const char *drawer_launch_path(void)    { return g_path; }
bool_t      drawer_launch_freemem(void) { return g_freemem; }
