/* ======================================================================
 * breaker.c - Breaker (a block-breaker game) for CASTALIA/386
 * ----------------------------------------------------------------------
 * All state lives in FIELD-pixel coordinates relative to the play field's
 * top-left corner; geom() re-derives that field from the (fixed) window
 * client on every call, so the tick, the full draw and the incremental
 * draw always agree.  A normal frame only moves the ball and the paddle,
 * so breaker_step_draw() erases and redraws just those two and reports a
 * tight bounding box - the bricks (which change only on a hit) are left
 * untouched.  A hit, a lost ball, a win or a restart raise s_need_full so
 * the next frame is a full breaker_draw() (bricks + status included).
 * ====================================================================== */
#include <dos.h>
#include <stdio.h>
#include "breaker.h"
#include "video.h"
#include "system.h"
#include "ui.h"
#include "font.h"
#include "keyboard.h"
#include "music.h"
#include "hiscore.h"

#define BCOLS   9
#define BROWS   5
#define NBRICK  (BCOLS * BROWS)
#define BALLSZ  4                 /* ball is a 4x4 square                    */
#define SUBSTEP 2                 /* physics sub-steps per tick (smoother)   */
#define SPEED   1                 /* BIOS ticks between advances (~18/sec)   */
#define BALLS0  3                 /* balls (lives) at the start of a game    */

/* Bricks live in far memory (DGROUP is nearly full); 0 = smashed,
   1 = normal, 2 = hardened (takes two hits). */
static unsigned char far s_brick[NBRICK];

static int s_bx, s_by;            /* ball top-left, field coords             */
static int s_vx, s_vy;            /* ball velocity, per sub-step             */
static int s_px;                  /* paddle left, field coords               */
static int s_left;                /* bricks remaining                        */
static long s_score;              /* long: a marathon run wraps an int       */
static int s_balls, s_over;
/* Set when the final score beat the stored record.  It replaces a dead
   s_won flag that was cleared in breaker_open and never assigned anywhere
   else, so the "you won" colour branch below could never be taken - and
   Breaker, unlike Corral, threw away hiscore_submit's result and could
   never congratulate the player at all. */
static int s_best;
static int s_level;               /* 1..n - each level a new wall, faster    */

/* Falling power-up capsule (one at a time).  A smashed brick sometimes
   drops one; catch it on the paddle for its effect. */
#define CAPW  11
#define CAPH  7
#define CAP_WIDE  0               /* a wider paddle for a while              */
#define CAP_BALL  1               /* an extra ball, at once                  */
#define CAP_SLOW  2               /* the ball slows for a while              */
static int s_cap_on;              /* a capsule is falling                    */
static int s_cap_x, s_cap_y;      /* capsule top-left, field coords          */
static int s_cap_type;
static int s_wide;                /* ticks of wide paddle remaining          */
static int s_slow;                /* ticks of slow ball remaining            */
static int s_fresh;               /* geometry-dependent init still pending   */
static int s_need_full;           /* a full redraw is required               */
static unsigned long s_last;

/* Where the ball / paddle were last painted (for incremental erase). */
static int s_dbx, s_dby, s_dpx;

/* Brick colours by row, top to bottom: a red->cyan rainbow. */
static const unsigned char s_row_col[BROWS] = {
    C_RED, C_DKYELLOW, C_YELLOW, C_GREEN, C_CYAN
};

typedef struct {
    int ox, oy;      /* field origin (top-left) on screen        */
    int fw, fh;      /* field size                               */
    int bw, bh;      /* brick cell size                          */
    int btop;        /* brick block top (field-relative y)       */
    int pw, ph, py;  /* paddle width/height and field-relative y */
} BGeom;

static unsigned long ticks(void)
{
    return sys_ticks();               /* fast BDA read - see system.h      */
}
static unsigned long g_seed = 0x1BADF00DUL;
static int coin(void)
{
    g_seed = g_seed * 1103515245UL + 12345UL;
    return (int)((g_seed >> 20) & 1);
}

static void clamp_paddle(const BGeom *g);       /* defined with the draws  */

static void geom(const Rect *cl, BGeom *g)
{
    int statush = font_h() + 4;
    g->ox   = cl->x + 4;
    g->oy   = cl->y + 3;
    g->fw   = cl->w - 8;
    g->fh   = cl->h - 6 - statush;
    g->bw   = g->fw / BCOLS;
    g->bh   = 9;
    g->btop = 8;
    g->pw   = s_wide ? (g->fw / 3) : (g->fw / 5);   /* the WIDE power-up    */
    g->ph   = 5;
    g->py   = g->fh - 10;
}

/* Serve the ball just above the paddle, heading up at a shallow angle. */
static void serve(const BGeom *g)
{
    s_px = (g->fw - g->pw) / 2;
    s_bx = s_px + g->pw / 2 - BALLSZ / 2;
    s_by = g->py - BALLSZ - 1;
    s_vx = coin() ? 2 : -2;
    s_vy = -2;
}

/* The wall pattern for a level: 1 = a brick in this cell, 0 = a gap.  Six
   classic Arkanoid shapes cycle as the levels climb. */
static int wall_at(int level, int r, int c)
{
    switch ((level - 1) % 6) {
    case 0:  return 1;                                     /* the full wall */
    case 1:  return ((r + c) & 1) ? 0 : 1;                 /* checkerboard  */
    case 2:  return (c >= r && c <= BCOLS - 1 - r) ? 1 : 0;/* pyramid       */
    case 3:  return (c & 1) ? 0 : 1;                       /* columns       */
    case 4:  {                                             /* diamond       */
        int dr = (r > BROWS / 2) ? r - BROWS / 2 : BROWS / 2 - r;
        int dc = (c > BCOLS / 2) ? c - BCOLS / 2 : BCOLS / 2 - c;
        return (dr + dc <= BCOLS / 2) ? 1 : 0;
    }
    default: return (r == 0 || r == BROWS - 1 ||
                     c == 0 || c == BCOLS - 1) ? 1 : 0;    /* fortress ring */
    }
}

/* Fill the wall for the current level.  From level 3 the top row is
   HARDENED (two hits, drawn with a mortar edge); from level 5 two rows. */
static void build_wall(void)
{
    int r, c;
    s_left = 0;
    for (r = 0; r < BROWS; ++r)
        for (c = 0; c < BCOLS; ++c) {
            unsigned char v = wall_at(s_level, r, c) ? 1 : 0;
            if (v && ((s_level >= 3 && r == 0) ||
                      (s_level >= 5 && r == 1)))
                v = 2;
            s_brick[r * BCOLS + c] = v;
            if (v) ++s_left;
        }
    s_cap_on = 0;                 /* no capsule carries across a new wall    */
}

/* Lay out a fresh wall and serve.  Called lazily once the geometry (which
   depends on the window client) is known. */
static void ensure_init(const Rect *cl)
{
    BGeom g;
    if (!s_fresh)
        return;
    geom(cl, &g);
    build_wall();
    serve(&g);
    s_dbx = s_bx; s_dby = s_by; s_dpx = s_px;
    s_fresh     = 0;
    s_need_full = 1;
}

void breaker_open(void)
{
    s_score = 0;
    s_balls = BALLS0;
    s_over  = 0;
    s_best  = 0;
    s_level = 1;
    s_cap_on = 0;
    s_wide = 0;
    s_slow = 0;
    s_fresh = 1;                   /* geometry-dependent setup on first draw  */
    s_need_full = 1;
    g_seed ^= ticks() | 1UL;
    s_last  = ticks();
}

/* Advance the ball one sub-step; resolve wall, paddle and brick hits. */
static void substep(const BGeom *g)
{
    int cx, cy, col, row, idx;

    s_bx += s_vx;
    s_by += s_vy;

    if (s_bx < 0)              { s_bx = 0;              s_vx = -s_vx; music_sfx(240, 1); }
    if (s_bx + BALLSZ > g->fw) { s_bx = g->fw - BALLSZ; s_vx = -s_vx; music_sfx(240, 1); }
    if (s_by < 0)              { s_by = 0;              s_vy = -s_vy; music_sfx(240, 1); }

    /* Paddle: bounce when the ball is falling onto the paddle's span.  The
       horizontal hit offset steers the rebound, so the player has control. */
    if (s_vy > 0 && s_by + BALLSZ >= g->py && s_by + BALLSZ <= g->py + g->ph) {
        cx = s_bx + BALLSZ / 2;
        if (cx >= s_px && cx <= s_px + g->pw) {
            int off = cx - (s_px + g->pw / 2);   /* -pw/2 .. +pw/2           */
            s_by = g->py - BALLSZ;
            s_vy = -2;
            s_vx = off / (g->pw / 6 + 1);
            if (s_vx >  3) s_vx =  3;
            if (s_vx < -3) s_vx = -3;
            if (s_vx == 0) s_vx = coin() ? 1 : -1;
            music_sfx(320, 1);                     /* a "pock" off the paddle */
        }
    }

    /* Bottom miss: lose a ball. */
    if (s_by + BALLSZ > g->fh) {
        if (--s_balls <= 0) {
            s_over = 1;
            s_best = hiscore_submit("breaker", s_score) ? 1 : 0;
        }
        else                { serve(g); }
        music_sfx(150, 3);                         /* a low buzz on a miss    */
        s_need_full = 1;
        return;
    }

    /* Brick hit: map the ball centre to a cell in the brick block. */
    cy = s_by + BALLSZ / 2;
    if (cy >= g->btop && cy < g->btop + BROWS * g->bh) {
        cx  = s_bx + BALLSZ / 2;
        col = cx / g->bw;
        row = (cy - g->btop) / g->bh;
        if (col >= 0 && col < BCOLS && row >= 0 && row < BROWS) {
            idx = row * BCOLS + col;
            if (s_brick[idx]) {
                if (s_brick[idx] == 2) {       /* hardened: crack it first  */
                    s_brick[idx] = 1;
                    s_score += 10;
                    music_sfx(500, 1);         /* a duller clink            */
                } else {
                    s_brick[idx] = 0;
                    s_score += (BROWS - row) * 5;
                    --s_left;
                    /* higher rows ring higher - a melody as you climb      */
                    music_sfx((unsigned)(760 + (BROWS - row) * 80), 1);
                    /* ~1 break in 4 drops a capsule (only one at a time). */
                    if (s_left > 0 && !s_cap_on && coin() && coin()) {
                        s_cap_on   = 1;
                        s_cap_x    = col * g->bw + g->bw / 2 - CAPW / 2;
                        s_cap_y    = g->btop + row * g->bh;
                        s_cap_type = (int)((g_seed >> 9) % 3);
                    }
                }
                s_vy = -s_vy;
                if (s_left <= 0) {             /* wall cleared: next level  */
                    ++s_level;
                    if (s_balls < 5) ++s_balls;    /* a bonus ball          */
                    build_wall();
                    serve(g);
                    music_sfx(1047, 4);            /* the level-up fanfare  */
                }
                s_need_full = 1;
            }
        }
    }
}

bool_t breaker_tick(const Rect *cl)
{
    BGeom g;
    int k, steps;
    if (s_fresh) { ensure_init(cl); return TRUE; }
    if (s_over) return FALSE;
    if (ticks() - s_last < (unsigned long)SPEED) return FALSE;
    s_last = ticks();

    /* The ball gains a sub-step every third level, capped where a 386
       (and a human) can still track it. */
    steps = SUBSTEP + (s_level - 1) / 3;
    if (steps > 4) steps = 4;

    if (s_slow) steps = 1;               /* the SLOW power-up               */

    geom(cl, &g);
    clamp_paddle(&g);   /* a keyboard nudge clamps loosely; make it exact
                           BEFORE physics so the hit span is the drawn one */
    for (k = 0; k < steps && !s_over; ++k)
        substep(&g);

    /* Power-up timers run down; a live capsule falls and may be caught.
       When one expires the paddle/status changes shape, so the next frame
       must be a full redraw - the incremental path would erase the old
       (wider) paddle with the new width and leave a fragment behind. */
    if (s_wide > 0) { --s_wide; if (s_wide == 0) s_need_full = 1; }
    if (s_slow > 0) { --s_slow; if (s_slow == 0) s_need_full = 1; }
    if (s_cap_on) {
        s_cap_y += 3;
        if (s_cap_y + CAPH >= g.py && s_cap_y <= g.py + g.ph) {
            int ccx = s_cap_x + CAPW / 2;
            if (ccx >= s_px && ccx <= s_px + g.pw) {        /* caught!       */
                if (s_cap_type == CAP_WIDE)      s_wide = 320;
                else if (s_cap_type == CAP_BALL) { if (s_balls < 5) ++s_balls; }
                else                             s_slow = 220;
                s_score += 25;
                music_sfx(1568, 2);
                s_cap_on = 0;
            }
        }
        if (s_cap_y > g.fh) s_cap_on = 0;                   /* missed        */
        s_need_full = 1;      /* a moving capsule: draw the whole field      */
    }
    return TRUE;
}

bool_t breaker_key(int key)
{
    if (key == KEY_F2) {          /* F2 = New Game, everywhere */
        breaker_open();
        return TRUE;
    }
    if ((key == KEY_ENTER || key == ' ') && s_over) {
        breaker_open();
        return TRUE;                /* one-off full repaint on restart        */
    }
    if (s_over || s_fresh)
        return FALSE;
    /* Nudge the paddle; the next tick (always running while in play) shows
       it, so no repaint is forced here - that keeps steering cheap.  The
       field width is not known without the client, so clamp loosely here
       and let the draws clamp precisely against the real field. */
    if (key == KEY_LEFT) {
        s_px -= 8;
        if (s_px < 0) s_px = 0;
    } else if (key == KEY_RIGHT) {
        s_px += 8;
        if (s_px > SCREEN_W) s_px = SCREEN_W;
    }
    return FALSE;
}

bool_t breaker_mouse(const Rect *cl, int mx)
{
    BGeom g;
    int want;
    if (s_fresh || s_over)
        return FALSE;
    geom(cl, &g);
    want = (mx - g.ox) - g.pw / 2;          /* centre the paddle on the mouse */
    if (want < 0) want = 0;
    if (want > g.fw - g.pw) want = g.fw - g.pw;
    if (want == s_px)
        return FALSE;
    s_px = want;
    return TRUE;
}

/* Clamp the paddle to the field (used by the draws after a blind key nudge). */
static void clamp_paddle(const BGeom *g)
{
    /* High clamp FIRST: with the order reversed, a field narrower than the
       paddle left s_px negative. */
    if (s_px > g->fw - g->pw) s_px = g->fw - g->pw;
    if (s_px < 0) s_px = 0;
}

static void draw_paddle(const BGeom *g)
{
    int x = g->ox + s_px, y = g->oy + g->py;
    vid_fillrect(x, y, g->pw, g->ph, C_LTBLUE);
    vid_hline(x, y, g->pw, C_WHITE);              /* a thin sheen on top      */
}

void breaker_draw(const Rect *cl)
{
    BGeom g;
    int r, c, i;
    char buf[48];   /* the status line has five fields; leave slack */
    ensure_init(cl);
    geom(cl, &g);
    clamp_paddle(&g);

    vid_fillrect(g.ox, g.oy, g.fw, g.fh, C_BLACK);
    vid_rect(g.ox - 1, g.oy - 1, g.fw + 2, g.fh + 2, C_DKGRAY);

    for (r = 0; r < BROWS; ++r)
        for (c = 0; c < BCOLS; ++c) {
            int bx = g.ox + c * g.bw + 1;
            int by = g.oy + g.btop + r * g.bh + 1;
            i = r * BCOLS + c;
            if (!s_brick[i]) continue;
            vid_fillrect(bx, by, g.bw - 2, g.bh - 2, s_row_col[r]);
            if (s_brick[i] == 2) {             /* hardened: mortar edges    */
                vid_rect (bx, by, g.bw - 2, g.bh - 2, C_WHITE);
                vid_hline(bx + 2, by + (g.bh - 2) / 2, g.bw - 6, C_WHITE);
            }
        }

    draw_paddle(&g);
    vid_fillrect(g.ox + s_bx, g.oy + s_by, BALLSZ, BALLSZ, C_WHITE);

    if (s_cap_on) {                    /* the falling capsule               */
        static const u8   CAPCOL[3] = { C_GREEN, C_YELLOW, C_CYAN };
        static const char CAPCH[3]  = { 'W', '+', 'S' };
        int cx = g.ox + s_cap_x, cy = g.oy + s_cap_y;
        vid_fillrect(cx, cy, CAPW, CAPH, CAPCOL[s_cap_type]);
        vid_rect(cx, cy, CAPW, CAPH, C_BLACK);
        font_draw_char(cx + (CAPW - font_adv()) / 2, cy - 1,
                       CAPCH[s_cap_type], C_BLACK);
    }

    if (s_over && s_best)
        sprintf(buf, "New record!  %ld  (click)", s_score);
    else if (s_over)
        sprintf(buf, "Game over - best %ld (click)", hiscore_best("breaker"));
    else
        sprintf(buf, "Lv %d  Score %ld  Balls %d %s%s",
                s_level, s_score, s_balls,
                s_wide ? "[W]" : "", s_slow ? "[S]" : "");
    ui_text_center(cl->x, g.oy + g.fh + 3, cl->w, buf,
                   s_over ? (s_best ? C_GREEN : C_RED) : C_BLACK);

    s_dbx = s_bx; s_dby = s_by; s_dpx = s_px;
    s_need_full = 0;
}

/* Incremental redraw: erase the ball and paddle where they were last
   painted, draw them where they are now, and report the bounding box.  The
   bricks and status line are unchanged on a normal frame, so they are left
   alone.  Returns FALSE when a full breaker_draw is required. */
bool_t breaker_step_draw(const Rect *cl, Rect *dirty)
{
    BGeom g;
    int minx = 32000, miny = 32000, maxx = -1, maxy = -1;
    int ex, ey, ew, eh;
    ensure_init(cl);
    if (s_need_full)
        return FALSE;
    geom(cl, &g);
    clamp_paddle(&g);

#define BRK_EXT(X, Y, W, H) do { \
        if ((X) < minx) minx = (X); \
        if ((Y) < miny) miny = (Y); \
        if ((X) + (W) > maxx) maxx = (X) + (W); \
        if ((Y) + (H) > maxy) maxy = (Y) + (H); } while (0)

    /* erase old ball */
    ex = g.ox + s_dbx; ey = g.oy + s_dby;
    vid_fillrect(ex, ey, BALLSZ, BALLSZ, C_BLACK);
    BRK_EXT(ex, ey, BALLSZ, BALLSZ);

    /* erase old paddle (full width, fixed y) */
    ex = g.ox + s_dpx; ey = g.oy + g.py; ew = g.pw; eh = g.ph;
    vid_fillrect(ex, ey, ew, eh, C_BLACK);
    BRK_EXT(ex, ey, ew, eh);

    /* draw paddle at its new position */
    draw_paddle(&g);
    BRK_EXT(g.ox + s_px, g.oy + g.py, g.pw, g.ph);

    /* draw ball at its new position */
    ex = g.ox + s_bx; ey = g.oy + s_by;
    vid_fillrect(ex, ey, BALLSZ, BALLSZ, C_WHITE);
    BRK_EXT(ex, ey, BALLSZ, BALLSZ);

#undef BRK_EXT
    s_dbx = s_bx; s_dby = s_by; s_dpx = s_px;
    rect_set(dirty, minx, miny, maxx - minx, maxy - miny);
    return TRUE;
}

void breaker_click(const Rect *cl, int mx, int my)
{
    (void)cl; (void)mx; (void)my;
    if (s_over)
        breaker_open();
}
