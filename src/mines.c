/* ======================================================================
 * mines.c - Minefield (a minesweeper) for CASTALIA/386
 * ====================================================================== */
#include <dos.h>
#include <stdio.h>
#include "mines.h"
#include "video.h"
#include "system.h"
#include "ui.h"
#include "keyboard.h"
#include "font.h"
#include "music.h"

#define MW 9
#define MH 9
#define MCELLS (MW * MH)
#define MINES 10

/* Grids in far memory: DGROUP (near data) is nearly full shell-wide. */
static char far m_mine[MCELLS];
static char far m_open[MCELLS];
static char far m_flag[MCELLS];
static char far m_adj[MCELLS];

static int  g_lost, g_won, g_flagmode, g_placed;
static int  g_cur = 0;              /* the keyboard cursor's cell          */
static unsigned long g_t0 = 0;      /* tick the first dig happened         */
static unsigned      g_elapsed = 0; /* seconds, frozen when the game ends  */

static unsigned long g_seed = 0x1BADB002UL;
static int rnd_below(int n)
{
    g_seed = g_seed * 1103515245UL + 12345UL;
    return (int)((g_seed >> 16) % (unsigned)n);
}
static void seed_clock(void)
{
    g_seed ^= sys_ticks() | 1UL;
}

void mines_open(void)
{
    int i;
    for (i = 0; i < MCELLS; ++i) {
        m_mine[i] = 0; m_open[i] = 0; m_flag[i] = 0; m_adj[i] = 0;
    }
    g_lost = g_won = g_flagmode = g_placed = 0;
    g_cur = (MH / 2) * MW + MW / 2;
    g_t0 = 0;
    g_elapsed = 0;
    seed_clock();
}

static void place(int safe)
{
    int placed = 0, i, dx, dy;
    while (placed < MINES) {
        i = rnd_below(MCELLS);
        if (i == safe || m_mine[i]) continue;
        m_mine[i] = 1; ++placed;
    }
    for (i = 0; i < MCELLS; ++i) {
        int cx = i % MW, cy = i / MW, n = 0;
        for (dy = -1; dy <= 1; ++dy)
            for (dx = -1; dx <= 1; ++dx) {
                int x = cx + dx, y = cy + dy;
                if ((dx || dy) && x >= 0 && x < MW && y >= 0 && y < MH &&
                    m_mine[y * MW + x]) ++n;
            }
        m_adj[i] = (char)n;
    }
    g_placed = 1;
}

static void reveal(int i)
{
    int cx, cy, dx, dy;
    if (m_open[i] || m_flag[i]) return;
    m_open[i] = 1;
    if (m_mine[i]) { g_lost = 1; return; }
    if (m_adj[i] != 0) return;
    cx = i % MW; cy = i / MW;
    for (dy = -1; dy <= 1; ++dy)
        for (dx = -1; dx <= 1; ++dx) {
            int x = cx + dx, y = cy + dy;
            if ((dx || dy) && x >= 0 && x < MW && y >= 0 && y < MH)
                reveal(y * MW + x);
        }
}

static void check_win(void)
{
    int i, hidden = 0;
    for (i = 0; i < MCELLS; ++i)
        if (!m_open[i] && !m_mine[i]) ++hidden;
    if (hidden == 0) g_won = 1;
}

static int flags_used(void)
{
    int i, n = 0;
    for (i = 0; i < MCELLS; ++i) if (m_flag[i]) ++n;
    return n;
}

/* Layout: a top bar of two buttons, then the grid. */
static int bar_h(void) { return font_h() + 7; }

static void geom(const Rect *cl, int *cell, int *gx, int *gy)
{
    int avail_w = cl->w - 8;
    int avail_h = cl->h - bar_h() - 6;
    int c = avail_w / MW;
    if (avail_h / MH < c) c = avail_h / MH;
    if (c < 8) c = 8;
    *cell = c;
    *gx = cl->x + (cl->w - c * MW) / 2;
    *gy = cl->y + bar_h() + 2;
}

static void btn_rects(const Rect *cl, Rect *mode, Rect *nw)
{
    int y = cl->y + 2, h = bar_h() - 4;
    rect_set(mode, cl->x + 3, y, 46, h);
    rect_set(nw, cl->x + cl->w - 40, y, 37, h);
}

void mines_draw(const Rect *cl)
{
    int cell, gx, gy, i;
    Rect mode, nw;
    char buf[28];   /* "Mines 10   999 s" plus slack */
    static const u8 numcol[9] = {
        C_FACE, C_BLUE, C_GREEN, C_RED, C_TITLE, C_DKYELLOW, C_CYAN,
        C_BLACK, C_DKGRAY
    };
    geom(cl, &cell, &gx, &gy);
    btn_rects(cl, &mode, &nw);

    ui_button(&mode, g_flagmode ? "Flag" : "Dig", g_flagmode);
    ui_button(&nw, "New", FALSE);
    /* The counter went NEGATIVE past ten flags, and there was no clock at
       all - the one number every Minesweeper player expects. */
    {
        int left = MINES - flags_used();
        if (left < 0) left = 0;
        sprintf(buf, "%d  -  %us", left, g_elapsed);
    }
    /* Between the two buttons, NOT across the whole client: centring on
       cl->w ran the readout straight through "Dig" on the left and "New"
       on the right as soon as the clock was added. */
    {
        int lx = mode.x + mode.w + 2;
        int rx = nw.x - 2;
        if (rx > lx)
            ui_text_center(lx, cl->y + 3, rx - lx, buf, C_BLACK);
    }

    for (i = 0; i < MCELLS; ++i) {
        int x = gx + (i % MW) * cell, y = gy + (i / MW) * cell;
        int showmine = (g_lost && m_mine[i]);
        if (m_open[i] || showmine) {
            vid_fillrect(x, y, cell, cell, C_FACE);
            vid_rect(x, y, cell, cell, C_SHADOW);
            if (m_mine[i]) {
                int cxp = x + cell / 2, cyp = y + cell / 2, rr = cell / 2 - 3;
                if (m_open[i]) vid_fillrect(x + 1, y + 1, cell - 2, cell - 2, C_RED);
                vid_fillrect(cxp - rr, cyp - rr, rr * 2, rr * 2, C_BLACK);
            } else if (m_adj[i] > 0) {
                char t[2]; t[0] = (char)('0' + m_adj[i]); t[1] = '\0';
                ui_text_center(x, y + (cell - font_h()) / 2, cell, t,
                               numcol[(int)m_adj[i]]);
            }
        } else {
            ui_fill_face(x, y, cell, cell);
            ui_raise(x, y, cell, cell);
            if (m_flag[i]) {
                int px = x + cell / 2, py = y + 3;
                vid_vline(px, py, cell - 6, C_BLACK);
                vid_fillrect(px - 4, py, 4, 3, C_RED);
            }
        }
        if (i == g_cur && !g_lost && !g_won)   /* the keyboard cursor      */
            vid_rect(x, y, cell, cell, C_TITLE);
    }

    if (g_lost || g_won)
        ui_text_center(cl->x, gy + cell * MH + 1, cl->w,
                       g_won ? "Cleared!  (New)" : "Boom!  (New)", C_RED);
}

/* Flag / unflag, and dig - shared by the mouse and the keyboard so the
   two can never diverge. */
static void do_flag(int i)
{
    if (!m_open[i]) { m_flag[i] = (char)!m_flag[i]; music_sfx(500, 1); }
}

static void do_dig(int i)
{
    if (!g_placed) { place(i); g_t0 = sys_ticks(); }
    reveal(i);
    if (g_lost) {
        music_sfx(120, 6);                         /* boom                  */
    } else {
        check_win();
        music_sfx(g_won ? 1047 : 700, (unsigned char)(g_won ? 3 : 1));
    }
}

void mines_click(const Rect *cl, int mx, int my)
{
    int cell, gx, gy, c, r, i;
    Rect mode, nw;
    btn_rects(cl, &mode, &nw);
    if (rect_contains(&mode, mx, my)) { g_flagmode = !g_flagmode; return; }
    if (rect_contains(&nw, mx, my))   { mines_open(); return; }
    if (g_lost || g_won) return;

    geom(cl, &cell, &gx, &gy);
    c = (mx - gx) / cell; r = (my - gy) / cell;
    if (mx < gx || my < gy || c < 0 || c >= MW || r < 0 || r >= MH) return;
    i = r * MW + c;
    g_cur = i;                       /* keep the keyboard cursor in step   */
    if (g_flagmode) {
        do_flag(i);
    } else {
        if (m_flag[i]) return;
        do_dig(i);
    }
}

/* Right-click: plant or lift a flag, wherever the pointer is.  TRUE when
   the board changed. */
bool_t mines_rclick(const Rect *cl, int mx, int my)
{
    int cell, gx, gy, c, r;
    if (g_lost || g_won)
        return FALSE;
    geom(cl, &cell, &gx, &gy);
    c = (mx - gx) / cell; r = (my - gy) / cell;
    if (mx < gx || my < gy || c < 0 || c >= MW || r < 0 || r >= MH)
        return FALSE;
    g_cur = r * MW + c;
    do_flag(g_cur);
    return TRUE;
}

/* TRUE while the clock is running, so the shell repaints the status line
   once a second instead of leaving a frozen timer on screen. */
bool_t mines_tick(void)
{
    unsigned secs;
    if (g_t0 == 0 || g_lost || g_won)
        return FALSE;
    /* Returning TRUE every pass re-composed and re-blitted all 81
       bevelled cells 18 times a second for a number that changes once.
       Compute the readout here and report only a real change. */
    secs = (unsigned)(((sys_ticks() - g_t0) * 5UL) / 91UL);
    if (secs > 999) secs = 999;
    if (secs == g_elapsed)
        return FALSE;
    g_elapsed = secs;
    return TRUE;
}

/* F2 starts a fresh game.  Arrows walk the minefield, Enter digs and
   Space flags - Minesweeper without a keyboard is half a game, and the
   mouse is optional on this machine. */
bool_t mines_key(int key)
{
    if (key == KEY_F2) {
        mines_open();
        return TRUE;
    }
    /* Report a redraw only when the cursor actually moved: holding an
       arrow against the edge otherwise repaints the whole client at the
       key-repeat rate. */
    switch (key) {
    case KEY_LEFT:
        if (g_cur % MW == 0) return FALSE;
        --g_cur;            return TRUE;
    case KEY_RIGHT:
        if (g_cur % MW == MW - 1) return FALSE;
        ++g_cur;            return TRUE;
    case KEY_UP:
        if (g_cur < MW) return FALSE;
        g_cur -= MW;        return TRUE;
    case KEY_DOWN:
        if (g_cur >= MCELLS - MW) return FALSE;
        g_cur += MW;        return TRUE;
    case KEY_ENTER:
        if (!g_lost && !g_won && !m_flag[g_cur]) do_dig(g_cur);
        return TRUE;
    case ' ':
        if (!g_lost && !g_won) do_flag(g_cur);
        return TRUE;
    default:
        break;
    }
    return FALSE;
}
