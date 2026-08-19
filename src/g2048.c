/* ======================================================================
 * g2048.c - 2048 for CASTALIA/386
 * ----------------------------------------------------------------------
 * The board holds EXPONENTS (1 = "2", 2 = "4", ... 11 = "2048"), so the
 * whole game state is sixteen bytes and the merge test is ==.  A slide
 * walks each line in the push direction, compacting and merging once per
 * pair, exactly by the original's rules.
 * ====================================================================== */
#include <stdio.h>
#include "g2048.h"
#include "video.h"
#include "font.h"
#include "system.h"
#include "keyboard.h"
#include "music.h"
#include "hiscore.h"

static unsigned char g_bd[16];         /* exponents, 0 = empty              */
static long g_score = 0;
static int  g_over  = 0;
static int  g_won   = 0;               /* 2048 reached (play continues)     */
static int  g_best  = 0;               /* the final score beat the record   */

static unsigned long g_seed = 0xC0FFEEUL;
static int rnd(int n)
{
    g_seed = g_seed * 1103515245UL + 12345UL;
    return (int)((g_seed >> 16) % (unsigned long)n);
}

/* Drop a 2 (or a lucky 4, one time in ten) on a random empty cell. */
static void spawn(void)
{
    int free[16], nf = 0, i;
    for (i = 0; i < 16; ++i)
        if (!g_bd[i]) free[nf++] = i;
    if (nf == 0)
        return;
    g_bd[free[rnd(nf)]] = (rnd(10) == 0) ? 2 : 1;
}

/* TRUE while any move remains (an empty cell, or an equal neighbour). */
static int can_move(void)
{
    int r, c;
    for (r = 0; r < 4; ++r)
        for (c = 0; c < 4; ++c) {
            int v = g_bd[r * 4 + c];
            if (!v) return 1;
            if (c < 3 && g_bd[r * 4 + c + 1] == v) return 1;
            if (r < 3 && g_bd[(r + 1) * 4 + c] == v) return 1;
        }
    return 0;
}

void g2048_open(void)
{
    int i;
    for (i = 0; i < 16; ++i) g_bd[i] = 0;
    g_score = 0; g_over = 0; g_won = 0; g_best = 0;
    g_seed ^= sys_ticks() | 1UL;
    spawn(); spawn();
}

/* Slide one line of four cells toward index 0; returns TRUE if it moved.
   The line is gathered through idx[] so the same code serves all four
   directions. */
static int slide_line(int i0, int i1, int i2, int i3)
{
    int idx[4], line[4], out[4], n = 0, i, k = 0, moved = 0;
    idx[0] = i0; idx[1] = i1; idx[2] = i2; idx[3] = i3;
    for (i = 0; i < 4; ++i)               /* compact the non-empty tiles    */
        if (g_bd[idx[i]]) line[n++] = g_bd[idx[i]];
    for (i = 0; i < 4; ++i) out[i] = 0;
    for (i = 0; i < n; ++i) {
        if (i + 1 < n && line[i] == line[i + 1]) {   /* merge once per pair */
            out[k] = line[i] + 1;
            g_score += 1L << out[k];
            if (out[k] == 11) g_won = 1;             /* 2^11 = 2048         */
            ++i;
        } else {
            out[k] = line[i];
        }
        ++k;
    }
    for (i = 0; i < 4; ++i) {
        if (g_bd[idx[i]] != out[i]) moved = 1;
        g_bd[idx[i]] = (unsigned char)out[i];
    }
    return moved;
}

static int slide(int key)
{
    int i, moved = 0;
    for (i = 0; i < 4; ++i) {
        if (key == KEY_LEFT)
            moved |= slide_line(i*4,   i*4+1, i*4+2, i*4+3);
        else if (key == KEY_RIGHT)
            moved |= slide_line(i*4+3, i*4+2, i*4+1, i*4);
        else if (key == KEY_UP)
            moved |= slide_line(i,     4+i,   8+i,   12+i);
        else
            moved |= slide_line(12+i,  8+i,   4+i,   i);
    }
    return moved;
}

/* One move in a direction, shared by the keyboard and the mouse.  The
   board was keyboard-only and the click handler discarded its
   coordinates, so 2048 could not be played with a mouse at all - on a
   machine where the arcade is otherwise mouse-first. */
static bool_t do_move(int key)
{
    int won_before = g_won;
    if (!slide(key))
        return FALSE;                   /* nothing moved: no spawn, no sound */
    spawn();
    music_sfx(g_won && !won_before ? 1319 : 660, 1);
    if (!can_move()) {
        g_over = 1; music_sfx(220, 3);
        /* Corral announces a record; 2048 threw the result away. */
        g_best = hiscore_submit("2048", g_score) ? 1 : 0;
    }
    return TRUE;
}

bool_t g2048_key(int key)
{

    if (key == KEY_F2) {          /* F2 = New Game, everywhere */
        g2048_open();
        return TRUE;
    }
    if (key == KEY_ENTER || key == KEY_SPACE) {
        if (g_over) { g2048_open(); return TRUE; }
        return FALSE;
    }
    if (g_over)
        return FALSE;
    if (key != KEY_LEFT && key != KEY_RIGHT &&
        key != KEY_UP   && key != KEY_DOWN)
        return FALSE;
    return do_move(key);
}

void g2048_click(const Rect *cl, int mx, int my)
{
    int cx, cy, dx, dy;
    if (g_over) {
        g2048_open();
        return;
    }
    /* Swipe-by-click: the direction is whichever side of the board's
       centre you clicked, taking the larger of the two offsets.  It reads
       naturally and needs no extra chrome. */
    cx = cl->x + cl->w / 2;
    cy = cl->y + cl->h / 2;
    dx = mx - cx;
    dy = my - cy;
    if (dx == 0 && dy == 0)
        return;
    if ((dx < 0 ? -dx : dx) > (dy < 0 ? -dy : dy))
        (void)do_move(dx > 0 ? KEY_RIGHT : KEY_LEFT);
    else
        (void)do_move(dy > 0 ? KEY_DOWN : KEY_UP);
}

/* Tile face colour by exponent (1..11); text goes black on the light low
   tiles and white from 16 up. */
static u8 tile_col(int v)
{
    static const u8 COL[12] = {
        C_FACE,                                       /* (empty)            */
        C_CREAM, C_YELLOW, C_DKYELLOW, C_RED, C_GREEN,
        C_CYAN, C_LTBLUE, C_BLUE, C_TITLE, C_DKGRAY, C_BLACK
    };
    return COL[(v >= 0 && v <= 11) ? v : 11];
}

void g2048_draw(const Rect *cl)
{
    char buf[48];                      /* two long scores + the 2048 flag  */
    int  gx, gy, cellw, cellh, r, c;
    int  top = cl->y + font_h() + 6;

    if (g_over && g_best)
        sprintf(buf, "New record!  %ld  (click)", g_score);
    else
        sprintf(buf, "Score %ld   Best %ld%s", g_score, hiscore_best("2048"),
                g_won ? "  2048!" : "");
    ui_text_center(cl->x, cl->y + 3, cl->w, buf,
                   (g_over && g_best) ? C_GREEN : (g_won ? C_GREEN : C_TITLE));

    cellw = (cl->w - 10) / 4;
    cellh = (cl->y + cl->h - top - 6) / 4;
    if (cellh < font_h() + 4) cellh = font_h() + 4;
    gx = cl->x + (cl->w - cellw * 4) / 2;
    gy = top;

    vid_fillrect(gx - 2, gy - 2, cellw * 4 + 4, cellh * 4 + 4, C_SHADOW);
    for (r = 0; r < 4; ++r)
        for (c = 0; c < 4; ++c) {
            int v = g_bd[r * 4 + c];
            int x = gx + c * cellw, y = gy + r * cellh;
            vid_fillrect(x + 1, y + 1, cellw - 2, cellh - 2, tile_col(v));
            if (v) {
                ui_raise(x + 1, y + 1, cellw - 2, cellh - 2);
                sprintf(buf, "%ld", 1L << v);
                ui_text_center(x, y + (cellh - font_h()) / 2, cellw, buf,
                               (v >= 4) ? C_WHITE : C_BLACK);
            }
        }

    if (g_over)
        ui_text_center(cl->x, gy + cellh * 4 - font_h() - 2, cl->w,
                       "No moves left - click for a new game", C_RED);
}
