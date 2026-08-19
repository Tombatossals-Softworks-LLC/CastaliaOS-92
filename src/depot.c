/* ======================================================================
 * depot.c - Depot (a box-pushing warehouse puzzle) for CASTALIA/386
 * ----------------------------------------------------------------------
 * Level cell legend (the classic text form):
 *     #  wall          .  target bay        $  crate
 *     *  crate on bay  @  worker            +  worker on bay
 * Every level below was verified solvable with a breadth-first search
 * before it was committed, so none of them is a trap.
 * ====================================================================== */
#include <stdio.h>
#include <string.h>
#include "depot.h"
#include "video.h"
#include "ui.h"
#include "font.h"
#include "keyboard.h"
#include "music.h"

#define DMAXW 10
#define DMAXH 8
#define DUNDO 250

/* Levels, easiest first (ordered by their BFS solution length). */
static const char far L01[] = "#####|#@$.#|#####";
static const char far L02[] = "######|#    #|#@$ .#|# $  #|# .  #|######";
static const char far L03[] = "########|#      #|# .$@$.#|#   $  #|#   .  #|#      #|########";
static const char far L04[] = "########|#  .   #|# @$   #|#      #|#   $. #|#      #|########";
static const char far L05[] = "#######|#     #|# $.$ #|# .@  #|#     #|#######";
static const char far L06[] = "######|#    #|# $..#|#@$  #|#    #|######";
static const char far L07[] = "#######|#.   .#|# $ $ #|#  @  #|# $   #|#.    #|#######";
static const char far L08[] = "#######|#.   .#|# $ $ #|#  @  #|# $ $ #|#.   .#|#######";
static const char far L09[] = "########|#  ##  #|# $..$ #|#  ##  #|# $..$ #|#  @   #|########";
static const char far L10[] = "########|##  ...#|# $$$  #|# @    #|########";

static const char far * const far D_LVL[] = {
    L01, L02, L03, L04, L05, L06, L07, L08, L09, L10
};
#define DN (int)(sizeof(D_LVL) / sizeof(D_LVL[0]))

/* The parsed floor: bit 1 = wall, bit 2 = target bay, bit 4 = crate. */
#define F_WALL 1
#define F_BAY  2
#define F_BOX  4
static u8 far d_map[DMAXW * DMAXH];
static int d_w, d_h;                   /* size of the current floor       */
static int d_px, d_py;                 /* the worker                      */
static int d_lvl;                      /* current level index             */
static int d_moves, d_pushes;
static int d_won;

/* Undo trail: direction (0..3) plus a "pushed a crate" flag in bit 2. */
static u8 far d_undo[DUNDO];
static int d_nundo;

static const int DDX[4] = { 1, -1, 0, 0 };
static const int DDY[4] = { 0, 0, 1, -1 };

static void d_load(int idx)
{
    const char far *s = D_LVL[idx];
    int x = 0, y = 0, i;
    for (i = 0; i < DMAXW * DMAXH; ++i)
        d_map[i] = F_WALL;             /* outside the level reads as wall */
    d_w = 0;
    for (; *s != '\0'; ++s) {
        char c = *s;
        if (c == '|') {
            if (x > d_w) d_w = x;
            x = 0; ++y;
            continue;
        }
        if (x < DMAXW && y < DMAXH) {
            u8 v = 0;
            if (c == '#')                        v = F_WALL;
            if (c == '.' || c == '*' || c == '+') v |= F_BAY;
            if (c == '$' || c == '*')             v |= F_BOX;
            if (c == '@' || c == '+') { d_px = x; d_py = y; }
            d_map[y * DMAXW + x] = v;
        }
        ++x;
    }
    if (x > d_w) d_w = x;
    d_h = y + 1;
    d_lvl = idx;
    d_moves = 0; d_pushes = 0; d_won = 0; d_nundo = 0;
}

void depot_open(void)
{
    d_load(0);
}

static int d_all_home(void)
{
    int i;
    for (i = 0; i < DMAXW * DMAXH; ++i)
        if ((d_map[i] & F_BOX) && !(d_map[i] & F_BAY))
            return 0;
    return 1;
}

static bool_t d_move(int dir)
{
    int nx = d_px + DDX[dir], ny = d_py + DDY[dir];
    u8 far *n = &d_map[ny * DMAXW + nx];
    if (*n & F_WALL)
        return FALSE;
    if (*n & F_BOX) {                  /* push the crate ahead            */
        int bx = nx + DDX[dir], by = ny + DDY[dir];
        u8 far *b = &d_map[by * DMAXW + bx];
        if (*b & (F_WALL | F_BOX))
            return FALSE;
        *n = (u8)(*n & ~F_BOX);
        *b |= F_BOX;
        ++d_pushes;
        if (d_nundo < DUNDO)
            d_undo[d_nundo++] = (u8)(dir | 4);
        music_sfx(240, 1);
    } else {
        if (d_nundo < DUNDO)
            d_undo[d_nundo++] = (u8)dir;
    }
    d_px = nx; d_py = ny;
    ++d_moves;
    if (d_all_home()) {
        d_won = 1;
        music_sfx(990, 3);
    }
    return TRUE;
}

static bool_t d_undo_move(void)
{
    int dir, pushed;
    if (d_nundo == 0)
        return FALSE;
    dir    = d_undo[--d_nundo] & 3;
    pushed = d_undo[d_nundo] & 4;
    if (pushed) {                      /* pull the crate back with us     */
        int bx = d_px + DDX[dir], by = d_py + DDY[dir];
        d_map[by * DMAXW + bx] = (u8)(d_map[by * DMAXW + bx] & ~F_BOX);
        d_map[d_py * DMAXW + d_px] |= F_BOX;
        --d_pushes;
    }
    d_px -= DDX[dir]; d_py -= DDY[dir];
    --d_moves;
    d_won = 0;
    return TRUE;
}

bool_t depot_key(int key)
{
    if (key == KEY_F2) {          /* F2 = New Game, everywhere */
        depot_open();
        return TRUE;
    }
    if (key == 'r' || key == 'R') { d_load(d_lvl); return TRUE; }
    if (key == 'n' || key == 'N') { d_load((d_lvl + 1) % DN); return TRUE; }
    if (key == 'p' || key == 'P') { d_load((d_lvl + DN - 1) % DN); return TRUE; }
    if (key == 'u' || key == 'U') return d_undo_move();
    if (d_won) {
        if (key == KEY_ENTER || key == KEY_SPACE) {
            d_load((d_lvl + 1) % DN);
            return TRUE;
        }
        return FALSE;
    }
    if (key == KEY_RIGHT) return d_move(0);
    if (key == KEY_LEFT)  return d_move(1);
    if (key == KEY_DOWN)  return d_move(2);
    if (key == KEY_UP)    return d_move(3);
    return FALSE;
}

void depot_click(const Rect *cl, int mx, int my)
{
    (void)cl; (void)mx; (void)my;
    if (d_won)
        d_load((d_lvl + 1) % DN);      /* click advances after a win      */
}

/* ---- drawing --------------------------------------------------------- */

void depot_draw(const Rect *cl)
{
    int cw = (cl->w - 8) / d_w;
    int ch = (cl->h - font_h() * 2 - 8) / d_h;
    int cell = (cw < ch) ? cw : ch;
    int ox, oy, x, y;
    char buf[44];                      /* 5-digit move counts still fit    */
    if (cell < 6) cell = 6;
    ox = cl->x + (cl->w - cell * d_w) / 2;
    oy = cl->y + 3;

    for (y = 0; y < d_h; ++y)
        for (x = 0; x < d_w; ++x) {
            u8 v  = d_map[y * DMAXW + x];
            int px = ox + x * cell, py = oy + y * cell;
            if (v & F_WALL) {
                vid_fillrect(px, py, cell, cell, C_SHADOW);
                vid_hline(px, py, cell, C_HILIGHT);
                vid_vline(px, py, cell, C_HILIGHT);
                vid_hline(px, py + cell - 1, cell, C_DKGRAY);
                vid_vline(px + cell - 1, py, cell, C_DKGRAY);
                continue;
            }
            vid_fillrect(px, py, cell, cell, C_BLACK);
            if (v & F_BAY)                       /* the marked bay        */
                vid_rect(px + cell / 3, py + cell / 3,
                         cell - 2 * (cell / 3), cell - 2 * (cell / 3),
                         C_RED);
            if (v & F_BOX) {                     /* a crate               */
                u8 c = (v & F_BAY) ? C_GREEN : C_DKYELLOW;
                int i, n = cell - 3;
                vid_fillrect(px + 1, py + 1, cell - 2, cell - 2, c);
                vid_rect(px + 1, py + 1, cell - 2, cell - 2, C_DKGRAY);
                for (i = 0; i < n; ++i) {        /* the strapping X       */
                    vid_pixel(px + 1 + i,     py + 1 + i, C_DKGRAY);
                    vid_pixel(px + 1 + n - i, py + 1 + i, C_DKGRAY);
                }
            }
        }

    /* The worker: head and overalls. */
    {
        int px = ox + d_px * cell, py = oy + d_py * cell;
        int q = cell / 4;
        vid_fillrect(px + q, py + 1, cell - 2 * q, q + 1, C_CREAM);     /* head */
        vid_fillrect(px + q, py + q + 2, cell - 2 * q, cell - q - 3, C_BLUE);
    }

    if (d_won)
        sprintf(buf, "Floor %d clear!  Enter: next", d_lvl + 1);
    else
        sprintf(buf, "Floor %d/%d  Moves %d  Push %d", d_lvl + 1, DN,
                d_moves, d_pushes);
    ui_text_center(cl->x, oy + d_h * cell + 3, cl->w, buf,
                   d_won ? C_GREEN : C_BLACK);
    ui_text_center(cl->x, oy + d_h * cell + 3 + font_h() + 1, cl->w,
                   "U undo  R retry  N/P floor", C_DKGRAY);
}
