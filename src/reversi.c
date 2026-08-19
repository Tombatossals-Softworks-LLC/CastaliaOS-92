/* ======================================================================
 * reversi.c - Reversi (Othello) minigame for CASTALIA/386
 * ====================================================================== */
#include <stdio.h>
#include "reversi.h"
#include "video.h"
#include "ui.h"
#include "keyboard.h"
#include "font.h"
#include "music.h"

static int far g_b[64];   /* 0 empty, 1 = you (dark), 2 = machine (light) */
static int g_over;        /* 0 playing, 1 finished                        */
static int g_cur = 27;    /* the keyboard cursor's square                 */

static const int DR[8] = { -1, -1, -1,  0, 0,  1, 1, 1 };
static const int DC[8] = { -1,  0,  1, -1, 1, -1, 0, 1 };

void reversi_open(void)
{
    int i;
    for (i = 0; i < 64; ++i) g_b[i] = 0;
    g_cur = 27;
    g_b[27] = 2; g_b[28] = 1;              /* the four opening discs        */
    g_b[35] = 1; g_b[36] = 2;
    g_over = 0;
}

/* Flips that placing `who` at pos would make; if doit, perform them. */
static int try_move(int pos, int who, int doit)
{
    int flipped = 0, d;
    int opp = (who == 1) ? 2 : 1;
    if (pos < 0 || pos >= 64 || g_b[pos] != 0)
        return 0;
    for (d = 0; d < 8; ++d) {
        int r = pos / 8 + DR[d], c = pos % 8 + DC[d], cnt = 0;
        while (r >= 0 && r < 8 && c >= 0 && c < 8 && g_b[r * 8 + c] == opp) {
            r += DR[d]; c += DC[d]; ++cnt;
        }
        if (cnt > 0 && r >= 0 && r < 8 && c >= 0 && c < 8 &&
            g_b[r * 8 + c] == who) {
            flipped += cnt;
            if (doit) {
                int rr = pos / 8 + DR[d], cc = pos % 8 + DC[d], k;
                for (k = 0; k < cnt; ++k) {
                    g_b[rr * 8 + cc] = who; rr += DR[d]; cc += DC[d];
                }
            }
        }
    }
    if (doit && flipped > 0) g_b[pos] = who;
    return flipped;
}

static int has_move(int who)
{
    int i;
    for (i = 0; i < 64; ++i)
        if (g_b[i] == 0 && try_move(i, who, 0) > 0) return 1;
    return 0;
}

static void cpu_move(void)
{
    int best = -1, bestscore = -1, i;
    for (i = 0; i < 64; ++i) {
        int f, s;
        if (g_b[i] != 0) continue;
        f = try_move(i, 2, 0);
        if (f <= 0) continue;
        s = f;
        if (i == 0 || i == 7 || i == 56 || i == 63) s += 20;   /* corners */
        else if (i / 8 == 0 || i / 8 == 7 || i % 8 == 0 || i % 8 == 7) s += 3;
        if (s > bestscore) { bestscore = s; best = i; }
    }
    if (best >= 0) try_move(best, 2, 1);
}

static void count(int *you, int *cpu)
{
    int i; *you = 0; *cpu = 0;
    for (i = 0; i < 64; ++i) {
        if (g_b[i] == 1) ++*you; else if (g_b[i] == 2) ++*cpu;
    }
}

static void isq_disc(int cx, int cy, int rad, u8 col)
{
    int dy;
    for (dy = -rad; dy <= rad; ++dy) {
        int v = rad * rad - dy * dy, hw = 0;
        while ((hw + 1) * (hw + 1) <= v) ++hw;
        vid_hline(cx - hw, cy + dy, 2 * hw + 1, col);
    }
}

static void geom(const Rect *cl, int *cell, int *gx, int *gy)
{
    int avail = (cl->w < cl->h ? cl->w : cl->h) - 8 - (font_h() + 6);
    int c = avail / 8;
    if (c < 8) c = 8;
    *cell = c;
    *gx = cl->x + (cl->w - c * 8) / 2;
    *gy = cl->y + 4;
}

void reversi_draw(const Rect *cl)
{
    int cell, gx, gy, i, you, cpu, rad;
    char line[40];
    geom(cl, &cell, &gx, &gy);
    rad = cell / 2 - 2;

    for (i = 0; i < 64; ++i) {
        int x = gx + (i % 8) * cell, y = gy + (i / 8) * cell;
        vid_fillrect(x, y, cell, cell, C_GREEN);
        vid_rect(x, y, cell + 1, cell + 1, C_DKGRAY);
        if (g_b[i] == 1) isq_disc(x + cell / 2, y + cell / 2, rad, C_RED);
        else if (g_b[i] == 2)
            isq_disc(x + cell / 2, y + cell / 2, rad, C_WHITE);
        else if (!g_over && try_move(i, 1, 0) != 0) {
            /* A legal square, marked the way every Othello program marks
               them.  Without this a beginner has no way to see where a
               move is even possible, and a rejected click looked
               identical to the game ignoring them. */
            int cxp = x + cell / 2, cyp = y + cell / 2;
            vid_fillrect(cxp - 1, cyp - 1, 3, 3, C_DKGRAY);
        }
        if (i == g_cur && !g_over)         /* the keyboard cursor          */
            vid_rect(x + 1, y + 1, cell - 1, cell - 1, C_YELLOW);
    }

    count(&you, &cpu);
    if (g_over)
        sprintf(line, "Final  You %d  CPU %d  %s", you, cpu,
                you > cpu ? "- you win!" : (cpu > you ? "- CPU wins" : "- tie"));
    else
        sprintf(line, "You(red) %d   CPU(white) %d", you, cpu);
    ui_text_center(cl->x, gy + cell * 8 + 3, cl->w, line,
                   g_over ? C_RED : C_BLACK);
}

/* Place at pos if it is legal, then let the machine answer.  Shared by
   the mouse and the keyboard.  A rejected square used to be a completely
   silent no-op, indistinguishable from a dropped keystroke on a 386SX. */
static void play_pos(int pos)
{
    if (pos < 0 || pos > 63)
        return;
    if (g_b[pos] != 0 || try_move(pos, 1, 0) == 0) {
        music_sfx(180, 1);                      /* a low "no" for a reject   */
        return;
    }
    try_move(pos, 1, 1);                       /* your move                  */
    music_sfx(440, 1);                          /* piece placed               */
    for (;;) {                                 /* machine takes its turns    */
        if (has_move(2)) cpu_move();
        else if (!has_move(1)) { g_over = 1; break; }
        if (has_move(1)) break;                /* back to you                */
        if (!has_move(2)) { g_over = 1; break; }
    }
    if (g_over) music_sfx(300, 4);              /* game over                  */
}

void reversi_click(const Rect *cl, int mx, int my)
{
    int cell, gx, gy, c, r;
    if (g_over) { reversi_open(); return; }
    geom(cl, &cell, &gx, &gy);
    c = (mx - gx) / cell;
    r = (my - gy) / cell;
    if (mx < gx || my < gy || c < 0 || c > 7 || r < 0 || r > 7)
        return;
    g_cur = r * 8 + c;                 /* keep the keyboard cursor in step */
    play_pos(g_cur);
}

/* These four were entirely mouse-driven: focusing one used to swallow
   every keystroke, and Fifteen had no way to restart at all short of
   closing the window.  F2 starts a fresh game, as in the other ten. */
/* Arrows walk the board, Enter or Space plays the square.  Reversi was
   mouse-only, and the mouse is optional on this machine. */
bool_t reversi_key(int key)
{
    if (key == KEY_F2) {
        reversi_open();
        return TRUE;
    }
    if (g_over && (key == KEY_ENTER || key == ' ')) {
        reversi_open();
        return TRUE;
    }
    /* FALSE when the cursor was already at the edge - no repaint. */
    switch (key) {
    case KEY_LEFT:  if (g_cur % 8 == 0) return FALSE; --g_cur;    return TRUE;
    case KEY_RIGHT: if (g_cur % 8 == 7) return FALSE; ++g_cur;    return TRUE;
    case KEY_UP:    if (g_cur < 8)      return FALSE; g_cur -= 8; return TRUE;
    case KEY_DOWN:  if (g_cur >= 56)    return FALSE; g_cur += 8; return TRUE;
    case KEY_ENTER:
    case ' ':
        play_pos(g_cur);
        return TRUE;
    default:
        break;
    }
    return FALSE;
}
