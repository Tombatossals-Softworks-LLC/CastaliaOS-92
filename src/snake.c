/* ======================================================================
 * snake.c - Serpent (a snake game) for CASTALIA/386
 * ====================================================================== */
#include <dos.h>
#include <stdio.h>
#include "snake.h"
#include "video.h"
#include "system.h"
#include "ui.h"
#include "font.h"
#include "keyboard.h"
#include "music.h"

#define GW 24
#define GH 16
#define GCELLS (GW * GH)
#define SPEED 3               /* BIOS ticks between moves (~6 moves/sec)   */

/* Body cells (head at index 0) in far memory. */
static unsigned char far s_x[GCELLS];
static unsigned char far s_y[GCELLS];
static int s_len, s_dir, s_ndir, s_fx, s_fy, s_over, s_score;
static unsigned long s_last;
static int s_vac_x = -1, s_vac_y = 0;  /* cell the tail just vacated (-1 none) */
static int s_need_full = 1;            /* a full redraw is required           */

static const int DX[4] = { 1, 0, -1, 0 };   /* R D L U */
static const int DY[4] = { 0, 1,  0, -1 };

static unsigned long ticks(void)
{
    return sys_ticks();               /* fast BDA read - see system.h      */
}
static unsigned long g_seed = 0x51ED7700UL;
static int rnd_below(int n)
{
    g_seed = g_seed * 1103515245UL + 12345UL;
    return (int)((g_seed >> 16) % (unsigned)n);
}

static int on_snake(int x, int y)
{
    int i;
    for (i = 0; i < s_len; ++i)
        if (s_x[i] == x && s_y[i] == y) return 1;
    return 0;
}
static void new_food(void)
{
    do { s_fx = rnd_below(GW); s_fy = rnd_below(GH); } while (on_snake(s_fx, s_fy));
}

void snake_open(void)
{
    int i;
    s_len = 5; s_dir = 0; s_ndir = 0; s_over = 0; s_score = 0;
    for (i = 0; i < s_len; ++i) { s_x[i] = (unsigned char)(10 - i); s_y[i] = 8; }
    g_seed ^= ticks() | 1UL;
    new_food();
    s_last = ticks();
    s_vac_x = -1;
    s_need_full = 1;
}

bool_t snake_key(int key)
{
    int nd = -1;
    if (key == KEY_F2) {          /* F2 = New Game, everywhere */
        snake_open();
        return TRUE;
    }
    if (key == KEY_RIGHT) nd = 0;
    else if (key == KEY_DOWN)  nd = 1;
    else if (key == KEY_LEFT)  nd = 2;
    else if (key == KEY_UP)    nd = 3;
    if (nd < 0) return FALSE;
    if ((nd + 2) % 4 != s_dir)          /* no instant reversal            */
        s_ndir = nd;
    return FALSE;
}

bool_t snake_tick(void)
{
    int nx, ny, i, grow;
    if (s_over) return FALSE;
    if (ticks() - s_last < (unsigned long)SPEED) return FALSE;
    s_last = ticks();

    s_dir = s_ndir;
    nx = s_x[0] + DX[s_dir];
    ny = s_y[0] + DY[s_dir];
    if (nx < 0 || nx >= GW || ny < 0 || ny >= GH) {
        s_over = 1; s_need_full = 1; music_sfx(140, 5); return TRUE;
    }
    grow = (nx == s_fx && ny == s_fy);
    for (i = 0; i < s_len - (grow ? 0 : 1); ++i)   /* self-collision       */
        if (s_x[i] == nx && s_y[i] == ny) {
            s_over = 1; s_need_full = 1; music_sfx(140, 5); return TRUE;
        }

    if (grow) s_vac_x = -1;                         /* no cell freed        */
    else { s_vac_x = s_x[s_len - 1]; s_vac_y = s_y[s_len - 1]; }

    for (i = (grow ? s_len : s_len - 1); i > 0; --i) {
        s_x[i] = s_x[i - 1]; s_y[i] = s_y[i - 1];
    }
    s_x[0] = (unsigned char)nx; s_y[0] = (unsigned char)ny;
    if (grow) {
        if (s_len < GCELLS) ++s_len;
        ++s_score;
        s_need_full = 1;               /* eat: full redraw (status + apple) */
        if (s_len >= GCELLS) {
            /* The serpent fills the whole board: a win.  Without this,
               new_food() would spin forever hunting for a free cell (and
               the next growth would write one slot past the body arrays). */
            s_over = 1;
            music_sfx(880, 4);
            return TRUE;
        }
        new_food();
        music_sfx((unsigned)(660 + (s_score % 12) * 30), 1);  /* rising blip */
    }
    return TRUE;
}

static void geom(const Rect *cl, int *cell, int *gx, int *gy)
{
    int cw = (cl->w - 8) / GW;
    int ch = (cl->h - (font_h() + 6)) / GH;
    int c = (cw < ch) ? cw : ch;
    if (c < 4) c = 4;
    *cell = c;
    *gx = cl->x + (cl->w - c * GW) / 2;
    *gy = cl->y + 2;
}

void snake_draw(const Rect *cl)
{
    int cell, gx, gy, i;
    char buf[36];                      /* the longest caption is 31 chars  */
    geom(cl, &cell, &gx, &gy);

    vid_fillrect(gx, gy, cell * GW, cell * GH, C_BLACK);
    vid_rect(gx - 1, gy - 1, cell * GW + 2, cell * GH + 2, C_DKGRAY);

    vid_fillrect(gx + s_fx * cell + 1, gy + s_fy * cell + 1,
                 cell - 2, cell - 2, C_RED);            /* apple            */

    for (i = 0; i < s_len; ++i)
        vid_fillrect(gx + s_x[i] * cell, gy + s_y[i] * cell, cell - 1, cell - 1,
                     i == 0 ? C_WHITE : C_GREEN);       /* head lighter     */

    if (s_over && s_len >= GCELLS)
                sprintf(buf, "Score %d  -  PERFECT! (click)", s_score);
    else if (s_over)
                sprintf(buf, "Score %d  -  crashed (click)", s_score);
    else        sprintf(buf, "Score %d  -  arrows to steer", s_score);
    ui_text_center(cl->x, gy + cell * GH + 2, cl->w, buf,
                   s_over ? C_RED : C_BLACK);
    s_need_full = 0;                    /* the whole board is now current    */
}

/* Incremental redraw: a normal move only changes three cells (the vacated
   tail, the old head now a body segment, and the new head), so redraw just
   those into the back buffer and report their bounding box for a tiny blit.
   Returns FALSE when a full snake_draw is needed (open, eat, crash). */
bool_t snake_step_draw(const Rect *cl, Rect *dirty)
{
    int cell, gx, gy, px, py;
    int minx = 32000, miny = 32000, maxx = -1, maxy = -1;
    if (s_need_full)
        return FALSE;
    geom(cl, &cell, &gx, &gy);

#define SNK_EXT(X, Y) do { if ((X) < minx) minx = (X); if ((Y) < miny) miny = (Y); \
        if ((X) + cell > maxx) maxx = (X) + cell; \
        if ((Y) + cell > maxy) maxy = (Y) + cell; } while (0)

    if (s_vac_x >= 0) {                            /* erase the freed tail  */
        px = gx + s_vac_x * cell; py = gy + s_vac_y * cell;
        vid_fillrect(px, py, cell, cell, C_BLACK);
        SNK_EXT(px, py);
    }
    if (s_len > 1) {                               /* old head -> green body */
        px = gx + s_x[1] * cell; py = gy + s_y[1] * cell;
        vid_fillrect(px, py, cell - 1, cell - 1, C_GREEN);
        SNK_EXT(px, py);
    }
    px = gx + s_x[0] * cell; py = gy + s_y[0] * cell;   /* new head -> white */
    vid_fillrect(px, py, cell - 1, cell - 1, C_WHITE);
    SNK_EXT(px, py);

#undef SNK_EXT
    rect_set(dirty, minx, miny, maxx - minx, maxy - miny);
    return TRUE;
}

void snake_click(const Rect *cl, int mx, int my)
{
    (void)cl; (void)mx; (void)my;
    if (s_over) snake_open();
}
