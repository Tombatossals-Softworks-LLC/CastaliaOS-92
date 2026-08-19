/* ======================================================================
 * mouse.c - Mouse input and software cursor for CASTALIA/386
 * ----------------------------------------------------------------------
 * INT 33h functions used:
 *   AX=00h  Reset driver / detect.  Returns AX=FFFFh if installed.
 *   AX=03h  Get button status (BX) and position (we use only BX).
 *   AX=0Bh  Read relative motion counters (CX,DX = signed mickeys since
 *           the previous fn 0Bh call).  Reading clears the counters.
 *
 * Sensitivity: MOUSE_DIV mickeys move the cursor one pixel.  Sub-pixel
 * remainders are accumulated so slow movements are not lost.  The value
 * was chosen to feel natural under DOSBox's default mouse sensitivity;
 * it can be retuned for a given mouse without touching anything else.
 * ====================================================================== */
#include <i86.h>
#include "mouse.h"
#include "video.h"

#define MOUSE_DIV 2       /* mickeys per pixel (tunable)                  */

/* The arrow cursor.  'X' = black outline, '.' = white fill, ' ' = clear.
   The hotspot (the actual click point) is the top-left pixel (0,0). */
static const char *CURSOR[CURSOR_H] = {
    "X           ",
    "XX          ",
    "X.X         ",
    "X..X        ",
    "X...X       ",
    "X....X      ",
    "X.....X     ",
    "X......X    ",
    "X.......X   ",
    "X........X  ",
    "X....XXXXX  ",
    "X.X.X       ",
    "XX X.X      ",
    "X  X.X      ",
    "   X.X      ",
    "    XX      "
};

/* The busy (hourglass) cursor, shown while a slow operation runs.  Same
   12x16 cell and 'X'=black / '.'=white / ' '=clear convention. */
static const char *HGLASS[CURSOR_H] = {
    "XXXXXXXXXXXX",
    "X..........X",
    "X..........X",
    " X........X ",
    "  X......X  ",
    "   X....X   ",
    "    X..X    ",
    "    X..X    ",
    "    X..X    ",
    "   X....X   ",
    "  X......X  ",
    " X........X ",
    "X..........X",
    "X..........X",
    "XXXXXXXXXXXX",
    "            "
};

static bool_t g_present = FALSE;
static bool_t g_visible = FALSE;
static bool_t g_busy    = FALSE;   /* TRUE => draw HGLASS instead of CURSOR  */

/* These are defaults only; mouse_init() recentres for the active screen
   size and main() calls mouse_set_bounds() with the real dimensions. */
static int g_x = 160;
static int g_y = 100;
static int g_buttons = 0;

static int g_accx = 0;            /* sub-pixel mickey remainder           */
static int g_accy = 0;

/* Left-button press events accumulated from INT 33h fn 05h.  The driver
   counts EVERY press in hardware, so a press is never lost even when a slow
   repaint keeps us from polling for tens of milliseconds - which is exactly
   why click detection off the polled button STATE (fn 03h) used to drop
   clicks.  main() drains this each iteration to drive clicks reliably. */
static int g_lpress = 0;
static int g_rpress = 0;               /* right presses (Dominus at cursor) */

/* Bounds the cursor is clamped to. */
static int g_minx = 0, g_miny = 0;
static int g_maxx = 320, g_maxy = 200;

/* Footprint of the last drawn cursor, for erase(). */
static int g_last_x = 0, g_last_y = 0;
static bool_t g_drawn = FALSE;

bool_t mouse_init(void)
{
    union REGS r;

    r.x.ax = 0x0000;              /* reset / detect                       */
    int86(0x33, &r, &r);
    if (r.x.ax != 0xFFFF) {
        g_present = FALSE;
        return FALSE;
    }
    g_present = TRUE;

    /* Prime fn 0Bh so the first mouse_update() reports motion from now. */
    r.x.ax = 0x000B;
    int86(0x33, &r, &r);

    g_x = SCREEN_W / 2;
    g_y = SCREEN_H / 2;
    g_accx = g_accy = 0;
    g_visible = TRUE;
    return TRUE;
}

static void clamp_pos(void)
{
    if (g_x < g_minx)       g_x = g_minx;
    if (g_x > g_maxx - 1)   g_x = g_maxx - 1;
    if (g_y < g_miny)       g_y = g_miny;
    if (g_y > g_maxy - 1)   g_y = g_maxy - 1;
}

void mouse_update(void)
{
    union REGS r;
    int dx, dy;

    if (!g_present)
        return;

    /* Button state. */
    r.x.ax = 0x0003;
    int86(0x33, &r, &r);
    g_buttons = r.x.bx & (MB_LEFT | MB_RIGHT | MB_MIDDLE);

    /* Left-button press count since the last poll (fn 05h, BX=0=left).
       BX returns the number of presses; the driver clears it on read, so
       presses accumulate in hardware across slow frames and none are lost. */
    r.x.ax = 0x0005;
    r.x.bx = 0x0000;
    int86(0x33, &r, &r);
    g_lpress += (int)r.x.bx;

    /* Right-button press count (fn 05h, BX=1): the desktop context menu. */
    r.x.ax = 0x0005;
    r.x.bx = 0x0001;
    int86(0x33, &r, &r);
    g_rpress += (int)r.x.bx;

    /* Relative motion in mickeys (signed 16-bit in CX/DX). */
    r.x.ax = 0x000B;
    int86(0x33, &r, &r);
    dx = (int)(short)r.x.cx;
    dy = (int)(short)r.x.dx;

    g_accx += dx;
    g_accy += dy;
    g_x += g_accx / MOUSE_DIV;
    g_y += g_accy / MOUSE_DIV;
    g_accx %= MOUSE_DIV;
    g_accy %= MOUSE_DIV;

    clamp_pos();
}

int mouse_x(void)        { return g_x; }
int mouse_y(void)        { return g_y; }
int mouse_buttons(void)  { return g_buttons; }

/* Take (and clear) the left-button presses seen since the last call. */
int mouse_take_lpresses(void) { int n = g_lpress; g_lpress = 0; return n; }

/* Fresh left-button state straight from the driver.  The cached state in
   mouse_update() is read BEFORE the press counter, so a press arriving
   between the two reads used to look like an already-released click and
   instantly cancelled the drag/resize it had just armed. */
bool_t mouse_left_now(void)
{
    union REGS r;
    if (!g_present)
        return FALSE;
    r.x.ax = 0x0003;
    int86(0x33, &r, &r);
    return (r.x.bx & MB_LEFT) ? TRUE : FALSE;
}

/* Take (and clear) the right-button presses seen since the last call. */
int mouse_take_rpresses(void) { int n = g_rpress; g_rpress = 0; return n; }

void mouse_get_state(int *x, int *y, int *buttons)
{
    if (x)       *x = g_x;
    if (y)       *y = g_y;
    if (buttons) *buttons = g_buttons;
}

void mouse_set_bounds(int x0, int y0, int x1, int y1)
{
    g_minx = x0; g_miny = y0;
    g_maxx = x1; g_maxy = y1;
    clamp_pos();
}

void mouse_show(void)        { g_visible = TRUE;  }
void mouse_hide(void)
{
    if (g_drawn) {
        mouse_erase();
    }
    g_visible = FALSE;
}
bool_t mouse_visible(void)   { return g_visible; }

/* Switch between the arrow and the hourglass.  The caller redraws. */
void mouse_set_busy(bool_t on) { g_busy = on ? TRUE : FALSE; }

/* The two cursors compiled to bitmask columns, built once from the ASCII
   art above: bit 15 is the leftmost pixel of the 12-wide cell.  Drawing
   used to cost CURSOR_W * CURSOR_H = 192 vga_pixel() calls per pointer
   move - the single most frequent screen operation in the shell - each a
   medium-model FAR call re-running four bound tests and the mode branch,
   and in Mode 12h reprogramming six VGA registers on top.  The art stays
   the source of truth; only its representation changed. */
static u16 g_arrow_out[CURSOR_H], g_arrow_fill[CURSOR_H];
static u16 g_hg_out[CURSOR_H],    g_hg_fill[CURSOR_H];
static bool_t g_masks_built = FALSE;

static void build_masks(void)
{
    int r, c;
    for (r = 0; r < CURSOR_H; ++r) {
        u16 ao = 0, af = 0, ho = 0, hf = 0;
        for (c = 0; c < CURSOR_W; ++c) {
            u16 bit = (u16)(0x8000U >> c);
            if (CURSOR[r][c] == 'X')      ao |= bit;
            else if (CURSOR[r][c] == '.') af |= bit;
            if (HGLASS[r][c] == 'X')      ho |= bit;
            else if (HGLASS[r][c] == '.') hf |= bit;
        }
        g_arrow_out[r] = ao; g_arrow_fill[r] = af;
        g_hg_out[r]    = ho; g_hg_fill[r]    = hf;
    }
    g_masks_built = TRUE;
}

void mouse_draw(void)
{
    if (!g_visible)
        return;
    if (!g_masks_built)
        build_masks();
    if (g_busy)
        vid_cursor(g_x, g_y, g_hg_out, g_hg_fill, CURSOR_H, C_BLACK, C_WHITE);
    else
        vid_cursor(g_x, g_y, g_arrow_out, g_arrow_fill, CURSOR_H,
                   C_BLACK, C_WHITE);
    g_last_x = g_x;
    g_last_y = g_y;
    g_drawn = TRUE;
}

void mouse_erase(void)
{
    if (!g_drawn)
        return;
    /* Repaint the scene rectangle that the cursor covered. */
    vid_blit_rect(g_last_x, g_last_y, CURSOR_W, CURSOR_H);
    g_drawn = FALSE;
}
