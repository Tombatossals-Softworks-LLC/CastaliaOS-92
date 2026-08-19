/* ======================================================================
 * corral.c - Corral (a wall-building ball-capture game) for CASTALIA/386
 * ----------------------------------------------------------------------
 * The beloved early-90s arcade puzzle: balls carom around a walled pen
 * and you claim the floor out from under them.  A click plants a fence
 * post that grows in BOTH directions (vertically or horizontally - Space
 * flips the axis); when an arm reaches solid ground it hardens, and any
 * pen left without a ball inside is instantly bricked over.  Corral 75%
 * of the floor to advance; a ball striking a still-growing arm knocks
 * the whole fence down and costs a life.
 *
 * Board cells live in FAR memory (DGROUP is on a diet); everything is
 * integer maths - ball positions are Q4 fixed point in cell units, so a
 * window resize just rescales the same game, like Pong's permille court.
 * ====================================================================== */
#include <stdio.h>
#include "corral.h"
#include "video.h"
#include "font.h"
#include "system.h"
#include "ui.h"
#include "keyboard.h"
#include "music.h"
#include "hiscore.h"

#define GW 30                  /* board cells, including the border ring   */
#define GH 19
#define INTERIOR ((GW - 2) * (GH - 2))
#define TARGET   75            /* percent of the interior to claim         */
#define MAXB     6             /* balls at the deepest level               */
#define BR       5             /* ball radius in Q4 cell units (16 = cell) */

/* Cell states. */
#define CE_OPEN  0
#define CE_WALL  1
#define CE_ARM_A 2             /* growing arm (up / left)                  */
#define CE_ARM_B 3             /* growing arm (down / right)               */
#define CE_MARK  4             /* flood-fill scratch: reachable by a ball  */

/* The floor, and the flood-fill queue, both far: ~1.7 KB off DGROUP. */
static u8  far g_bd[GH][GW];
static u16 far g_q[GW * GH];

static int  g_level, g_lives, g_nballs;
static long g_score;           /* long: the hiscore store speaks long      */
static int  g_claimed;         /* interior cells turned to wall            */
static int  g_state;           /* 0 playing, 1 level clear, 2 game over    */
static int  g_newbest;         /* game over set a fresh record             */

/* Balls: Q4 positions and velocities in cell units. */
static int  g_bx[MAXB], g_by[MAXB], g_vx[MAXB], g_vy[MAXB];

/* The growing fence. */
static int  g_won;             /* a fence is growing                       */
static int  g_wor;             /* NEXT fence's axis: 0 vertical, 1 horiz   */
static int  g_wdir;            /* the ACTIVE fence's axis, frozen at the
                                  plant - flipping the aim mid-growth must
                                  never bend an arm that is already out    */
static int  g_wcx, g_wcy;      /* the planted post                         */
static int  g_la, g_lb;        /* arm lengths beyond the post              */
static int  g_da, g_db;        /* arm anchored ("done") flags              */

/* Mouse hover cell for the aiming guide (-1 = pointer elsewhere). */
static int  g_hx = -1, g_hy = -1;

/* The axis-flip button, laid out by the draw for the click handler. */
static Rect g_axis_b;

static unsigned long s_last;
static unsigned long ticks(void) { return sys_ticks(); }

static unsigned long g_seed = 0xC0881A11UL;
static int rnd_below(int n)
{
    g_seed = g_seed * 1103515245UL + 12345UL;
    return (int)((g_seed >> 16) % (unsigned)n);
}

/* ---- level setup ------------------------------------------------------ */

static void level_start(void)
{
    int x, y, i;
    for (y = 0; y < GH; ++y)
        for (x = 0; x < GW; ++x)
            g_bd[y][x] = (u8)((x == 0 || y == 0 ||
                               x == GW - 1 || y == GH - 1) ? CE_WALL
                                                           : CE_OPEN);
    g_claimed = 0;
    g_won     = 0;
    g_nballs  = 1 + g_level;
    if (g_nballs > MAXB) g_nballs = MAXB;
    for (i = 0; i < g_nballs; ++i) {
        int spd = 4 + g_level / 3;
        if (spd > 6) spd = 6;
        g_bx[i] = (2 + rnd_below(GW - 4)) * 16 + 8;
        g_by[i] = (2 + rnd_below(GH - 4)) * 16 + 8;
        g_vx[i] = rnd_below(2) ? spd : -spd;
        g_vy[i] = rnd_below(2) ? spd : -spd;
    }
    g_state = 0;
    s_last  = ticks();
}

void corral_open(void)
{
    g_level   = 1;
    g_score   = 0;
    g_lives   = 3;
    g_newbest = 0;
    g_wor     = 0;
    g_hx = g_hy = -1;
    g_seed ^= ticks() | 1UL;
    level_start();
}

/* ---- the growing fence ------------------------------------------------ */

static void fence_fall(void)
{
    int x, y;
    for (y = 1; y < GH - 1; ++y)
        for (x = 1; x < GW - 1; ++x)
            if (g_bd[y][x] == CE_ARM_A || g_bd[y][x] == CE_ARM_B)
                g_bd[y][x] = CE_OPEN;
    g_won = 0;
}

/* Re-count the interior claim (arms harden one at a time, and a fallen
   fence keeps whatever already hardened - so count, never accumulate). */
static void recount_claim(void)
{
    int x, y;
    g_claimed = 0;
    for (y = 1; y < GH - 1; ++y)
        for (x = 1; x < GW - 1; ++x)
            if (g_bd[y][x] == CE_WALL) ++g_claimed;
}

/* Turn one arm's cells to solid wall (post included with arm A). */
static void arm_harden(int which)
{
    int k;
    if (g_wdir == 0) {
        if (which == 0)
            for (k = 0; k <= g_la; ++k) g_bd[g_wcy - k][g_wcx] = CE_WALL;
        else
            for (k = 1; k <= g_lb; ++k) g_bd[g_wcy + k][g_wcx] = CE_WALL;
    } else {
        if (which == 0)
            for (k = 0; k <= g_la; ++k) g_bd[g_wcy][g_wcx - k] = CE_WALL;
        else
            for (k = 1; k <= g_lb; ++k) g_bd[g_wcy][g_wcx + k] = CE_WALL;
    }
    recount_claim();
}

/* Flood from every ball across open floor; unreached open cells become
   wall (the pen had no ball in it).  An iterative queue - never recursion,
   the 8 KB stack is shared with the whole shell. */
static void capture_pens(void)
{
    int i, x, y, head = 0, tail = 0, gained = 0;
    for (i = 0; i < g_nballs; ++i) {
        int cx = g_bx[i] / 16, cy = g_by[i] / 16;
        if (cx < 0 || cx >= GW || cy < 0 || cy >= GH)
            continue;                  /* same implicit invariant as above */
        if (g_bd[cy][cx] == CE_OPEN) {
            g_bd[cy][cx] = CE_MARK;
            g_q[tail++] = (u16)(cy * GW + cx);
        }
    }
    while (head < tail) {
        unsigned c = g_q[head++];
        x = (int)(c % GW); y = (int)(c / GW);
        if (x > 0        && g_bd[y][x - 1] == CE_OPEN) {
            g_bd[y][x - 1] = CE_MARK; g_q[tail++] = (u16)(c - 1);
        }
        if (x < GW - 1   && g_bd[y][x + 1] == CE_OPEN) {
            g_bd[y][x + 1] = CE_MARK; g_q[tail++] = (u16)(c + 1);
        }
        if (y > 0        && g_bd[y - 1][x] == CE_OPEN) {
            g_bd[y - 1][x] = CE_MARK; g_q[tail++] = (u16)(c - GW);
        }
        if (y < GH - 1   && g_bd[y + 1][x] == CE_OPEN) {
            g_bd[y + 1][x] = CE_MARK; g_q[tail++] = (u16)(c + GW);
        }
    }
    for (y = 1; y < GH - 1; ++y)
        for (x = 1; x < GW - 1; ++x) {
            if (g_bd[y][x] == CE_OPEN) { g_bd[y][x] = CE_WALL; ++gained; }
            else if (g_bd[y][x] == CE_MARK) g_bd[y][x] = CE_OPEN;
        }
    if (gained > 0) {
        g_score += gained;
        music_sfx(988, 2);                       /* the pen bricks over    */
    }

    recount_claim();

    if ((long)g_claimed * 100 >= (long)INTERIOR * TARGET) {
        g_score += g_lives * 50;                 /* spare lives pay out    */
        g_state  = 1;
        music_sfx(1320, 4);
    }
}

/* One growth step for each live arm; TRUE if anything changed. */
static bool_t fence_grow(void)
{
    bool_t changed = FALSE;
    if (!g_won)
        return FALSE;
    if (!g_da) {
        int nx = g_wcx, ny = g_wcy;
        if (g_wdir == 0) ny = g_wcy - g_la - 1; else nx = g_wcx - g_la - 1;
        if (g_bd[ny][nx] == CE_OPEN) {
            g_bd[ny][nx] = CE_ARM_A; ++g_la;
        } else if (g_bd[ny][nx] == CE_WALL) {
            g_da = 1; arm_harden(0); music_sfx(660, 1);
        }
        changed = TRUE;
    }
    if (!g_db) {
        int nx = g_wcx, ny = g_wcy;
        if (g_wdir == 0) ny = g_wcy + g_lb + 1; else nx = g_wcx + g_lb + 1;
        if (g_bd[ny][nx] == CE_OPEN) {
            g_bd[ny][nx] = CE_ARM_B; ++g_lb;
        } else if (g_bd[ny][nx] == CE_WALL) {
            g_db = 1; arm_harden(1); music_sfx(660, 1);
        }
        changed = TRUE;
    }
    if (g_da && g_db) {
        g_won = 0;
        capture_pens();
    }
    return changed;
}

/* ---- balls ------------------------------------------------------------ */

static bool_t cell_solid(int cx, int cy)
{
    return (g_bd[cy][cx] == CE_WALL) ? TRUE : FALSE;
}

/* Is this cell under any ball right now?  A post planted beneath a ball
   could anchor and harden in its very first tick - before the collision
   test ever sees it - entombing the ball in solid wall.  Refuse it. */
static bool_t cell_touches_ball(int cx, int cy)
{
    int i;
    for (i = 0; i < g_nballs; ++i) {
        int x0 = (g_bx[i] - BR) / 16, x1 = (g_bx[i] + BR) / 16;
        int y0 = (g_by[i] - BR) / 16, y1 = (g_by[i] + BR) / 16;
        if (cx >= x0 && cx <= x1 && cy >= y0 && cy <= y1)
            return TRUE;
    }
    return FALSE;
}

/* Any covered cell still a growing arm?  (Solid cells already bounced.) */
static bool_t touches_arm(int bx, int by)
{
    int x0 = (bx - BR) / 16, x1 = (bx + BR) / 16;
    int y0 = (by - BR) / 16, y1 = (by + BR) / 16;
    int x, y;
    /* The border ring is solid, so physics keeps these in range - but that
       is an implicit invariant guarding a far 2-D read.  One tweak to BR or
       to the serve speed and this walks off the end of the board. */
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 >= GW) x1 = GW - 1;
    if (y1 >= GH) y1 = GH - 1;
    for (y = y0; y <= y1; ++y)
        for (x = x0; x <= x1; ++x)
            if (g_bd[y][x] == CE_ARM_A || g_bd[y][x] == CE_ARM_B)
                return TRUE;
    return FALSE;
}

static void ball_move(int i)
{
    int nx = g_bx[i] + g_vx[i];
    int ny = g_by[i] + g_vy[i];
    int lead, c0, c1;

    /* X axis: probe the edge the ball is moving toward. */
    lead = nx + ((g_vx[i] > 0) ? BR : -BR);
    c0 = (g_by[i] - BR) / 16; c1 = (g_by[i] + BR) / 16;
    if (cell_solid(lead / 16, c0) || cell_solid(lead / 16, c1))
        g_vx[i] = -g_vx[i];
    else
        g_bx[i] = nx;

    /* Y axis. */
    lead = ny + ((g_vy[i] > 0) ? BR : -BR);
    c0 = (g_bx[i] - BR) / 16; c1 = (g_bx[i] + BR) / 16;
    if (cell_solid(c0, lead / 16) || cell_solid(c1, lead / 16))
        g_vy[i] = -g_vy[i];
    else
        g_by[i] = ny;

    if (g_won && touches_arm(g_bx[i], g_by[i])) {
        fence_fall();                            /* the fence comes down   */
        music_sfx(150, 5);
        if (--g_lives <= 0) {
            g_state = 2;
            g_newbest = hiscore_submit("corral", (long)g_score) ? 1 : 0;
        }
    }
}

/* ---- the applet entry points ------------------------------------------ */

bool_t corral_tick(const Rect *cl)
{
    int i;
    (void)cl;
    if (g_state != 0)
        return FALSE;
    if (ticks() - s_last < 1UL)
        return FALSE;
    s_last = ticks();
    fence_grow();
    for (i = 0; i < g_nballs && g_state == 0; ++i)
        ball_move(i);
    return TRUE;
}

/* ---- geometry ---------------------------------------------------------- */

static void geom(const Rect *cl, int *cell, int *gx, int *gy)
{
    int strip = font_h() + 6;
    int cw = (cl->w - 6) / GW;
    int ch = (cl->h - strip - 4) / GH;
    int c  = (cw < ch) ? cw : ch;
    if (c < 4) c = 4;
    *cell = c;
    *gx = cl->x + (cl->w - c * GW) / 2;
    *gy = cl->y + 2;
}

static int percent_claimed(void)
{
    return (int)((long)g_claimed * 100 / INTERIOR);
}

void corral_draw(const Rect *cl)
{
    int cell, gx, gy, x, y, i;
    char buf[40];
    geom(cl, &cell, &gx, &gy);

    /* Claimed ground arrives in big contiguous blocks, so run-length the
       rows: late in a level ~475 of the 476 interior cells are wall, and
       painting them one at a time cost three clipped primitive calls each
       - about 1425 round trips through video.c every frame at 18 Hz.  A
       run of n cells is one fillrect plus n grout strokes instead of n
       fillrects plus the same strokes. */
    vid_fillrect(gx, gy, cell * GW, cell * GH, C_BLACK);
    for (y = 0; y < GH; ++y) {
        int py = gy + y * cell;
        x = 0;
        while (x < GW) {
            u8  c   = g_bd[y][x];
            int run = 1;
            int px  = gx + x * cell;
            if (c != CE_WALL && c != CE_ARM_A && c != CE_ARM_B) { ++x; continue; }
            while (x + run < GW && g_bd[y][x + run] == c)
                ++run;
            if (c == CE_WALL) {
                int k;
                vid_fillrect(px, py, cell * run, cell, C_FACE);
                vid_hline(px, py + cell - 1, cell * run, C_SHADOW);
                for (k = 0; k < run; ++k)      /* the vertical grout lines */
                    vid_vline(px + k * cell + cell - 1, py, cell, C_SHADOW);
            } else {
                vid_fillrect(px, py, cell * run, cell,
                             (c == CE_ARM_A) ? C_RED : C_CYAN);
            }
            x += run;
        }
    }

    /* The aiming guide: a slim axis hint under the pointer. */
    if (g_state == 0 && !g_won &&
        g_hx > 0 && g_hx < GW - 1 && g_hy > 0 && g_hy < GH - 1 &&
        g_bd[g_hy][g_hx] == CE_OPEN) {
        int px = gx + g_hx * cell, py = gy + g_hy * cell;
        if (g_wor == 0)
            vid_vline(px + cell / 2, py - cell / 2, cell * 2, C_YELLOW);
        else
            vid_hline(px - cell / 2, py + cell / 2, cell * 2, C_YELLOW);
    }

    for (i = 0; i < g_nballs; ++i) {
        int r  = (BR * cell) / 16;
        int px = gx + (g_bx[i] * cell) / 16;
        int py = gy + (g_by[i] * cell) / 16;
        if (r < 2) r = 2;
        vid_fillrect(px - r, py - r, r * 2, r * 2, C_WHITE);
        vid_hline(px - r + 1, py - r, r * 2 - 2, C_LTBLUE);
        vid_vline(px - r, py - r + 1, r * 2 - 2, C_LTBLUE);
        vid_pixel(px - r / 2, py - r / 2, C_CREAM);
    }

    if (g_state == 1)
        ui_text_center(cl->x, gy + (GH / 2) * cell - font_h(), cl->w,
                       "PEN CLEARED!  Click for the next", C_GREEN);
    else if (g_state == 2)
        ui_text_center(cl->x, gy + (GH / 2) * cell - font_h(), cl->w,
                       g_newbest ? "GAME OVER - a new record! (click)"
                                 : "GAME OVER  (click to lasso again)",
                       C_RED);

    /* The axis switch lives in the strip's right corner - click to flip. */
    {
        int bw = font_adv() * 3 + 6;
        rect_set(&g_axis_b, cl->x + cl->w - bw - 4,
                 gy + cell * GH + 1, bw, font_h() + 4);
        ui_button(&g_axis_b, (g_wor == 0) ? "|" : "--", FALSE);
    }

    sprintf(buf, "Lv%d  %d%%/%d%%  Lives %d  %ld",
            g_level, percent_claimed(), TARGET, g_lives, g_score);
    ui_text_center(cl->x, gy + cell * GH + 3, cl->w - g_axis_b.w - 8,
                   buf, C_BLACK);
}

bool_t corral_mouse(const Rect *cl, int mx, int my)
{
    int cell, gx, gy, hx, hy;
    geom(cl, &cell, &gx, &gy);
    hx = (mx - gx) / cell;
    hy = (my - gy) / cell;
    if (mx < gx || my < gy || hx >= GW || hy >= GH) { hx = -1; hy = -1; }
    if (hx == g_hx && hy == g_hy)
        return FALSE;
    g_hx = hx; g_hy = hy;
    return (g_state == 0 && !g_won) ? TRUE : FALSE;
}

void corral_click(const Rect *cl, int mx, int my)
{
    int cell, gx, gy, cx, cy;
    if (g_state == 1) { ++g_level; level_start(); return; }
    if (g_state == 2) { corral_open(); return; }
    geom(cl, &cell, &gx, &gy);

    /* The axis switch. */
    if (rect_contains(&g_axis_b, mx, my)) {
        g_wor = !g_wor;
        return;
    }
    if (g_won || mx < gx || my < gy)
        return;
    cx = (mx - gx) / cell;
    cy = (my - gy) / cell;
    if (cx <= 0 || cx >= GW - 1 || cy <= 0 || cy >= GH - 1)
        return;
    if (g_bd[cy][cx] != CE_OPEN || cell_touches_ball(cx, cy))
        return;
    g_bd[cy][cx] = CE_ARM_A;                     /* the post               */
    g_wcx  = cx;  g_wcy = cy;
    g_la = 0;     g_lb = 0;
    g_da = 0;     g_db = 0;
    g_wdir = g_wor;              /* freeze this fence's axis at the plant  */
    g_won  = 1;
    music_sfx(523, 1);
}

bool_t corral_key(int key)
{
    if (key == KEY_F2) {          /* F2 = New Game, everywhere */
        corral_open();
        return TRUE;
    }
    if (key == KEY_SPACE || key == KEY_TAB) {
        if (g_state == 1)      { ++g_level; level_start(); }
        else if (g_state == 2) corral_open();
        else                   g_wor = !g_wor;
        return TRUE;
    }
    if (key == KEY_ENTER) {
        if (g_state == 1)      { ++g_level; level_start(); return TRUE; }
        if (g_state == 2)      { corral_open(); return TRUE; }
    }
    return FALSE;
}
