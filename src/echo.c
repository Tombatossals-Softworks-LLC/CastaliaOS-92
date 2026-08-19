/* ======================================================================
 * echo.c - Echo (a sound-memory game) for CASTALIA/386
 * ----------------------------------------------------------------------
 * A small state machine: SHOW replays the sequence (the machine's turn,
 * paced off the BIOS tick), INPUT waits for the player's clicks, OVER holds
 * the final score.  Only state changes repaint - INPUT sits idle between
 * clicks with no busy redraw - and every tone goes through music.c's
 * non-blocking sound effects, so nothing here ever blocks the desktop.
 * ====================================================================== */
#include <dos.h>
#include <stdio.h>
#include "echo.h"
#include "video.h"
#include "system.h"
#include "ui.h"
#include "font.h"
#include "keyboard.h"
#include "music.h"

#define MAXSEQ    32
#define LIT_TICKS 4                /* how long a panel stays lit (~0.22 s)    */
#define GAP_TICKS 2                /* dark gap between steps                   */

#define EC_SHOW   0
#define EC_INPUT  1
#define EC_OVER   2

static unsigned char far ec_seq[MAXSEQ];   /* the sequence, panel indices 0..3 */
static int ec_len;                 /* current sequence length                 */
static int ec_show;                /* SHOW: step being played (-1 = lead gap) */
static int ec_phase;               /* SHOW: 0 = about to light, 1 = in gap    */
static int ec_in;                  /* INPUT: next expected step               */
static int ec_lit;                 /* panel currently lit (-1 = none)         */
static int ec_mode;
static int ec_score;
static unsigned long ec_t;         /* tick of the last state change           */

/* Panel colours (top-left, top-right, bottom-left, bottom-right) and the
   tone each one sounds - an A-major spread, low to high, so the sequence
   plays as music. */
static const u8       EC_COL[4]  = { C_GREEN, C_RED, C_YELLOW, C_CYAN };
static const unsigned EC_TONE[4] = { 330, 277, 220, 165 };

static unsigned long ticks(void)
{
    return sys_ticks();               /* fast BDA read - see system.h      */
}
static unsigned long g_seed = 0x0EC0EC00UL;
static int rnd4(void)
{
    g_seed = g_seed * 1103515245UL + 12345UL;
    return (int)((g_seed >> 22) & 3);
}

void echo_open(void)
{
    g_seed  ^= ticks() | 1UL;          /* season BEFORE the first draw -   */
    ec_len   = 1;                      /* else game one always opens on    */
    ec_seq[0] = (unsigned char)rnd4(); /* the same panel                   */
    ec_mode  = EC_SHOW;
    ec_show  = -1;                 /* a lead-in gap before the first flash     */
    ec_phase = 1;
    ec_in    = 0;
    ec_lit   = -1;
    ec_score = 0;
    ec_t     = ticks();
}

/* Advance the SHOW playback / clear a click flash.  Returns TRUE when the
   window should repaint. */
bool_t echo_tick(void)
{
    unsigned long el = ticks() - ec_t;

    if (ec_mode == EC_SHOW) {
        if (ec_lit >= 0) {                         /* lit: hold, then darken  */
            if (el >= LIT_TICKS) { ec_lit = -1; ec_phase = 1; ec_t = ticks(); return TRUE; }
            return FALSE;
        }
        if (ec_phase == 1) {                       /* gap: wait, then advance */
            if (el < GAP_TICKS) return FALSE;
            ++ec_show;
            ec_phase = 0;
            if (ec_show >= ec_len) {               /* whole sequence shown    */
                ec_mode = EC_INPUT; ec_in = 0; ec_t = ticks();
            }
            return TRUE;
        }
        ec_lit = ec_seq[ec_show];                  /* light the current step  */
        music_sfx(EC_TONE[ec_lit], LIT_TICKS);
        ec_t = ticks();
        return TRUE;
    }
    if (ec_mode == EC_INPUT) {
        if (ec_lit >= 0 && el >= LIT_TICKS) { ec_lit = -1; return TRUE; }
        return FALSE;
    }
    return FALSE;                                   /* OVER: nothing moves     */
}

bool_t echo_key(int key)
{
    if (key == KEY_F2) {          /* F2 = New Game, everywhere */
        echo_open();
        return TRUE;
    }
    if ((key == KEY_ENTER || key == ' ') && ec_mode == EC_OVER) {
        echo_open();
        return TRUE;
    }
    return FALSE;
}

/* ---- geometry -------------------------------------------------------- */

static void panels(const Rect *cl, Rect p[4], int *status_y)
{
    int m = 6, g = 6, sh = font_h() + 4;
    int pw = (cl->w - 2 * m - g) / 2;
    int ph = (cl->h - 2 * m - g - sh) / 2;
    int x0 = cl->x + m, y0 = cl->y + m;
    rect_set(&p[0], x0,          y0,          pw, ph);
    rect_set(&p[1], x0 + pw + g, y0,          pw, ph);
    rect_set(&p[2], x0,          y0 + ph + g, pw, ph);
    rect_set(&p[3], x0 + pw + g, y0 + ph + g, pw, ph);
    *status_y = y0 + 2 * ph + g + 2;
}

static void draw_panel(const Rect *p, int idx, bool_t on)
{
    if (on) {                                      /* bright: solid + frame   */
        vid_fillrect(p->x, p->y, p->w, p->h, EC_COL[idx]);
        vid_rect(p->x,     p->y,     p->w,     p->h,     C_WHITE);
        vid_rect(p->x + 1, p->y + 1, p->w - 2, p->h - 2, C_WHITE);
    } else {                                       /* dim: half-shaded panel  */
        /* A CHECKERBOARD, not a black line every other column.  The
           stripe was a 50% shade too, and arithmetically the same, but
           at 320x200 a one-pixel vertical comb reads as corduroy rather
           than as "this lamp is off" - and vid_dither_rect is what the
           trough, the drop shadows and the desktop's selection tint all
           use for exactly this, so the shell had one panel dimming
           itself in a way nothing else did. */
        vid_fillrect(p->x, p->y, p->w, p->h, EC_COL[idx]);
        vid_dither_rect(p->x, p->y, p->w, p->h, C_BLACK);
        vid_bevel(p->x, p->y, p->w, p->h, C_DKGRAY, C_DKGRAY);
    }
}

void echo_draw(const Rect *cl)
{
    Rect p[4];
    int sy, i;
    char buf[40];
    panels(cl, p, &sy);
    for (i = 0; i < 4; ++i)
        draw_panel(&p[i], i, (i == ec_lit) ? TRUE : FALSE);

    if (ec_mode == EC_OVER)      sprintf(buf, "Score %d  -  game over (click)", ec_score);
    else if (ec_mode == EC_SHOW) sprintf(buf, "Round %d  -  watch...", ec_len);
    else                         sprintf(buf, "Round %d  -  your turn", ec_len);
    ui_text_center(cl->x, sy, cl->w, buf, (ec_mode == EC_OVER) ? C_RED : C_BLACK);
}

static int hit_panel(const Rect *cl, int mx, int my)
{
    Rect p[4];
    int sy, i;
    panels(cl, p, &sy);
    for (i = 0; i < 4; ++i)
        if (mx >= p[i].x && mx < p[i].x + p[i].w &&
            my >= p[i].y && my < p[i].y + p[i].h)
            return i;
    return -1;
}

void echo_click(const Rect *cl, int mx, int my)
{
    int pn;
    if (ec_mode == EC_OVER) { echo_open(); return; }
    if (ec_mode != EC_INPUT) return;               /* machine's turn: ignore  */
    pn = hit_panel(cl, mx, my);
    if (pn < 0) return;

    ec_lit = pn; ec_t = ticks();                   /* flash the clicked panel */
    if (pn == ec_seq[ec_in]) {
        music_sfx(EC_TONE[pn], LIT_TICKS);
        if (++ec_in >= ec_len) {                   /* round cleared -> grow   */
            ec_score = ec_len;
            if (ec_len < MAXSEQ) ec_seq[ec_len++] = (unsigned char)rnd4();
            ec_mode = EC_SHOW; ec_show = -1; ec_phase = 1; ec_lit = -1;
            ec_t = ticks();
        }
    } else {
        music_sfx(110, 6);                         /* a low buzz: wrong       */
        ec_mode = EC_OVER;
    }
}
