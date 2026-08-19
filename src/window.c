/* ======================================================================
 * window.c - Window manager for CASTALIA/386
 * ====================================================================== */
#include <stdio.h>
#include <string.h>
#include "window.h"
#include "video.h"
#include "ui.h"
#include "font.h"
#include "keyboard.h"
#include "files.h"
#include "dialog.h"
#include "system.h"
#include "calc.h"
#include "scrap.h"
#include "clock.h"
#include "paint.h"
#include "drawer.h"
#include "group.h"
#include "reversi.h"
#include "mines.h"
#include "snake.h"
#include "breaker.h"
#include "echo.h"
#include "fractal.h"
#include "colors.h"
#include "card.h"
#include "inspect.h"
#include "bench.h"
#include "music.h"
#include "puzzle.h"
#include "ttt.h"
#include "charmap.h"
#include "quadrix.h"
#include "depot.h"
#include "timer.h"
#include "eyes.h"
#include "patience.h"
#include "lights.h"
#include "settings.h"
#include "oracle.h"
#include "peek.h"
#include "agenda.h"
#include "find.h"
#include "media.h"
#include "about.h"
#include "pong.h"
#include "calendar.h"
#include "g2048.h"
#include "corral.h"
#include "typist.h"

/* Bottom of the usable desktop (just above the taskbar). Matches
   TASKBAR_H in desktop.h (font_h() + 6) in BOTH video modes - the old
   hard-coded 14 let windows slide under the taller Mode 12h taskbar. */
#define WORK_BOTTOM (SCREEN_H - (font_h() + 6))

typedef struct {
    bool_t used;
    bool_t minimized;      /* hidden from the desktop, still on the taskbar */
    bool_t shaded;         /* rolled up to its title bar (dbl-click title)  */
    bool_t maxed;          /* filling the work area (maximize box)          */
    Rect   saved;          /* pre-maximize geometry, for the restore        */
    int    full_h;         /* the unshaded height while shaded              */
    int    min_w, min_h;   /* opening size = resize floor (grow-only)       */
    int    kind;
    char   title[40];
    int    x, y, w, h;
} Window;

static Window g_w[WM_MAX];
static int    g_order[WM_MAX];     /* indices, bottom .. top              */
static int    g_n = 0;

static int g_drag = -1;            /* window id being dragged, or -1      */
static int g_dx = 0, g_dy = 0;     /* grab offset within the window       */
static int g_drag_x = 0, g_drag_y = 0;  /* prospective top-left (outline) */

static int g_rsz = -1;             /* window id being resized, or -1      */
static int g_rsz_w = 0, g_rsz_h = 0;    /* prospective size (outline)     */

static int     order_top_visible(void);
static Window *top_window(void);

/* Session memory of each kind's last geometry: re-opening an applet puts
   it back where the user left it, at the size they grew it to.  (Per
   KIND, so the two group windows share a slot - they are the same grid.) */
/* far: ~420 bytes off DGROUP.  Everything that touches these two must go
   through a `far` pointer or a whole-struct assignment - never a helper
   taking a near Rect*, which silently drops the segment and reads or
   WRITES DGROUP at the same offset instead. */
static Rect   far g_last_geom[WIN_KIND_COUNT];
static bool_t far g_last_set[WIN_KIND_COUNT];

/* A pending window animation for the caller to play (over the still-current
   back buffer) before the next repaint: kind 0 = close, 1 = minimize,
   2 = restore (from the taskbar).  For minimize/restore the window's own
   taskbar button index and the button count are recorded too, so the zoom
   can fly to/from the actual button instead of a generic spot. */
static Rect g_anim_rect;
static int  g_anim_kind  = -1;
static int  g_anim_bar_i = -1;
static int  g_anim_bar_n = 0;

static void stash_anim(int id, int kind)
{
    int m = 3 * ui_scale() + 1;
    if (id < 0 || id >= WM_MAX || !g_w[id].used)
        return;
    rect_set(&g_anim_rect, g_w[id].x, g_w[id].y, g_w[id].w + m, g_w[id].h + m);
    g_anim_kind  = kind;
    g_anim_bar_i = -1;
    g_anim_bar_n = 0;
    if (kind == 1 || kind == 2) {      /* which taskbar button is it?      */
        int s, c = 0;
        for (s = 0; s < WM_MAX; ++s) {
            if (!g_w[s].used)
                continue;
            if (s == id) { g_anim_bar_i = c; break; }
            ++c;
        }
        g_anim_bar_n = wm_bar_count();
    }
}

bool_t wm_take_anim(Rect *r, int *kind, int *bar_i, int *bar_n)
{
    if (g_anim_kind < 0)
        return FALSE;
    *r     = g_anim_rect;
    *kind  = g_anim_kind;
    *bar_i = g_anim_bar_i;
    *bar_n = g_anim_bar_n;
    g_anim_kind = -1;
    return TRUE;
}

/* ---- pool / z-order -------------------------------------------------- */
void wm_init(void)
{
    int i;
    for (i = 0; i < WM_MAX; ++i)
        g_w[i].used = FALSE;
    g_n = 0;
    g_drag = -1;
    g_rsz  = -1;
}

static int order_pos(int id)
{
    int i;
    for (i = 0; i < g_n; ++i)
        if (g_order[i] == id)
            return i;
    return -1;
}

static void raise_to_top(int id)
{
    int p = order_pos(id);
    int i;
    if (p < 0)
        return;
    for (i = p; i < g_n - 1; ++i)
        g_order[i] = g_order[i + 1];
    g_order[g_n - 1] = id;
}

int wm_open(int kind, const char *title, int x, int y, int w, int h)
{
    int id = -1, i;

    /* If a window of this kind already exists, restore and raise it. */
    for (i = 0; i < WM_MAX; ++i) {
        if (g_w[i].used && g_w[i].kind == kind && kind != WIN_GENERIC) {
            int k = 0;
            g_w[i].minimized = FALSE;
            if (g_w[i].shaded) {                 /* unroll a shaded window  */
                g_w[i].shaded = FALSE;
                g_w[i].h = g_w[i].full_h;
            }
            while (title[k] != '\0' && k < (int)sizeof(g_w[i].title) - 1) {
                g_w[i].title[k] = title[k];      /* titles follow the last  */
                ++k;                             /* open (file names etc.)  */
            }
            g_w[i].title[k] = '\0';
            raise_to_top(i);
            return i;
        }
    }
    for (i = 0; i < WM_MAX; ++i) {
        if (!g_w[i].used) { id = i; break; }
    }
    if (id < 0)
        return -1;

    g_w[id].used = TRUE;
    g_w[id].minimized = FALSE;
    g_w[id].shaded = FALSE;
    g_w[id].maxed  = FALSE;
    g_w[id].full_h = h;
    g_w[id].kind = kind;
    g_w[id].x = x;
    g_w[id].y = y;
    g_w[id].w = w;
    g_w[id].h = h;
    g_w[id].min_w = w;             /* a window never shrinks below this   */
    g_w[id].min_h = h;

    /* Re-open where the user left it, at the size they grew it to (the
       caller's w/h stay the resize floor, so layouts never break). */
    if (kind > WIN_GENERIC && kind < WIN_KIND_COUNT && g_last_set[kind]) {
        const Rect far *r = &g_last_geom[kind];   /* far: see the note above */
        if (r->w >= w && r->h >= h) {
            g_w[id].w = r->w;
            g_w[id].h = r->h;
            g_w[id].full_h = r->h;
        }
        g_w[id].x = r->x;
        g_w[id].y = r->y;
        if (g_w[id].x + g_w[id].w > SCREEN_W)
            g_w[id].x = SCREEN_W - g_w[id].w;
        if (g_w[id].y + g_w[id].h > WORK_BOTTOM)
            g_w[id].y = WORK_BOTTOM - g_w[id].h;
        if (g_w[id].x < 0) g_w[id].x = 0;
        if (g_w[id].y < 0) g_w[id].y = 0;
    }
    {
        int k = 0;
        while (title[k] != '\0' && k < (int)sizeof(g_w[id].title) - 1) {
            g_w[id].title[k] = title[k];
            ++k;
        }
        g_w[id].title[k] = '\0';
    }
    g_order[g_n++] = id;
    return id;
}

void wm_close_id(int id)
{
    int p, i;
    if (id < 0 || id >= WM_MAX || !g_w[id].used)
        return;
    if (g_w[id].kind == WIN_CARD)
        card_flush();                  /* never lose the deck on close     */
    /* Closing was the commonest way to lose work: the Scrap Box and the
       Sketch Pad both threw unsaved edits away without a word.  Ask, and
       simply DON'T close when the answer is no - the caller neither knows
       nor needs to (it just repaints a window that is still there). */
    if (g_w[id].kind == WIN_SCRAP && scrap_is_dirty()) {
        if (dialog_confirm("Scrap Box", "Close and lose the unsaved",
                           "changes in this document?") != DLG_YES)
            return;
        scrap_flush_state();
    }
    if (g_w[id].kind == WIN_PAINT && paint_is_dirty()) {
        if (dialog_confirm("Sketch Pad", "Close and lose the unsaved",
                           "changes to this drawing?") != DLG_YES)
            return;
        paint_flush_state();
    }
    /* Settings applies live, so the desktop already LOOKS changed while
       the INI still says otherwise - the one case where closing silently
       is most likely to be mistaken for having saved. */
    if (g_w[id].kind == WIN_SETTINGS && settings_is_dirty()) {
        if (dialog_confirm("Settings", "Close without saving these",
                           "preferences to CASTALIA.INI?") != DLG_YES)
            return;
        settings_flush_state();
    }
    if (g_w[id].kind > WIN_GENERIC && g_w[id].kind < WIN_KIND_COUNT) {
        /* A maximized window remembers its RESTORED shape - the maximize
           was a mode, not the geometry the user chose. */
        if (g_w[id].maxed) {
            g_last_geom[g_w[id].kind] = g_w[id].saved;
        } else {
            /* Field by field, NOT rect_set(): its Rect* parameter is
               near, so &g_last_geom[kind] arrived as a bare offset and
               the eight bytes of geometry were written into DGROUP at
               that offset - into the program's own string pool - every
               time a kind-tagged window was closed.  Disassembly of the
               old code: "add ax,offset _g_last_geom / call far ptr
               rect_set_", with the segment never loaded.  The maxed
               branch above was always correct: a far struct assignment
               copies through the right segment. */
            Rect far *lg = &g_last_geom[g_w[id].kind];
            lg->x = g_w[id].x;
            lg->y = g_w[id].y;
            lg->w = g_w[id].w;
            lg->h = g_w[id].shaded ? g_w[id].full_h : g_w[id].h;
        }
        g_last_set[g_w[id].kind] = TRUE;
    }
    stash_anim(id, 0);             /* let the caller play a close zoom       */
    g_w[id].used = FALSE;
    if (g_drag == id)
        g_drag = -1;
    if (g_rsz == id)
        g_rsz = -1;
    p = order_pos(id);
    if (p >= 0) {
        for (i = p; i < g_n - 1; ++i)
            g_order[i] = g_order[i + 1];
        --g_n;
    }
}

void wm_close_top(void)
{
    int p = order_top_visible();
    if (p >= 0)
        wm_close_id(g_order[p]);
}

/* Minimize / restore.  Minimizing drops focus to the next visible window;
   restoring raises the window back to the top. */
void wm_minimize_id(int id)
{
    if (id >= 0 && id < WM_MAX && g_w[id].used) {
        stash_anim(id, 1);         /* let the caller play a minimize zoom    */
        g_w[id].minimized = TRUE;
    }
}

void wm_set_min(int id, bool_t m)
{
    if (id >= 0 && id < WM_MAX && g_w[id].used)
        g_w[id].minimized = m;
}

/* "Show desktop": every visible window drops to the taskbar at once.
   Deliberately no per-window zoom - one repaint clears the desk. */
void wm_minimize_all(void)
{
    int i;
    for (i = 0; i < WM_MAX; ++i)
        if (g_w[i].used && !g_w[i].minimized)
            g_w[i].minimized = TRUE;
}

/* Classic staggered cascade of the visible windows, bottom to top, each
   offset by one title bar so every title stays readable.  Shaded windows
   unroll; minimized ones stay on the taskbar. */
void wm_cascade(void)
{
    int i, n = 0, step = TITLE_H + 4;
    for (i = 0; i < g_n; ++i) {
        Window *w = &g_w[g_order[i]];
        if (w->minimized)
            continue;
        if (w->shaded) { w->shaded = FALSE; w->h = w->full_h; }
        w->maxed = FALSE;
        w->x = 4 + n * step;
        w->y = 4 + n * step;
        if (w->x + w->w > SCREEN_W)    w->x = CAST_MAX(0, SCREEN_W - w->w);
        if (w->y + w->h > WORK_BOTTOM) w->y = CAST_MAX(0, WORK_BOTTOM - w->h);
        ++n;
    }
}

/* Tile the visible windows into a grid over the work area.  Panes never
   shrink below a window's opening size (the same grow-only floor the
   resize grip obeys), so on a crowded 320x200 they overlap a little
   rather than break an applet's layout. */
void wm_tile(void)
{
    int i, vis = 0, cols, rows, cw, ch, k = 0;
    for (i = 0; i < g_n; ++i)
        if (!g_w[g_order[i]].minimized)
            ++vis;
    if (vis == 0)
        return;
    cols = (vis <= 1) ? 1 : 2;
    rows = (vis + cols - 1) / cols;
    cw = SCREEN_W / cols;
    ch = WORK_BOTTOM / rows;
    for (i = 0; i < g_n; ++i) {
        Window *w = &g_w[g_order[i]];
        if (w->minimized)
            continue;
        if (w->shaded)
            w->shaded = FALSE;
        w->maxed = FALSE;
        w->w = CAST_MAX(w->min_w, cw - 6);
        w->h = CAST_MAX(w->min_h, ch - 6);
        w->full_h = w->h;
        w->x = (k % cols) * cw + 2;
        w->y = (k / cols) * ch + 2;
        if (w->x + w->w > SCREEN_W)    w->x = CAST_MAX(0, SCREEN_W - w->w);
        if (w->y + w->h > WORK_BOTTOM) w->y = CAST_MAX(0, WORK_BOTTOM - w->h);
        ++k;
    }
}

/* Outer footprint (incl. shadow) of window id, for the open/zoom effects. */
bool_t wm_window_rect(int id, Rect *r)
{
    int m = 3 * ui_scale() + 1;
    if (id < 0 || id >= WM_MAX || !g_w[id].used)
        return FALSE;
    rect_set(r, g_w[id].x, g_w[id].y, g_w[id].w + m, g_w[id].h + m);
    return TRUE;
}

bool_t wm_any_open(void)
{
    return (g_n > 0) ? TRUE : FALSE;
}

bool_t wm_has_kind(int kind)
{
    int i;
    for (i = 0; i < WM_MAX; ++i)
        if (g_w[i].used && g_w[i].kind == kind)
            return TRUE;
    return FALSE;
}

/* Rename an open window.  The Sketch Pad has no room on its toolbar for
   a filename - the Scrap Box shows one there - and Save PRE-FILLS the
   picker with the name it is holding, so a name the user cannot see is
   a name they can overwrite a file with by accident.  The title bar is
   where Windows put it and the only space this window has. */
static bool_t g_retitled = FALSE;

void wm_set_title(int kind, const char *title)
{
    int i, k;
    for (i = 0; i < WM_MAX; ++i) {
        if (!g_w[i].used || g_w[i].kind != kind)
            continue;
        if (strcmp(g_w[i].title, title) == 0)
            return;                    /* same name, nothing to repaint    */
        for (k = 0; title[k] != '\0' &&
                    k < (int)sizeof(g_w[i].title) - 1; ++k)
            g_w[i].title[k] = title[k];
        g_w[i].title[k] = '\0';
        /* The FRAME has changed, and a key or a click that the applet
           handled only repaints the CLIENT area - so the new name sat in
           the struct while the title bar went on showing the old one.
           Same poll-once shape as oracle_poll_damage(). */
        g_retitled = TRUE;
        return;
    }
}

bool_t wm_poll_retitled(void)
{
    bool_t r = g_retitled;
    g_retitled = FALSE;
    return r;
}

/* Z-order index of the topmost NON-minimized window, or -1 if none is
   visible.  A minimized window keeps its slot and z-order but is skipped by
   every "which window is on top / focused / hit" query. */
static int order_top_visible(void)
{
    int i;
    for (i = g_n - 1; i >= 0; --i)
        if (!g_w[g_order[i]].minimized)
            return i;
    return -1;
}

/* The focused (topmost visible) window, or NULL. */
static Window *top_window(void)
{
    int p = order_top_visible();
    return (p < 0) ? (Window *)0 : &g_w[g_order[p]];
}

/* Kind of the topmost visible window, or -1 if none.  Used by the
   partial-present fast path so an animating window (Serpent, Music Box) can
   refresh just its own footprint instead of blitting the whole frame. */
int wm_top_kind(void)
{
    Window *w = top_window();
    return (w == (Window *)0) ? -1 : w->kind;
}

/* Outer footprint of the topmost visible window, grown to include its shadow. */
bool_t wm_top_rect(Rect *r)
{
    int m = 3 * ui_scale() + 1;
    Window *w = top_window();
    if (w == (Window *)0) return FALSE;
    rect_set(r, w->x, w->y, w->w + m, w->h + m);
    return TRUE;
}

/* Client rectangle of the topmost visible window (for incremental redraws).
   A shaded window has no client area, so animating applets (Serpent,
   Breaker, the Fractal) pause politely while rolled up. */
static void client_rect(const Window *w, Rect *cl);
bool_t wm_top_client_rect(Rect *r)
{
    Window *w = top_window();
    if (w == (Window *)0 || w->shaded) return FALSE;
    client_rect(w, r);
    return TRUE;
}

/* ---- geometry helpers ------------------------------------------------ */

static void close_box_rect(const Window *w, Rect *cb)
{
    cb->w = font_h() + 2;          /* 10 at the 8px font                  */
    cb->h = font_h() + 1;          /* 9                                   */
    cb->x = w->x + w->w - 3 - cb->w;
    cb->y = w->y + 3;
}

/* The maximize box sits just left of the close box... */
static void max_box_rect(const Window *w, Rect *xb)
{
    Rect cb;
    close_box_rect(w, &cb);
    xb->w = cb.w;
    xb->h = cb.h;
    xb->x = cb.x - cb.w - 1;
    xb->y = cb.y;
}

/* ...and the minimize box left of that: [_][^][X], the full set. */
static void min_box_rect(const Window *w, Rect *mb)
{
    Rect xb;
    max_box_rect(w, &xb);
    mb->w = xb.w;
    mb->h = xb.h;
    mb->x = xb.x - xb.w - 1;
    mb->y = xb.y;
}

/* The interior client rectangle (below the title bar, inside the frame). */
static void client_rect(const Window *w, Rect *cl)
{
    cl->x = w->x + 3;
    cl->y = w->y + TITLE_H + 2;
    cl->w = w->w - 6;
    cl->h = w->h - TITLE_H - 5;
}

/* The resize grip: the bottom-right corner of the frame. */
static void grip_rect(const Window *w, Rect *g)
{
    int s = 9 * ui_scale();
    g->x = w->x + w->w - s;
    g->y = w->y + w->h - s;
    g->w = s;
    g->h = s;
}

/* ---- content painters ------------------------------------------------ */

static void draw_close_x(const Rect *cb)
{
    /* An X needs an ODD span and a stroke no wider than the scale, or the
       two diagonals meet in a slab instead of crossing.  Two 2px
       diagonals over a 3px span gave "##.## / .###. / .###. / ##.##" - a
       bowtie with no centre pixel at all.  ui_raise is 1px top-left and
       2px bottom-right, so the free field is (w-3) x (h-3): 7 x 6 on the
       10x9 box.  Fit the largest odd square inside that and centre it. */
    int sc = ui_scale();
    int iw = cb->w - 3, ih = cb->h - 3;
    int n  = (iw < ih) ? iw : ih;
    int x0, y0, i, t;
    if (sc < 1) sc = 1;
    if ((n & 1) == 0) --n;         /* odd: guarantees a crossing pixel     */
    if (n < 3) n = 3;
    x0 = cb->x + 1 + (iw - n) / 2;
    y0 = cb->y + 1 + (ih - n) / 2;
    ui_fill_face(cb->x, cb->y, cb->w, cb->h);
    ui_raise(cb->x, cb->y, cb->w, cb->h);
    for (i = 0; i < n; ++i)
        for (t = 0; t < sc; ++t) {          /* stroke thickens with scale  */
            vid_pixel(x0 + i,         y0 + i + t, C_BLACK);
            vid_pixel(x0 + n - 1 - i, y0 + i + t, C_BLACK);
        }
}

/* The minimize glyph: the Windows-95 short bar low on the LEFT of the box. */
static void draw_min_box(const Rect *mb)
{
    /* The free field is (w-3) x (h-3) - ui_raise is 1px top-left, 2px
       bottom-right - so a short bar low on the LEFT is: start one pixel
       in, take most of the width, and stop one row clear of the bottom.
       Both the w/2-from-x+3 version and its replacement ran into the
       inner shadow on a 10x9 box. */
    int s  = ui_scale();
    int iw = mb->w - 3, ih = mb->h - 3;
    int bh = 1 + s;
    ui_fill_face(mb->x, mb->y, mb->w, mb->h);
    ui_raise(mb->x, mb->y, mb->w, mb->h);
    vid_fillrect(mb->x + 1 + s, mb->y + 1 + ih - bh - 1,
                 iw - 3, bh, C_BLACK);
}

/* The maximize glyph: the Windows-95 little window (a box with a thick
   title bar); when already maximized it becomes two overlapping windows -
   the restore glyph. */
static void draw_max_box(const Rect *xb, bool_t maxed)
{
    /* The glyph is sized to the FREE field ((w-3) x (h-3), because
       ui_raise is 1px top-left and 2px bottom-right) less a one-pixel
       margin, so it neither touches the bevel nor shrinks to a blob.
       The caption bar stays thicker than the outline: at 1px each they
       were indistinguishable and the glyph read as a striped box. */
    int th = 1 + ui_scale();             /* caption-bar thickness           */
    ui_fill_face(xb->x, xb->y, xb->w, xb->h);
    ui_raise(xb->x, xb->y, xb->w, xb->h);
    if (!maxed) {
        int iw = xb->w - 3, ih = xb->h - 3;
        /* -2, not -1: (iw - gw)/2 with a single spare pixel is 0, so the
           whole margin went to the right and the glyph sat flush against
           the top and left bevel.  Two spare pixels centre properly. */
        int gw = iw - 2, gh = ih - 2;
        int gx, gy;
        if (gw < 4) gw = 4;
        if (gh < 4) gh = 4;
        gx = xb->x + 1 + (iw - gw) / 2;
        gy = xb->y + 1 + (ih - gh) / 2;
        vid_rect    (gx, gy, gw, gh, C_BLACK);       /* the window outline   */
        vid_fillrect(gx, gy, gw, th, C_BLACK);       /* its title bar        */
    } else {
        int gw = xb->w - 6, gh = xb->h - 6;
        int fx = xb->x + 2, fy = xb->y + 4;          /* front window         */
        int bxp, byp;
        if (gw < 4) gw = 4;
        if (gh < 4) gh = 4;
        bxp = fx + 2; byp = fy - 2;                  /* back window, up-right */
        vid_rect    (bxp, byp, gw, gh, C_BLACK);
        vid_fillrect(bxp, byp, gw, th, C_BLACK);
        vid_fillrect(fx, fy, gw, gh, C_FACE);        /* erase the overlap    */
        vid_rect    (fx, fy, gw, gh, C_BLACK);
        vid_fillrect(fx, fy, gw, th, C_BLACK);
    }
}


static void content_sysinfo(const Rect *cl)
{
    int i, n = system_line_count();
    for (i = 0; i < n; ++i)
        font_draw(cl->x + 4, cl->y + 3 + i * (font_h() + 1),
                  system_line(i), C_BLACK);
}

static void content_generic(const Rect *cl)
{
    int lh = font_h() + 4;
    font_draw(cl->x + 6, cl->y + 6,          "Castalia window.",    C_BLACK);
    font_draw(cl->x + 6, cl->y + 6 + lh,     "Drag the title bar;", C_BLACK);
    font_draw(cl->x + 6, cl->y + 6 + 2 * lh, "click [X] to close.", C_BLACK);
}

/* The quick reference.  It had drifted well behind the shell: no F10, no
   mention that F2 starts a new game in every game, nothing about the
   Gramophone or the Oracle, and one undifferentiated 17-line column that
   read like a changelog.  Grouped under headings and paged with the arrow
   keys, so it can grow without getting cramped. */
#define HELP_PAGES 4
static int g_help_page = 0;
/* One line of keys for the window that was in front when F1 was pressed:
   see help_set_context(). */
static const char *g_help_ctx  = (const char *)0;
static const char *g_help_name = (const char *)0;

static const char * const HELP_P0[] = {
    "Getting around",
    "",
    "F10 / right-click   menu",
    "Shift+Tab    next window",
    "F5 cascade     F6 tile",
    "Arrows+Enter  desk icons",
    "ESC       close / quit",
    "F1            this help",
    "",
    "Windows",
    "",
    "2x-click title   maximize",
    "2x-click taskbar show desk",
    "Title icon: shade, 2x close",
    "Drag the grip    resize",
    "Drop icon on icon   swap"
};
static const char * const HELP_P1[] = {
    "The Disk Cabinet",
    "",
    "F7 new folder  F2 rename",
    "F5 copy        F3 sort",
    "Del delete   Backspace up",
    "Enter opens; .TXT goes to",
    "the Scrap Box, .ICN to the",
    "Sketch Pad.",
    "",
    "Games",
    "",
    "F2 starts a new game in",
    "every one of them.  Arrows",
    "or the mouse play; best",
    "runs are kept in",
    "CASTALIA.HI."
};
static const char * const HELP_P2[] = {
    "Type at the Run box",
    "",
    "fileman  the Disk Cabinet",
    "arcade   the games group",
    "tools    the toolbox",
    "gram     the Gramophone",
    "oracle   the System Oracle",
    "paint    the Sketch Pad",
    "scrap    the Scrap Box",
    "cards    the Cardfile",
    "agenda   the day planner",
    "calc     the Calculator",
    "clock    the Clock",
    "cards    see also: Cardfile"
};

/* The verb list outgrew one page when the nine that were missing from it
   were added - a reference that stops halfway is its own kind of wrong. */
static const char * const HELP_P3[] = {
    "Run box, continued",
    "",
    "find     search the drive",
    "drawer   the Program Drawer",
    "charmap  the Character Map",
    "colors   the palette viewer",
    "cinema   FLI/FLC playback",
    "peek     the Hex Peek",
    "settings preferences",
    "demo     the Light Show",
    "pictures the gallery",
    "bench    the benchmark",
    "about    this shell",
    "exit     back to DOS"
};

static void content_help(const Rect *cl)
{
    const char * const *lines;
    int n, i, lh = font_h() + 1, top;
    char foot[26];

    if (g_help_page == 1) {
        lines = HELP_P1; n = (int)(sizeof(HELP_P1) / sizeof(HELP_P1[0]));
    } else if (g_help_page == 2) {
        lines = HELP_P2; n = (int)(sizeof(HELP_P2) / sizeof(HELP_P2[0]));
    } else if (g_help_page == 3) {
        lines = HELP_P3; n = (int)(sizeof(HELP_P3) / sizeof(HELP_P3[0]));
    } else {
        lines = HELP_P0; n = (int)(sizeof(HELP_P0) / sizeof(HELP_P0[0]));
    }
    top = cl->y + 4;

    /* The focused window's own keys come first, so F1 answers "what can I
       do HERE" before it answers "what can I do at all". */
    if (g_help_page == 0 && g_help_ctx != (const char *)0) {
        font_draw(cl->x + 6, cl->y + 4, g_help_name, C_TITLE);
        /* CLIPPED to the page.  These lines are up to 44 characters
           ("Arrows push, U undoes, R restarts, N/P level") and were drawn
           unbounded, so the longest of them lost their last letters off
           the right border - a help line that is itself cut off. */
        font_draw_n(cl->x + 6, cl->y + 4 + lh, g_help_ctx,
                    (cl->w - 10) / FONT_ADV, C_BLACK);
        if (g_help_name != (const char *)0 && n > 0) {
            vid_hline(cl->x + 4, cl->y + 4 + lh * 2 + 2, cl->w - 8, C_SHADOW);
            top += lh * 2 + 5;
        }
    }
    for (i = 0; i < n; ++i) {
        int y = top + i * lh;
        if (y + font_h() > cl->y + cl->h - lh - 2)
            break;
        /* A heading stands ALONE: blank line before (or the top of the
           page) and blank line after.  Testing only the successor made
           the last entry of every group - "F1  this help" - come out in
           heading blue. */
        if (lines[i][0] != '\0' &&
            (i == 0 || lines[i - 1][0] == '\0') &&
            i + 1 < n && lines[i + 1][0] == '\0')
            font_draw(cl->x + 6, y, lines[i], C_TITLE);
        else
            font_draw(cl->x + 6, y, lines[i], C_BLACK);
    }
    sprintf(foot, "Page %d of %d - arrows", g_help_page + 1, HELP_PAGES);
    ui_text_center(cl->x, cl->y + cl->h - font_h() - 2, cl->w, foot, C_DKGRAY);
}

/* One line of keys for the window that was in front when F1 was pressed.
   F1 used to show the same static page whatever you were looking at, so
   not one applet or game key was documented anywhere in the program -
   including the F2 = New Game convention the whole arcade now follows. */
void help_set_context(int kind)
{
    g_help_name = (const char *)0;
    g_help_ctx  = (const char *)0;
    switch (kind) {
    case WIN_FILEMAN:
        g_help_name = "Disk Cabinet";
        g_help_ctx  = "Space + - * tag, Del removes, F7 F2 F5 F6";
        break;
    case WIN_MINES:
        g_help_name = "Minefield";
        g_help_ctx  = "Enter digs, Space/RMB flags";
        break;
    case WIN_PUZZLE:
        g_help_name = "Fifteen";
        g_help_ctx  = "Arrows slide a tile";
        break;
    case WIN_TTT:
        g_help_name = "Tic Tac Toe";
        g_help_ctx  = "Arrows + Enter take a square";
        break;
    case WIN_REVERSI:
        g_help_name = "Reversi";
        g_help_ctx  = "Arrows+Enter; dots are legal";
        break;
    case WIN_G2048:
        g_help_name = "2048";
        g_help_ctx  = "Arrows, or click a side";
        break;
    case WIN_SNAKE:  case WIN_BREAKER: case WIN_PONG:
    case WIN_QUADRIX:
        g_help_name = "This game";
        g_help_ctx  = "Arrows play";
        break;
    case WIN_SCRAP:
        g_help_name = "Scrap Box";
        g_help_ctx  = "F2 save, F3 load, F7 new, F5 find, F6 again";
        break;
    case WIN_PAINT:
        g_help_name = "Sketch Pad";
        g_help_ctx  = "Arrows+Space draw, Tab tool, F2 saves";
        break;
    case WIN_SETTINGS:
        g_help_name = "Settings";
        g_help_ctx  = "Tab/arrows, Enter, S saves";
        break;
    case WIN_MEDIA:
        g_help_name = "Gramophone";
        g_help_ctx  = "E opens, Space plays, S stops, PgUp/PgDn track";
        break;
    case WIN_ORACLE:
        g_help_name = "System Oracle";
        g_help_ctx  = "Up/Down change page";
        break;
    /* Twenty more kinds used to fall through to default: and get only the
       three generic pages, even though every one of them has a real key
       handler in g_app.  Each line below was read off that handler, not
       guessed - the Sketch Pad entry above is what happens otherwise. */
    case WIN_CALC:
        g_help_name = "Calculator";
        g_help_ctx  = "Digits, + - * /, Enter, C clears";
        break;
    case WIN_CARD:
        g_help_name = "Cardfile";
        /* Every word of the old line was wrong: arrows move the CURSOR,
           Enter inserts a newline, and Del removes a CHARACTER. */
        g_help_ctx  = "PgUp/PgDn flip, F7 adds, F8 removes";
        break;
    case WIN_PATIENCE:
        g_help_name = "Patience";
        g_help_ctx  = "Space deals, F2 or N a new game";
        break;
    case WIN_CORRAL:
        g_help_name = "Corral";
        g_help_ctx  = "Arrows draw, Space/Tab turns, F2 new";
        break;
    case WIN_TYPIST:
        g_help_name = "Typing Tutor";
        g_help_ctx  = "Just type; Tab skips a line";
        break;
    case WIN_AGENDA:
        g_help_name = "Agenda";
        g_help_ctx  = "Space ticks, A adds, Del removes";
        break;
    case WIN_CAL:
        g_help_name = "Calendar";
        g_help_ctx  = "Arrows, PgUp/PgDn month, Home today";
        break;
    case WIN_FIND:
        g_help_name = "Find File";
        g_help_ctx  = "Enter opens, F4 folder, F3 name, F2 text";
        break;
    case WIN_PEEK:
        g_help_name = "Hex Peek";
        g_help_ctx  = "Arrows, PgUp/PgDn, Home/End";
        break;
    case WIN_DEPOT:
        g_help_name = "Depot";
        g_help_ctx  = "Arrows push, U undoes, R restarts, N/P level";
        break;
    case WIN_TIMER:
        g_help_name = "Stopwatch";
        g_help_ctx  = "Space start/stop, L lap, R reset";
        break;
    case WIN_LIGHTS:
        g_help_name = "Lights Out";
        g_help_ctx  = "Click a lamp; F2 or N a new board";
        break;
    case WIN_ECHO:
        g_help_name = "Echo";
        g_help_ctx  = "Space/Enter repeats, F2 restarts";
        break;
    case WIN_FRACT:
        g_help_name = "Fractal";
        g_help_ctx  = "+/- zoom, M/J set, I/O iterate, R resets";
        break;
    case WIN_BENCH:
        g_help_name = "Benchmark";
        g_help_ctx  = "Enter runs the suite again";
        break;
    case WIN_DRAWER:
        g_help_name = "Program Drawer";
        g_help_ctx  = "Arrows pick, Enter launches";
        break;
    case WIN_GROUP:
        g_help_name = "Toolbox / Arcade";
        g_help_ctx  = "Arrows pick, PgUp/PgDn scroll, Enter opens";
        break;
    /* These have NO key handler.  Saying so is the honest answer to
       "what can I do here", and stops the user hunting for keys. */
    case WIN_CLOCK: case WIN_EYES:
        g_help_name = "This window";
        g_help_ctx  = "Nothing to drive - it just runs";
        break;
    case WIN_MUSIC:
        g_help_name = "Music Box";
        g_help_ctx  = "Arrows pick, Enter plays or stops, S stops";
        break;
    case WIN_CHARMAP:
        g_help_name = "Character Map";
        g_help_ctx  = "Type a character, or walk with the arrows";
        break;
    case WIN_COLORS: case WIN_INSPECT:
        g_help_name = "This window";
        g_help_ctx  = "Mouse only; Esc closes it";
        break;
    default:
        break;
    }
    g_help_page = 0;
}

/* Arrows (or PgUp/PgDn) turn the page. */
bool_t help_key(int key)
{
    if (key == KEY_RIGHT || key == KEY_DOWN || key == KEY_PGDN) {
        g_help_page = (g_help_page + 1) % HELP_PAGES;
        return TRUE;
    }
    if (key == KEY_LEFT || key == KEY_UP || key == KEY_PGUP) {
        g_help_page = (g_help_page + HELP_PAGES - 1) % HELP_PAGES;
        return TRUE;
    }
    return FALSE;
}

/* ---- window painting ------------------------------------------------- */

/* The title-bar app badge: a tiny per-application glyph (11x10-ish) at the
   caption's left, the way every Windows-95 window wears its own icon.  Apps
   without a face of their own wear the Castalia crest. */
static void draw_title_badge(int kind, int x, int y)
{
    switch (kind) {
    case WIN_FILEMAN: case WIN_DRAWER: case WIN_GROUP:
        vid_fillrect(x + 1, y + 1, 5, 2, C_YELLOW);        /* folder tab     */
        vid_fillrect(x,     y + 3, 11, 6, C_DKYELLOW);     /* folder body    */
        vid_hline   (x,     y + 3, 11, C_YELLOW);
        break;
    case WIN_SCRAP: case WIN_CARD: case WIN_AGENDA:
    case WIN_PEEK:  case WIN_HELP: case WIN_TYPIST:
        vid_fillrect(x + 1, y, 9, 10, C_WHITE);            /* a ruled page   */
        vid_rect    (x + 1, y, 9, 10, C_DKGRAY);
        vid_hline   (x + 3, y + 3, 5, C_SHADOW);
        vid_hline   (x + 3, y + 5, 5, C_SHADOW);
        vid_hline   (x + 3, y + 7, 5, C_SHADOW);
        break;
    case WIN_MEDIA: case WIN_MUSIC:
        vid_fillrect(x + 2, y + 6, 3, 3, C_CYAN);          /* a beamed note  */
        vid_fillrect(x + 4, y,     2, 8, C_WHITE);
        vid_fillrect(x + 4, y,     5, 2, C_WHITE);
        break;
    case WIN_PUZZLE: case WIN_TTT:     case WIN_MINES:
    case WIN_REVERSI:case WIN_SNAKE:   case WIN_BREAKER:
    case WIN_ECHO:   case WIN_QUADRIX: case WIN_DEPOT:
    case WIN_PATIENCE: case WIN_LIGHTS: case WIN_PONG: case WIN_G2048:
    case WIN_CORRAL:
        vid_fillrect(x + 1, y, 10, 10, C_WHITE);           /* a die: 3 pips  */
        vid_rect    (x + 1, y, 10, 10, C_DKGRAY);
        vid_fillrect(x + 3, y + 2, 2, 2, C_RED);
        vid_fillrect(x + 5, y + 4, 2, 2, C_RED);
        vid_fillrect(x + 7, y + 6, 2, 2, C_RED);
        break;
    case WIN_CLOCK: case WIN_TIMER: case WIN_CAL:
        vid_fillrect(x + 1, y, 10, 10, C_WHITE);           /* a clock face   */
        vid_rect    (x + 1, y, 10, 10, C_DKGRAY);
        vid_vline   (x + 6, y + 2, 3, C_BLACK);            /* hands          */
        vid_hline   (x + 6, y + 5, 3, C_BLACK);
        break;
    case WIN_SETTINGS: case WIN_COLORS: case WIN_CHARMAP:
        vid_fillrect(x, y, 11, 10, C_FACE);                /* slider panel   */
        vid_rect    (x, y, 11, 10, C_DKGRAY);
        vid_hline   (x + 2, y + 3, 7, C_SHADOW);
        vid_fillrect(x + 3, y + 2, 2, 3, C_BLACK);
        vid_hline   (x + 2, y + 7, 7, C_SHADOW);
        vid_fillrect(x + 7, y + 6, 2, 3, C_BLACK);
        break;
    case WIN_CALC:
        vid_fillrect(x + 1, y, 9, 10, C_FACE);             /* calculator     */
        vid_rect    (x + 1, y, 9, 10, C_DKGRAY);
        vid_fillrect(x + 3, y + 2, 5, 2, C_GREEN);         /* LCD            */
        vid_fillrect(x + 3, y + 6, 2, 2, C_DKGRAY);        /* keys           */
        vid_fillrect(x + 6, y + 6, 2, 2, C_DKGRAY);
        break;
    case WIN_PAINT:
        vid_fillrect(x, y + 1, 11, 8, C_FACE);             /* paint wells    */
        vid_rect    (x, y + 1, 11, 8, C_DKGRAY);
        vid_fillrect(x + 2, y + 3, 2, 2, C_RED);
        vid_fillrect(x + 5, y + 3, 2, 2, C_GREEN);
        vid_fillrect(x + 8, y + 3, 2, 2, C_BLUE);
        vid_fillrect(x + 3, y + 6, 2, 2, C_YELLOW);
        vid_fillrect(x + 6, y + 6, 2, 2, C_CYAN);
        break;
    case WIN_SYSINFO: case WIN_INSPECT: case WIN_ORACLE:
    case WIN_BENCH:   case WIN_FRACT:
        vid_fillrect(x, y, 11, 8, C_FACE);                 /* a monitor      */
        vid_rect    (x, y, 11, 8, C_DKGRAY);
        vid_fillrect(x + 2, y + 2, 7, 4, C_BLACK);         /* screen         */
        vid_hline   (x + 3, y + 4, 5, C_GREEN);            /* trace          */
        vid_fillrect(x + 4, y + 8, 3, 2, C_DKGRAY);        /* foot           */
        break;
    default:
        ui_start_castle(x - 1, y, 1);                      /* the crest      */
        break;
    }
}

/* ---- the applet table -------------------------------------------------
 * draw_client, wm_key and wm_rpress each used to enumerate every window
 * kind by hand, in three parallel chains over the same forty applets.
 * That is how wm_rpress came to be the only one of the three that forgot
 * to ask for a repaint after raising a window, and it is the same shape
 * that once let the "find" verb escape to DOS's FIND.EXE.  One row per
 * applet, one lookup per event, and a new applet is registered in a
 * single place.
 *
 * The Disk Cabinet's files_key() returns its own FILES_* enum rather
 * than a bool_t, so it stays a named special case in wm_key - being
 * explicit about the one exception is better than bending the table
 * around it.
 * -------------------------------------------------------------------- */
#define AF_SINK      0x01   /* sink the client before the applet draws    */
#define AF_SELFPAINT 0x02   /* owns its pixels; skip the face pre-fill    */
#define AF_TICKTOP   0x04   /* tick() only while this window is FOCUSED   */

typedef void   (*AppDrawFn)(const Rect *);
typedef bool_t (*AppKeyFn)(int);
typedef bool_t (*AppRClickFn)(const Rect *, int, int);
typedef bool_t (*AppTickFn)(void);

typedef struct {
    int         kind;
    AppDrawFn   draw;       /* 0 = the generic sunken text page           */
    AppKeyFn    key;        /* 0 = the window takes no keys               */
    AppRClickFn rclick;     /* 0 = no right-button behaviour              */
    AppTickFn   tick;       /* 0 = nothing animates on its own            */
    u8          flags;
    u8          key_yes;    /* WM_* when key() returns TRUE               */
    u8          key_no;     /* WM_* when it returns FALSE                 */
} AppEntry;

static const AppEntry far g_app[] = {
    { WIN_ABOUT,    about_draw,    about_key,    0, about_tick,            0,                     WM_REDRAW,      WM_NONE  },
    { WIN_FILEMAN,  files_draw,    0,            0, 0,            0,                     WM_NONE,        WM_NONE },
    { WIN_CALC,     calc_draw,     calc_key,     0, calc_tick, AF_TICKTOP,                     WM_REDRAW,      WM_NONE  },
    { WIN_SCRAP,    scrap_draw,    scrap_key,    0, 0,            0,                     WM_REDRAW,      WM_NONE },
    { WIN_CLOCK,    clock_draw,    0,            0, 0,            0,                     WM_NONE,        WM_NONE },
    { WIN_PAINT,    paint_draw,    paint_key,    0, 0,            0,                     WM_REDRAW,      WM_NONE },
    { WIN_DRAWER,   drawer_draw,   drawer_key,   0, 0,            AF_SINK,               WM_LAUNCH_PROG, WM_REDRAW },
    { WIN_INSPECT,  inspect_draw,  0,            0, 0,            0,                     WM_NONE,        WM_NONE },
    { WIN_BENCH,    bench_draw,    bench_key,    0, 0,            0,                     WM_REDRAW,      WM_NONE },
    { WIN_MUSIC,    music_draw,    music_key,    0, 0,            0,                     WM_REDRAW,      WM_NONE },
    { WIN_PUZZLE,   puzzle_draw,   puzzle_key,   0, 0,            0,                     WM_REDRAW,      WM_NONE },
    { WIN_TTT,      ttt_draw,      ttt_key,      0, 0,            0,                     WM_REDRAW,      WM_NONE },
    { WIN_CHARMAP,  charmap_draw,  charmap_key,  0, 0,            0,                     WM_REDRAW,      WM_NONE },
    { WIN_GROUP,    group_draw,    group_key,    0, 0,            AF_SINK,               WM_LAUNCH_GROUP, WM_REDRAW },
    { WIN_MINES,    mines_draw,    mines_key,    mines_rclick, mines_tick, AF_TICKTOP,                     WM_REDRAW,      WM_NONE  },
    { WIN_REVERSI,  reversi_draw,  reversi_key,  0, 0,            0,                     WM_REDRAW,      WM_NONE },
    { WIN_SNAKE,    snake_draw,    snake_key,    0, snake_tick, AF_TICKTOP,                     WM_REDRAW,      WM_NONE  },
    { WIN_COLORS,   colors_draw,   0,            0, 0,            0,                     WM_NONE,        WM_NONE },
    { WIN_CARD,     card_draw,     card_key,     0, 0,            0,                     WM_REDRAW,      WM_NONE },
    { WIN_BREAKER,  breaker_draw,  breaker_key,  0, 0,            0,                     WM_REDRAW,      WM_NONE },
    { WIN_ECHO,     echo_draw,     echo_key,     0, echo_tick, AF_TICKTOP,                     WM_REDRAW,      WM_NONE  },
    { WIN_FRACT,    fractal_draw,  fractal_key,  0, fractal_tick,            0,                     WM_REDRAW,      WM_NONE  },
    { WIN_QUADRIX,  quadrix_draw,  quadrix_key,  0, quadrix_tick, AF_TICKTOP,                     WM_REDRAW,      WM_NONE  },
    { WIN_DEPOT,    depot_draw,    depot_key,    0, 0,            0,                     WM_REDRAW,      WM_NONE },
    { WIN_TIMER,    timer_draw,    timer_key,    0, timer_tick,            0,                     WM_REDRAW,      WM_NONE  },
    { WIN_EYES,     eyes_draw,     0,            0, 0,            0,                     WM_NONE,        WM_NONE },
    { WIN_HELP,     0,             help_key,     0, 0,            0,                     WM_REDRAW,      WM_NONE },
    { WIN_PATIENCE, patience_draw, patience_key, 0, patience_tick,            AF_SELFPAINT|AF_TICKTOP,          WM_REDRAW,      WM_NONE  },
    { WIN_LIGHTS,   lights_draw,   lights_key,   0, 0,            0,                     WM_REDRAW,      WM_NONE },
    { WIN_SETTINGS, settings_draw, settings_key, 0, 0,            0,                     WM_STRUCT,      WM_NONE },
    { WIN_ORACLE,   oracle_draw,   oracle_key,   0, 0,            0,                     WM_REDRAW,      WM_NONE },
    { WIN_PEEK,     peek_draw,     peek_key,     0, 0,            0,                     WM_REDRAW,      WM_NONE },
    { WIN_AGENDA,   agenda_draw,   agenda_key,   0, 0,            0,                     WM_REDRAW,      WM_NONE },
    { WIN_MEDIA,    media_draw,    media_key,    0, 0,            0,                     WM_REDRAW,      WM_NONE },
    { WIN_PONG,     pong_draw,     pong_key,     0, 0,            0,                     WM_REDRAW,      WM_NONE },
    { WIN_CAL,      calendar_draw, calendar_key, 0, 0,            0,                     WM_REDRAW,      WM_NONE },
    { WIN_G2048,    g2048_draw,    g2048_key,    0, 0,            0,                     WM_REDRAW,      WM_NONE },
    { WIN_FIND,     find_draw,     find_key,     0, 0,            0,                     WM_REDRAW,      WM_NONE },
    { WIN_CORRAL,   corral_draw,   corral_key,   0, 0,            0,                     WM_REDRAW,      WM_NONE },
    { WIN_TYPIST,   typist_draw,   typist_key,   0, typist_tick, AF_TICKTOP,                     WM_REDRAW,      WM_NONE  },
};

#define APP_N ((int)(sizeof(g_app) / sizeof(g_app[0])))

/* kind -> row index + 1 (0 = no such applet), built once.  app_find used
   to LINEAR-SCAN all 40 far rows, and draw_client calls it for every
   window on every compose - up to 6 x 40 far struct reads, 18 times a
   second, to answer a question whose answer never changes.  42 bytes of
   far memory buys O(1). */
static u8 far g_app_ix[WIN_KIND_COUNT];
static bool_t g_app_ix_built = FALSE;

static const AppEntry far *app_find(int kind)
{
    if (!g_app_ix_built) {
        int i;
        for (i = 0; i < WIN_KIND_COUNT; ++i)
            g_app_ix[i] = 0;
        for (i = 0; i < APP_N; ++i)
            if (g_app[i].kind >= 0 && g_app[i].kind < WIN_KIND_COUNT)
                g_app_ix[g_app[i].kind] = (u8)(i + 1);
        g_app_ix_built = TRUE;
    }
    if (kind < 0 || kind >= WIN_KIND_COUNT || g_app_ix[kind] == 0)
        return (const AppEntry far *)0;
    return &g_app[g_app_ix[kind] - 1];
}

/* Drive every applet that animates on its own.  main.c carried this as a
   hand-written chain of `if (wm_top_kind() == WIN_X && x_tick())` lines -
   one far call per line to re-ask a question with a single answer, and a
   list a new applet had to be added to by hand or it silently never
   ticked.  The table already knows which applets exist; now it knows
   which ones move.  `present` is called with each kind that asked to be
   repainted (more than one can, per pass). */
void wm_tick_all(void (*present)(int kind))
{
    int topk = wm_top_kind();
    int i;
    for (i = 0; i < APP_N; ++i) {
        const AppEntry far *a = &g_app[i];
        if (a->tick == 0)
            continue;
        if (a->flags & AF_TICKTOP) {
            if (a->kind != topk)       /* focused-only: paused in the back */
                continue;
        } else if (!wm_has_kind(a->kind)) {
            continue;
        }
        if (a->tick())
            present(a->kind);
    }
}

/* TRUE for applets that own every pixel of their client and rely on what
   was there last frame.  Patience's win cascade deliberately skips its
   felt fill and draws one card on top of the previous frame so the trail
   paints itself - a pre-fill underneath turns that into a single card on
   flat grey. */
static bool_t client_self_paints(int kind)
{
    const AppEntry far *a = app_find(kind);
    return (a != (const AppEntry far *)0 && (a->flags & AF_SELFPAINT))
           ? TRUE : FALSE;
}

/* Paint just the client contents of a window into an already-prepared
   client rect (the face fill is the caller's job).  Split out of
   draw_window so an animation tick can repaint the contents alone. */
static void draw_client(const Window *w, const Rect *cl)
{
    const AppEntry far *a = app_find(w->kind);
    if (a != (const AppEntry far *)0 && a->draw != (AppDrawFn)0) {
        if (a->flags & AF_SINK)
            ui_sink(cl->x, cl->y, cl->w, cl->h);
        a->draw(cl);
        return;
    }
    /* No painter of its own: the generic sunken text page. */
    vid_fillrect(cl->x, cl->y, cl->w, cl->h, C_WHITE);
    ui_sink(cl->x, cl->y, cl->w, cl->h);
    switch (w->kind) {
    case WIN_SYSINFO: content_sysinfo(cl); break;
    case WIN_HELP:    content_help(cl);    break;
    default:          content_generic(cl); break;
    }
}

static void draw_window(const Window *w, bool_t active)
{
    Rect cb, cl;

    /* Drop shadow, then the frame on top of it. */
    ui_shadow(w->x, w->y, w->w, w->h);
    ui_fill_face(w->x, w->y, w->w, w->h);
    ui_raise(w->x, w->y, w->w, w->h);

    /* Title bar (gradient when active in Mode 13h; flat otherwise), with the
       app's own badge as its system-menu icon at the top-left (single click
       shades, double click closes). */
    vid_title_bar(w->x + 2, w->y + 2, w->w - 4, TITLE_H - 1, active);
    draw_title_badge(w->kind, w->x + 5, w->y + (TITLE_H - 10) / 2 + 1);
    {
        /* Windows 95 set the ACTIVE caption in bold, and it is most of
           why a Win95 title bar reads as a title bar.  Synthesise it by
           drawing the same text one pixel to the right as well - no new
           glyph data, no DGROUP cost. */
        int tx = w->x + 21, ty = w->y + (TITLE_H - font_h()) / 2 + 1;
        /* C_HILIGHT, not C_FACE: light grey on the C_SHADOW inactive bar
           left the title barely legible.  Win95's greyed caption still
           had real contrast. */
        font_draw(tx, ty, w->title, active ? C_WHITE : C_HILIGHT);
        if (active)
            font_draw(tx + 1, ty, w->title, C_WHITE);
    }

    /* Minimize + maximize + close boxes. */
    {
        Rect mb, xb;
        min_box_rect(w, &mb);
        draw_min_box(&mb);
        max_box_rect(w, &xb);
        draw_max_box(&xb, w->maxed);
    }
    close_box_rect(w, &cb);
    draw_close_x(&cb);

    /* A shaded window is just its title bar - no client area at all. */
    if (w->shaded)
        return;

    /* Resize grip: three diagonal strokes in the bottom-right corner. */
    {
        int s = ui_scale(), k, t;
        int gx = w->x + w->w - 3, gy = w->y + w->h - 3;
        for (k = 1; k <= 3; ++k) {
            int d = k * 3 * s;
            for (t = 0; t < d; ++t) {
                vid_pixel(gx - t, gy - (d - t), C_SHADOW);
                vid_pixel(gx - t, gy - (d - t) + 1, C_HILIGHT);
            }
        }
    }

    /* Client area, fenced to itself: an applet that miscomputes a width
       now truncates instead of painting over the frame, the drop shadow
       and whatever is behind them.  Intersect with whatever clip the
       caller already set (a partial present sets one) rather than
       replacing it. */
    client_rect(w, &cl);
    {
        Rect save;
        vid_get_clip(&save);
        if (vid_clip_hits(cl.x, cl.y, cl.w, cl.h)) {
            vid_set_clip_isect(&save, &cl);
            draw_client(w, &cl);
        }
        vid_set_clip(save.x, save.y, save.w, save.h);
    }
}

void wm_draw_all(void)
{
    int i, j, top = order_top_visible();
    int m = 3 * ui_scale() + 1;             /* dither drop-shadow margin    */
    for (i = 0; i < g_n; ++i) {
        const Window *wi = &g_w[g_order[i]];
        bool_t covered = FALSE;
        if (wi->minimized)
            continue;                       /* lives on the taskbar only    */
        /* Outside the clip there is nothing to paint, and composing a
           window anyway costs its whole chrome - the dither shadow, the
           face fill, the raised bevel, the gradient title bar, the badge,
           three caption boxes, the title text and the resize grip - for
           pixels that are all discarded.  Without this test the clipped
           present still paid for every window on screen, which is exactly
           the cost render_scene_clipped() exists to avoid. */
        if (!vid_clip_hits(wi->x, wi->y, wi->w + m, wi->h + m))
            continue;
        /* Occlusion cull: when this window's whole footprint (frame plus
           shadow) lies under a single higher window's opaque body, every
           pixel it would compose gets painted over anyway - skip it.  With
           windows stacked on a slow 386 this saves whole client repaints. */
        for (j = i + 1; j < g_n; ++j) {
            const Window *wj = &g_w[g_order[j]];
            if (wj->minimized)
                continue;
            if (wi->x >= wj->x && wi->y >= wj->y &&
                wi->x + wi->w + m <= wj->x + wj->w &&
                wi->y + wi->h + m <= wj->y + wj->h) {
                covered = TRUE;
                break;
            }
        }
        if (covered)
            continue;
        draw_window(wi, (i == top) ? TRUE : FALSE);
    }
}

/* Redraw ONLY the topmost window into the back buffer.  The partial-present
   fast path uses this for an animating window so a per-tick refresh costs
   just that window's own repaint, not a full-scene redraw (which would
   recompute the whole gradient desktop every frame). */
void wm_draw_top(void)
{
    Window *w = top_window();
    if (w != (Window *)0)
        draw_window(w, TRUE);
}

/* Repaint ONLY the focused window's client area.  An animation tick
   (Pong, Quadrix, Echo, the Clock, the Eyes, the Gramophone, Corral,
   2048, Patience, the Stopwatch...) changes nothing outside the client,
   yet wm_draw_top() rebuilt the whole window 18 times a second: the
   dither shadow, the face fill, the raised bevel, the gradient title bar,
   the badge, all three caption boxes, the resize grip and the title text.
   Repaint the client, blit the client, leave the chrome alone.
   FALSE when there is no focused window (the caller falls back). */
bool_t wm_draw_top_client(Rect *client)
{
    Window *w = top_window();
    Rect cl;
    if (w == (Window *)0 || w->shaded)
        return FALSE;
    client_rect(w, &cl);
    if (cl.w <= 0 || cl.h <= 0)
        return FALSE;
    if (!client_self_paints(w->kind))
        ui_fill_face(cl.x, cl.y, cl.w, cl.h);  /* the ground draw_window lays */
    {
        Rect save;
        vid_get_clip(&save);
        vid_set_clip_isect(&save, &cl);
        draw_client(w, &cl);
        vid_set_clip(save.x, save.y, save.w, save.h);
    }
    *client = cl;
    return TRUE;
}

/* ---- taskbar window buttons ----------------------------------------- */

/* The i-th used window slot in stable slot order, or -1. */
static int bar_slot(int i)
{
    int s, c = 0;
    for (s = 0; s < WM_MAX; ++s)
        if (g_w[s].used) { if (c == i) return s; ++c; }
    return -1;
}

int wm_bar_count(void)
{
    int s, c = 0;
    for (s = 0; s < WM_MAX; ++s)
        if (g_w[s].used) ++c;
    return c;
}

const char *wm_bar_title(int i)
{
    int s = bar_slot(i);
    return (s < 0) ? "" : g_w[s].title;
}

bool_t wm_bar_active(int i)
{
    int s = bar_slot(i);
    return (s >= 0 && &g_w[s] == top_window()) ? TRUE : FALSE;
}

bool_t wm_bar_min(int i)
{
    int s = bar_slot(i);
    return (s >= 0 && g_w[s].minimized) ? TRUE : FALSE;
}

/* Alt+Tab: rotate the z-order so the next window comes to the front,
   restoring it if it was minimized. */
void wm_cycle(void)
{
    int top, i;
    if (g_n < 2) {
        if (g_n == 1) g_w[g_order[0]].minimized = FALSE;
        return;
    }
    top = g_order[g_n - 1];
    for (i = g_n - 1; i > 0; --i)
        g_order[i] = g_order[i - 1];
    g_order[0] = top;
    if (g_w[g_order[g_n - 1]].minimized) {
        g_w[g_order[g_n - 1]].minimized = FALSE;
        stash_anim(g_order[g_n - 1], 2);   /* zoom up from its bar button  */
    }
}

/* Clicking a taskbar button: restore a minimized window, minimize the
   focused one, or bring another to the front. */
void wm_bar_click(int i)
{
    int s = bar_slot(i);
    if (s < 0)
        return;
    if (g_w[s].minimized) {
        g_w[s].minimized = FALSE;
        raise_to_top(s);
        stash_anim(s, 2);      /* zoom out of the taskbar button           */
    } else if (&g_w[s] == top_window()) {
        stash_anim(s, 1);
        g_w[s].minimized = TRUE;
    } else {
        raise_to_top(s);
    }
}

/* ---- hit testing & input -------------------------------------------- */

static int hit(int x, int y)
{
    int i;
    for (i = g_n - 1; i >= 0; --i) {
        Window *w = &g_w[g_order[i]];
        if (w->minimized)
            continue;                       /* not on the desktop           */
        if (x >= w->x && x < w->x + w->w &&
            y >= w->y && y < w->y + w->h)
            return g_order[i];
    }
    return -1;
}

/* A pending maximize/restore zoom: the frame's outline flies between the
   old and new geometry.  main.c drains it (wm_take_maxzoom) and plays the
   zoom before the full repaint, like the minimize animation. */
static Rect   g_mz_from, g_mz_to;
static bool_t g_mz_pending = FALSE;

bool_t wm_take_maxzoom(Rect *from, Rect *to)
{
    if (!g_mz_pending)
        return FALSE;
    *from = g_mz_from;
    *to   = g_mz_to;
    g_mz_pending = FALSE;
    return TRUE;
}

/* Fill the work area, or put the window back - shared by the maximize box
   and the Windows-95 double-click on the title bar. */
static void toggle_maximize(Window *w)
{
    rect_set(&g_mz_from, w->x, w->y, w->w, w->h);
    if (!w->maxed) {
        w->saved.x = w->x;
        w->saved.y = w->y;
        w->saved.w = w->w;
        w->saved.h = w->shaded ? w->full_h : w->h;
        w->shaded  = FALSE;
        w->x = 0;
        w->y = 0;
        w->w = SCREEN_W;
        w->h = WORK_BOTTOM;
        w->maxed = TRUE;
    } else {
        w->x = w->saved.x;
        w->y = w->saved.y;
        w->w = w->saved.w;
        w->h = w->saved.h;
        w->maxed = FALSE;
    }
    w->full_h = w->h;
    rect_set(&g_mz_to, w->x, w->y, w->w, w->h);
    g_mz_pending = TRUE;
    music_sfx(w->maxed ? 990 : 660, 1);
}

/* Roll the window up to its bare title bar, or unroll it - the shade. */
static void toggle_shade(Window *w)
{
    if (w->shaded) {
        w->shaded = FALSE;
        w->h = w->full_h;
    } else {
        w->full_h = w->h;
        w->h = TITLE_H + 5;
        w->shaded = TRUE;
    }
}

/* The clickable footprint of the title-bar app badge (the system icon). */
static void badge_rect(const Window *w, Rect *r)
{
    rect_set(r, w->x + 3, w->y + 2, 16, TITLE_H - 1);
}

int wm_press(int x, int y, bool_t dbl)
{
    int id = hit(x, y);
    Window *w;
    Rect cb;
    bool_t was_top;

    if (id < 0)
        return WM_MISS;

    {
        int tv = order_top_visible();
        was_top = (tv >= 0 && g_order[tv] == id) ? TRUE : FALSE;
    }
    raise_to_top(id);
    w = &g_w[id];

    /* Minimize box? */
    {
        Rect mb;
        min_box_rect(w, &mb);
        if (rect_contains(&mb, x, y)) {
            music_sfx(330, 1);         /* a little downward blip           */
            wm_minimize_id(id);
            return WM_MINIMIZE;
        }
    }

    /* Maximize box: fill the work area; press again to restore. */
    {
        Rect xb;
        max_box_rect(w, &xb);
        if (rect_contains(&xb, x, y)) {
            toggle_maximize(w);
            g_drag = -1;
            return WM_STRUCT;
        }
    }

    /* Close box? */
    close_box_rect(w, &cb);
    if (rect_contains(&cb, x, y)) {
        music_sfx(440, 1);             /* the goodbye blip                 */
        wm_close_id(id);
        return WM_CLOSED;
    }

    /* The resize grip (bottom-right corner) begins a rubber-band resize. */
    if (!w->shaded) {
        Rect gr;
        grip_rect(w, &gr);
        if (rect_contains(&gr, x, y)) {
            g_rsz   = id;
            g_rsz_w = w->w;
            g_rsz_h = w->h;
            return was_top ? WM_NONE : WM_RAISED;
        }
    }

    /* The app badge (system icon) at the title bar's left, Windows-95
       semantics: double-click CLOSES the window, a single click SHADES it -
       rolls it up to its bare title bar and back, the quick peek at what is
       underneath without disturbing the layout. */
    {
        Rect br;
        badge_rect(w, &br);
        if (rect_contains(&br, x, y)) {
            if (dbl) {
                music_sfx(440, 1);     /* the goodbye blip                 */
                wm_close_id(id);
                return WM_CLOSED;
            }
            toggle_shade(w);
            g_drag = -1;
            return WM_STRUCT;
        }
    }

    /* Title bar (but not the boxes or the badge).  A double-click
       maximizes / restores, exactly like Windows 95; a single click begins
       dragging. */
    if (y < w->y + TITLE_H + 2) {
        if (dbl) {
            toggle_maximize(w);
            g_drag = -1;
            return WM_STRUCT;      /* geometry changed: full repaint      */
        }
        g_drag = id;
        g_dx = x - w->x;
        g_dy = y - w->y;
        g_drag_x = w->x;        /* outline starts on the window           */
        g_drag_y = w->y;
        return was_top ? WM_NONE : WM_RAISED;
    }

    /* Click-to-focus, the Windows-95 rule: the press that brings a
       background window forward is spent doing exactly that, and does
       NOT also reach the applet.  Without this, clicking a background
       Minefield to look at it dug a cell, and clicking a background
       Patience played a card. */
    if (!was_top)
        return WM_RAISED;

    /* Client interaction. */
    if (w->kind == WIN_FILEMAN) {
        Rect cl;
        client_rect(w, &cl);
        if (files_click(&cl, x, y, dbl) == FILES_LAUNCH)
            return WM_LAUNCH;
        return WM_REDRAW;
    }
    if (w->kind == WIN_CALC || w->kind == WIN_SCRAP || w->kind == WIN_PAINT) {
        Rect cl;
        client_rect(w, &cl);
        if      (w->kind == WIN_CALC)  calc_click(&cl, x, y);
        else if (w->kind == WIN_SCRAP) scrap_click(&cl, x, y);
        else                           paint_click(&cl, x, y);
        return WM_REDRAW;
    }
    if (w->kind == WIN_DRAWER) {
        Rect cl;
        client_rect(w, &cl);
        if (drawer_click(&cl, x, y, dbl))
            return WM_LAUNCH_PROG;
        return WM_REDRAW;
    }
    if (w->kind == WIN_GROUP) {
        Rect cl;
        client_rect(w, &cl);
        if (group_click(&cl, x, y, dbl))
            return WM_LAUNCH_GROUP;
        return WM_REDRAW;
    }
    if (w->kind == WIN_BENCH) {
        Rect cl;
        client_rect(w, &cl);
        bench_click(&cl, x, y);
        return WM_REDRAW;
    }
    if (w->kind == WIN_MUSIC) {
        Rect cl;
        client_rect(w, &cl);
        music_click(&cl, x, y);
        return WM_REDRAW;
    }
    if (w->kind == WIN_PUZZLE) {
        Rect cl;
        client_rect(w, &cl);
        puzzle_click(&cl, x, y);
        return WM_REDRAW;
    }
    if (w->kind == WIN_TTT) {
        Rect cl;
        client_rect(w, &cl);
        ttt_click(&cl, x, y);
        return WM_REDRAW;
    }
    if (w->kind == WIN_CHARMAP) {
        Rect cl;
        client_rect(w, &cl);
        charmap_click(&cl, x, y);
        return WM_REDRAW;
    }
    if (w->kind == WIN_MINES) {
        Rect cl;
        client_rect(w, &cl);
        mines_click(&cl, x, y);
        return WM_REDRAW;
    }
    if (w->kind == WIN_REVERSI) {
        Rect cl;
        client_rect(w, &cl);
        reversi_click(&cl, x, y);
        return WM_REDRAW;
    }
    if (w->kind == WIN_SNAKE) {
        Rect cl;
        client_rect(w, &cl);
        snake_click(&cl, x, y);
        return WM_REDRAW;
    }
    if (w->kind == WIN_BREAKER) {
        Rect cl;
        client_rect(w, &cl);
        breaker_click(&cl, x, y);
        return WM_REDRAW;
    }
    if (w->kind == WIN_ECHO) {
        Rect cl;
        client_rect(w, &cl);
        echo_click(&cl, x, y);
        return WM_REDRAW;
    }
    if (w->kind == WIN_FRACT) {
        Rect cl;
        client_rect(w, &cl);
        fractal_click(&cl, x, y);
        return WM_REDRAW;
    }
    if (w->kind == WIN_CARD) {
        Rect cl;
        client_rect(w, &cl);
        card_click(&cl, x, y);
        return WM_REDRAW;
    }
    if (w->kind == WIN_QUADRIX) {
        Rect cl;
        client_rect(w, &cl);
        quadrix_click(&cl, x, y);
        return WM_REDRAW;
    }
    if (w->kind == WIN_DEPOT) {
        Rect cl;
        client_rect(w, &cl);
        depot_click(&cl, x, y);
        return WM_REDRAW;
    }
    if (w->kind == WIN_TIMER) {
        Rect cl;
        client_rect(w, &cl);
        timer_click(&cl, x, y);
        return WM_REDRAW;
    }
    if (w->kind == WIN_PATIENCE) {
        Rect cl;
        client_rect(w, &cl);
        patience_click(&cl, x, y, dbl);
        return WM_REDRAW;
    }
    if (w->kind == WIN_LIGHTS) {
        Rect cl;
        client_rect(w, &cl);
        lights_click(&cl, x, y);
        return WM_REDRAW;
    }
    if (w->kind == WIN_SETTINGS) {
        Rect cl;
        client_rect(w, &cl);
        /* Theme / backdrop changes repaint the whole desktop. */
        return settings_click(&cl, x, y) ? WM_STRUCT : WM_REDRAW;
    }
    if (w->kind == WIN_ORACLE) {
        Rect cl;
        client_rect(w, &cl);
        if (oracle_click(&cl, x, y))
            return WM_REDRAW;
    }
    if (w->kind == WIN_FIND) {
        Rect cl;
        client_rect(w, &cl);
        if (find_click(&cl, x, y))
            return WM_REDRAW;
    }
    if (w->kind == WIN_AGENDA) {
        Rect cl;
        client_rect(w, &cl);
        if (agenda_click(&cl, x, y))
            return WM_REDRAW;
    }
    if (w->kind == WIN_ABOUT) {
        Rect cl;
        client_rect(w, &cl);
        if (about_click(&cl, x, y))
            return WM_REDRAW;
    }
    if (w->kind == WIN_PONG) {
        Rect cl;
        client_rect(w, &cl);
        pong_click(&cl, x, y);
        return WM_REDRAW;
    }
    if (w->kind == WIN_CAL) {
        Rect cl;
        client_rect(w, &cl);
        if (calendar_click(&cl, x, y))
            return WM_REDRAW;
    }
    if (w->kind == WIN_G2048) {
        Rect cl;
        client_rect(w, &cl);
        g2048_click(&cl, x, y);
        return WM_REDRAW;
    }
    if (w->kind == WIN_CORRAL) {
        Rect cl;
        client_rect(w, &cl);
        corral_click(&cl, x, y);
        return WM_REDRAW;
    }
    if (w->kind == WIN_MEDIA) {
        Rect cl;
        client_rect(w, &cl);
        if (media_click(&cl, x, y))
            return WM_REDRAW;
    }
    /* Nothing specific was hit: a click on the bare frame.  If it pulled
       a background window forward, say so - the caller can then present
       just the two windows and the taskbar instead of the whole frame. */
    return was_top ? WM_NONE : WM_RAISED;
}

/* Update the prospective (outline) position; the window itself does NOT
   move until wm_release(), so dragging never repaints the whole scene. */
void wm_drag(int x, int y)
{
    Window *w;
    int nx, ny;
    if (g_rsz >= 0) {                  /* rubber-band resize               */
        int nw, nh;
        w = &g_w[g_rsz];
        nw = x - w->x + 4;
        nh = y - w->y + 4;
        if (nw < w->min_w) nw = w->min_w;      /* grow-only floor          */
        if (nh < w->min_h) nh = w->min_h;
        if (w->x + nw > SCREEN_W)    nw = SCREEN_W - w->x;
        if (w->y + nh > WORK_BOTTOM) nh = WORK_BOTTOM - w->y;
        g_rsz_w = nw;
        g_rsz_h = nh;
        return;
    }
    if (g_drag < 0)
        return;
    w = &g_w[g_drag];
    nx = x - g_dx;
    ny = y - g_dy;

    /* Keep the whole window on the desktop and above the taskbar.  The
       "not past the bottom/right" clamps run FIRST: for a window taller
       than the work area the reverse order would pin it at a negative y
       with its title bar irretrievably off-screen. */
    if (nx + w->w > SCREEN_W)    nx = SCREEN_W - w->w;
    if (ny + w->h > WORK_BOTTOM) ny = WORK_BOTTOM - w->h;
    if (nx < 0) nx = 0;
    if (ny < 0) ny = 0;
    g_drag_x = nx;
    g_drag_y = ny;
}

/* Fill r with the current drag outline rectangle (valid while dragging). */
void wm_drag_rect(Rect *r)
{
    if (g_rsz >= 0) {
        rect_set(r, g_w[g_rsz].x, g_w[g_rsz].y, g_rsz_w, g_rsz_h);
        return;
    }
    if (g_drag < 0) { rect_set(r, 0, 0, 0, 0); return; }
    rect_set(r, g_drag_x, g_drag_y, g_w[g_drag].w, g_w[g_drag].h);
}

/* Commit the drag: move the window to the outline position.  Returns TRUE
   if a window actually moved, so the caller can repaint once. */
bool_t wm_release(void)
{
    int id;
    if (g_rsz >= 0) {                  /* commit a resize                  */
        id = g_rsz;
        g_rsz = -1;
        if (g_w[id].w == g_rsz_w && g_w[id].h == g_rsz_h)
            return FALSE;
        g_w[id].w = g_rsz_w;
        g_w[id].h = g_rsz_h;
        g_w[id].full_h = g_rsz_h;      /* shading remembers the new size   */
        g_w[id].maxed  = FALSE;        /* hand-sized now, not "maximized"  */
        return TRUE;
    }
    id = g_drag;
    if (id < 0)
        return FALSE;
    g_drag = -1;
    if (g_w[id].x == g_drag_x && g_w[id].y == g_drag_y)
        return FALSE;
    g_w[id].x = g_drag_x;
    g_w[id].y = g_drag_y;
    g_w[id].maxed = FALSE;             /* moved off the full-screen spot   */
    return TRUE;
}

/* Old (current) and new (outline) outer footprints of the window being
   dragged, both grown to include the drop shadow.  Called just before
   wm_release() so the caller can blit only the two footprints on a move. */
bool_t wm_drag_bounds(Rect *oldr, Rect *newr)
{
    int m = 3 * ui_scale() + 1;
    if (g_rsz >= 0) {
        rect_set(oldr, g_w[g_rsz].x, g_w[g_rsz].y,
                 g_w[g_rsz].w + m, g_w[g_rsz].h + m);
        rect_set(newr, g_w[g_rsz].x, g_w[g_rsz].y,
                 g_rsz_w + m, g_rsz_h + m);
        return TRUE;
    }
    if (g_drag < 0)
        return FALSE;
    rect_set(oldr, g_w[g_drag].x, g_w[g_drag].y,
             g_w[g_drag].w + m, g_w[g_drag].h + m);
    rect_set(newr, g_drag_x, g_drag_y,
             g_w[g_drag].w + m, g_w[g_drag].h + m);
    return TRUE;
}

bool_t wm_dragging(void)
{
    return (g_drag >= 0 || g_rsz >= 0) ? TRUE : FALSE;
}

bool_t wm_over_window(int x, int y)
{
    return (hit(x, y) >= 0) ? TRUE : FALSE;
}

/* Right button inside a window.  Right-click was routed only to the bare
   desktop, so inside every window in the shell it did nothing at all -
   the most conspicuous missing affordance in a Windows-95 tribute.
   WM_REDRAW when a window took it, WM_MISS when nobody did (the caller
   then falls back to the desktop behaviour). */
int wm_rpress(int x, int y)
{
    int id = hit(x, y);
    int was_top = order_top_visible();
    bool_t raised;
    Rect cl;
    if (id < 0)
        return WM_MISS;
    raised = (was_top < 0 || g_order[was_top] != id) ? TRUE : FALSE;
    raise_to_top(id);
    /* The raise reorders the stack, so the screen no longer matches the
       model: say so, or the caller repaints nothing and the display
       stays wrong until some unrelated event happens to redraw. */
    if (g_w[id].shaded || g_w[id].minimized)
        return raised ? WM_RAISED : WM_NONE;
    client_rect(&g_w[id], &cl);
    if (!rect_contains(&cl, x, y))
        return raised ? WM_RAISED : WM_NONE;   /* the frame, not contents  */
    {
        const AppEntry far *a = app_find(g_w[id].kind);
        if (a != (const AppEntry far *)0 &&
            a->rclick != (AppRClickFn)0 && a->rclick(&cl, x, y))
            return WM_REDRAW;
    }
    return raised ? WM_RAISED : WM_NONE;
}

bool_t wm_point_on_top(int x, int y)
{
    int p = order_top_visible();
    return (p >= 0 && hit(x, y) == g_order[p]) ? TRUE : FALSE;
}

int wm_kind_rect(int kind, Rect *r)
{
    int i, m = 3 * ui_scale() + 1;
    for (i = 0; i < WM_MAX; ++i) {
        if (g_w[i].used && g_w[i].kind == kind) {
            if (g_w[i].minimized)
                return 0;
            rect_set(r, g_w[i].x, g_w[i].y, g_w[i].w + m, g_w[i].h + m);
            return 1;
        }
    }
    return -1;
}

bool_t wm_content_drag(int x, int y, Rect *dirty)
{
    Window *w = top_window();
    Rect cl;
    if (w == (Window *)0 || w->kind != WIN_PAINT)
        return FALSE;
    client_rect(w, &cl);
    if (!paint_drag(&cl, x, y))
        return FALSE;
    paint_drag_rect(dirty);            /* the single cell that changed        */
    return TRUE;
}

int wm_key(int key)
{
    Window *w = top_window();
    const AppEntry far *a;
    if (w == (Window *)0)
        return WM_NONE;
    /* The one applet whose key handler does not return a bool_t. */
    if (w->kind == WIN_FILEMAN) {
        int r = files_key(key);
        if (r == FILES_LAUNCH) return WM_LAUNCH;
        if (r == FILES_REDRAW) return WM_REDRAW;
        return WM_NONE;
    }
    a = app_find(w->kind);
    if (a == (const AppEntry far *)0 || a->key == (AppKeyFn)0)
        return WM_NONE;
    return a->key(key) ? (int)a->key_yes : (int)a->key_no;
}
