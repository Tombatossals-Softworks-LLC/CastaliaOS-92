/* ======================================================================
 * pong.c - Pong for CASTALIA/386
 * ----------------------------------------------------------------------
 * All geometry is derived from the client rectangle on every call, so the
 * court follows the window through moves and resizes.  Positions are kept
 * as PERMILLE of the court (0..1000) and mapped to pixels per frame; that
 * way a resize rescales the whole game instead of breaking it.
 * ====================================================================== */
#include <stdio.h>
#include "pong.h"
#include "video.h"
#include "font.h"
#include "system.h"
#include "keyboard.h"
#include "music.h"

#define WIN_SCORE 7
#define SPEED     1              /* BIOS ticks between advances (~18/s)     */
#define SUBSTEP   2              /* physics sub-steps per advance           */

/* Court state, in permille of the court's interior. */
static int  g_py  = 500;         /* player paddle centre (left)             */
static int  g_ay  = 500;         /* AI paddle centre (right)                */
static int  g_bx  = 500, g_by = 500;        /* ball centre                  */
static int  g_vx  = 14,  g_vy = 6;          /* ball velocity per substep    */
static int  g_sp  = 0,   g_sa  = 0;         /* score: player / AI           */
static int  g_serving = 1;       /* 1 = waiting for a serve                 */
static int  g_over    = 0;       /* someone reached WIN_SCORE               */
static unsigned long s_last = 0;

static unsigned long ticks(void) { return sys_ticks(); }

void pong_open(void)
{
    g_py = g_ay = 500;
    g_bx = g_by = 500;
    g_vx = 14; g_vy = 6;
    g_sp = g_sa = 0;
    g_serving = 1;
    g_over = 0;
    s_last = ticks();
}

/* The court interior: the client minus a score strip and a frame. */
static void court(const Rect *cl, Rect *c)
{
    c->x = cl->x + 3;
    c->y = cl->y + font_h() + 8;
    c->w = cl->w - 6;
    c->h = cl->h - font_h() - 12;
    if (c->w < 40) c->w = 40;
    if (c->h < 30) c->h = 30;
}

/* Paddle half-height in permille (bigger court -> same relative size). */
#define PAD_HALF 90

static void serve(int toward_player)
{
    g_bx = 500; g_by = 500;
    g_vx = toward_player ? -14 : 14;
    g_vy = (int)((ticks() % 13) - 6);    /* a little spin off the clock     */
    if (g_vy == 0) g_vy = 3;
    g_serving = 0;
}

static void substep(void)
{
    g_bx += g_vx;
    g_by += g_vy;

    /* Roof / floor. */
    if (g_by < 20)  { g_by = 20;  g_vy = -g_vy; music_sfx(440, 1); }
    if (g_by > 980) { g_by = 980; g_vy = -g_vy; music_sfx(440, 1); }

    /* The player's paddle face sits at x=30; the AI's at x=970.  The test
       covers the whole substep of travel (where the ball WAS to where it
       IS), because at full rally speed (26/substep) the ball can jump
       clean over the 23-permille hit window and tunnel the paddle. */
    if (g_vx < 0 && g_bx <= 40 && g_bx - g_vx >= 18 &&
        g_by >= g_py - PAD_HALF - 20 && g_by <= g_py + PAD_HALF + 20) {
        int off = g_by - g_py;               /* englishes the return        */
        g_bx = 40;
        g_vx = -g_vx + 1;                    /* each rally a touch faster   */
        if (g_vx > 26) g_vx = 26;
        g_vy = off / 8;
        music_sfx(880, 1);
    }
    if (g_vx > 0 && g_bx >= 960 && g_bx - g_vx <= 982 &&
        g_by >= g_ay - PAD_HALF - 20 && g_by <= g_ay + PAD_HALF + 20) {
        int off = g_by - g_ay;
        g_bx = 960;
        g_vx = -(g_vx + 1);
        if (g_vx < -26) g_vx = -26;
        g_vy = off / 8;
        music_sfx(880, 1);
    }

    /* Out at either end: score and set up the next serve. */
    if (g_bx < 0) {
        ++g_sa;  music_sfx(220, 2);
        if (g_sa >= WIN_SCORE) g_over = 1;
        g_serving = 1;
    } else if (g_bx > 1000) {
        ++g_sp;  music_sfx(1320, 2);
        if (g_sp >= WIN_SCORE) g_over = 1;
        g_serving = 1;
    }

    /* The house AI: follows the ball with a capped step and a lazy eye
       (it only really tracks while the ball is coming), so it is good -
       and beatable. */
    {
        int want = (g_vx > 0) ? g_by : 500;
        int step = 7;
        if (g_ay < want - step) g_ay += step;
        else if (g_ay > want + step) g_ay -= step;
        if (g_ay < PAD_HALF)        g_ay = PAD_HALF;
        if (g_ay > 1000 - PAD_HALF) g_ay = 1000 - PAD_HALF;
    }
}

bool_t pong_tick(const Rect *cl)
{
    int k;
    (void)cl;
    if (g_over || g_serving)
        return FALSE;
    if (ticks() - s_last < (unsigned long)SPEED)
        return FALSE;
    s_last = ticks();
    for (k = 0; k < SUBSTEP && !g_serving && !g_over; ++k)
        substep();
    return TRUE;
}

bool_t pong_mouse(const Rect *cl, int my)
{
    Rect c;
    int perm, old = g_py;
    court(cl, &c);
    if (c.h < 2) return FALSE;
    perm = (int)((long)(my - c.y) * 1000 / c.h);
    if (perm < PAD_HALF)        perm = PAD_HALF;
    if (perm > 1000 - PAD_HALF) perm = 1000 - PAD_HALF;
    g_py = perm;
    return (g_py != old) ? TRUE : FALSE;
}

bool_t pong_key(int key)
{
    if (key == KEY_F2) {          /* F2 = New Game, everywhere */
        pong_open();
        return TRUE;
    }
    if (key == KEY_UP)   { g_py -= 70; if (g_py < PAD_HALF) g_py = PAD_HALF; return TRUE; }
    if (key == KEY_DOWN) { g_py += 70; if (g_py > 1000 - PAD_HALF) g_py = 1000 - PAD_HALF; return TRUE; }
    if (key == KEY_SPACE || key == KEY_ENTER) {
        if (g_over) { pong_open(); return TRUE; }
        if (g_serving) { serve((g_sp + g_sa) & 1); return TRUE; }
    }
    return FALSE;
}

void pong_click(const Rect *cl, int mx, int my)
{
    (void)cl; (void)mx; (void)my;
    if (g_over)         { pong_open(); return; }
    if (g_serving)      serve((g_sp + g_sa) & 1);
}

/* A chunky 2x score digit, so the score reads across the room. */
static void big_char(int x, int y, char ch, u8 col)
{
    int rows, row, colb;
    const u8 far *g = font_glyph((unsigned char)ch, &rows);
    for (row = 0; row < rows; ++row) {
        u8 bits = g[row];
        for (colb = 0; colb < 8; ++colb)
            if (bits & (0x80 >> colb))
                vid_fillrect(x + colb * 2, y + row * 2, 2, 2, col);
    }
}

void pong_draw(const Rect *cl)
{
    Rect c;
    char b[8];
    int i, px, ax, pw, py, ay, ph, bx, by;

    court(cl, &c);

    /* Scores over the court. */
    vid_fillrect(cl->x, cl->y, cl->w, c.y - cl->y, C_FACE);
    sprintf(b, "%d", g_sp);
    big_char(cl->x + cl->w / 4, cl->y + 2, b[0], C_TITLE);
    sprintf(b, "%d", g_sa);
    big_char(cl->x + 3 * cl->w / 4 - 8, cl->y + 2, b[0], C_RED);

    /* The court. */
    vid_fillrect(c.x, c.y, c.w, c.h, C_BLACK);
    ui_sink(c.x, c.y, c.w, c.h);
    for (i = c.y + 3; i < c.y + c.h - 3; i += 8)         /* centre net      */
        vid_fillrect(c.x + c.w / 2 - 1, i, 2, 4, C_DKGRAY);

    /* Paddles and ball (permille -> pixels). */
    ph = (int)((long)PAD_HALF * 2 * c.h / 1000);
    if (ph < 8) ph = 8;
    pw = 3;
    px = c.x + 2;
    ax = c.x + c.w - 2 - pw;
    py = c.y + (int)((long)g_py * c.h / 1000) - ph / 2;
    ay = c.y + (int)((long)g_ay * c.h / 1000) - ph / 2;
    vid_fillrect(px, py, pw, ph, C_WHITE);
    vid_fillrect(ax, ay, pw, ph, C_CYAN);

    if (!g_serving && !g_over) {
        bx = c.x + (int)((long)g_bx * c.w / 1000);
        by = c.y + (int)((long)g_by * c.h / 1000);
        vid_fillrect(bx - 1, by - 1, 3, 3, C_YELLOW);
    }

    if (g_over)
        ui_text_center(c.x, c.y + c.h / 2 - font_h(), c.w,
                       (g_sp > g_sa) ? "YOU WIN!  Click for more"
                                     : "The house wins.  Click",
                       (g_sp > g_sa) ? C_GREEN : C_RED);
    else if (g_serving)
        ui_text_center(c.x, c.y + c.h / 2 - font_h(), c.w,
                       "Click (or Space) to serve", C_LTBLUE);
}
