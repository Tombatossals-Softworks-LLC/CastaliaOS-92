/* ======================================================================
 * puzzle.c - Sliding 15-puzzle minigame for CASTALIA/386
 * ====================================================================== */
#include <i86.h>
#include <stdio.h>
#include "puzzle.h"
#include "video.h"
#include "ui.h"
#include "keyboard.h"
#include "font.h"
#include "music.h"

static int      g_board[16];     /* 0 = the gap, 1..15 = tiles            */
static int      g_moves = 0;
static unsigned g_seed  = 1;

static unsigned long ticks(void)
{
    union REGS r;
    r.h.ah = 0x00;
    int86(0x1A, &r, &r);
    return ((unsigned long)r.x.cx << 16) | (unsigned long)r.x.dx;
}

static unsigned rnd(void)
{
    g_seed = (unsigned)(g_seed * 25173u + 13849u);
    return g_seed;
}

static int gap_pos(void)
{
    int i;
    for (i = 0; i < 16; ++i)
        if (g_board[i] == 0)
            return i;
    return 15;
}

void puzzle_open(void)
{
    int i, e = 15;
    for (i = 0; i < 16; ++i)
        g_board[i] = (i + 1) & 15;        /* 1..15 then 0                  */
    g_seed = (unsigned)ticks() | 1u;
    /* Shuffle by 400 random legal slides from solved -> always solvable.
       Use the HIGH bits of the LCG (its low bits have a tiny period, which
       would cycle the gap back to the start and leave the board solved). */
    for (i = 0; i < 400; ++i) {
        int er = e / 4, ec = e % 4, dir = (int)((rnd() >> 12) & 3);
        int nr = er, nc = ec, n;
        if      (dir == 0) --nr;
        else if (dir == 1) ++nr;
        else if (dir == 2) --nc;
        else               ++nc;
        if (nr < 0 || nr > 3 || nc < 0 || nc > 3)
            continue;
        n = nr * 4 + nc;
        g_board[e] = g_board[n];
        g_board[n] = 0;
        e = n;
    }
    g_moves = 0;
}

static bool_t solved(void)
{
    int i;
    for (i = 0; i < 15; ++i)
        if (g_board[i] != i + 1)
            return FALSE;
    return (g_board[15] == 0) ? TRUE : FALSE;
}

/* Board geometry within the client rectangle. */
static void geom(const Rect *cl, int *cell, int *gx, int *gy)
{
    int avail = (cl->w < cl->h ? cl->w : cl->h) - 12 - (font_h() + 8);
    int c = avail / 4;
    if (c < 8) c = 8;
    *cell = c;
    *gx = cl->x + (cl->w - c * 4) / 2;
    *gy = cl->y + 6;
}

void puzzle_draw(const Rect *cl)
{
    int cell, gx, gy, i;
    char buf[24];                      /* "Solved in 32767!" needs 17      */
    geom(cl, &cell, &gx, &gy);

    for (i = 0; i < 16; ++i) {
        int x = gx + (i % 4) * cell, y = gy + (i / 4) * cell;
        if (g_board[i] == 0)
            continue;
        ui_fill_face(x + 1, y + 1, cell - 2, cell - 2);
        ui_raise(x + 1, y + 1, cell - 2, cell - 2);
        sprintf(buf, "%d", g_board[i]);
        ui_text_center(x, y + (cell - font_h()) / 2, cell, buf, C_TITLE);
    }

    if (solved())
        sprintf(buf, "Solved in %d!", g_moves);
    else
        sprintf(buf, "Moves: %d", g_moves);
    ui_text_center(cl->x, gy + cell * 4 + 4, cl->w, buf,
                   solved() ? C_RED : C_BLACK);
}

void puzzle_click(const Rect *cl, int mx, int my)
{
    int cell, gx, gy, c, r, i, e, er, ec, ir, ic, dr, dc;
    geom(cl, &cell, &gx, &gy);
    c = (mx - gx) / cell;
    r = (my - gy) / cell;
    if (mx < gx || my < gy || c < 0 || c > 3 || r < 0 || r > 3)
        return;
    i = r * 4 + c;
    e = gap_pos();
    er = e / 4; ec = e % 4; ir = i / 4; ic = i % 4;
    dr = ir - er; if (dr < 0) dr = -dr;
    dc = ic - ec; if (dc < 0) dc = -dc;
    if (dr + dc == 1) {                    /* adjacent to the gap -> slide  */
        g_board[e] = g_board[i];
        g_board[i] = 0;
        ++g_moves;
        if (solved()) music_sfx(1047, 3);  /* a little fanfare when finished */
        else          music_sfx(600, 1);   /* a tile-slide tick             */
    }
}

/* Slide whichever tile sits in the given direction FROM the gap - so
   pressing Left slides the tile on the gap's right leftwards into it,
   which is how every sliding puzzle since 1880 has read.  Shared with
   the click path's rule: only a tile orthogonally adjacent to the gap
   can move. */
static bool_t slide_dir(int dr, int dc)
{
    int e = gap_pos();
    int er = e / 4, ec = e % 4;
    int nr = er - dr, nc = ec - dc, n;
    if (nr < 0 || nr > 3 || nc < 0 || nc > 3)
        return FALSE;
    n = nr * 4 + nc;
    g_board[e] = g_board[n];
    g_board[n] = 0;
    ++g_moves;
    if (solved()) music_sfx(1047, 3);      /* a little fanfare when done   */
    else          music_sfx(600, 1);       /* a tile-slide tick            */
    return TRUE;
}

/* F2 starts a fresh game, as in the other ten.  The arrows play it: a
   sliding-tile puzzle whose arrow keys did nothing simply read as broken,
   and the mouse is optional on this machine. */
bool_t puzzle_key(int key)
{
    if (key == KEY_F2) {
        puzzle_open();
        return TRUE;
    }
    if (key == KEY_LEFT)  return slide_dir(0, -1);
    if (key == KEY_RIGHT) return slide_dir(0,  1);
    if (key == KEY_UP)    return slide_dir(-1, 0);
    if (key == KEY_DOWN)  return slide_dir(1,  0);
    return FALSE;
}
