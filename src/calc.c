/* ======================================================================
 * calc.c - Calculator applet for CASTALIA/386 (v0.5)
 * ====================================================================== */
#include <stdio.h>
#include <string.h>
#include "calc.h"
#include "video.h"
#include "ui.h"
#include "font.h"
#include "system.h"   /* sys_ticks: the key-press flash */

/* Calculator state (classic immediate-execution machine). */
static long   g_acc   = 0;        /* running accumulator                 */
static long   g_cur   = 0;        /* number currently being typed         */
static char   g_op    = 0;        /* pending operator, or 0               */
static bool_t g_fresh = TRUE;     /* next digit starts a new number       */
static bool_t g_error = FALSE;
static char   g_disp[20] = "0";
/* Which keypad button is showing pressed, and since when. */
static int           g_press_r = -1, g_press_c = -1;
static unsigned long g_press_t = 0;

/* The 4x4 keypad. */
static const char *KEYS[4][4] = {
    { "7", "8", "9", "/" },
    { "4", "5", "6", "*" },
    { "1", "2", "3", "-" },
    { "0", "C", "=", "+" }
};

void calc_reset(void)
{
    g_acc = 0; g_cur = 0; g_op = 0;
    g_fresh = TRUE; g_error = FALSE;
    strcpy(g_disp, "0");
}

static void show(long v)
{
    sprintf(g_disp, "%ld", v);
}

static void calc_digit(int d)
{
    if (g_error) calc_reset();
    if (g_fresh) { g_cur = d; g_fresh = FALSE; }
    else if (g_cur < 99999999L && g_cur > -99999999L)
        g_cur = g_cur * 10 + (g_cur < 0 ? -d : d);
    show(g_cur);
}

static void apply_pending(void)
{
    switch (g_op) {
    case '+': g_acc = g_acc + g_cur; break;
    case '-': g_acc = g_acc - g_cur; break;
    case '*':
        /* Guard the 32-bit accumulator: 8-digit operands can overflow a
           long product silently, so report Error like divide-by-zero. */
        if (g_cur != 0) {
            long lim = 999999999L / (g_cur < 0 ? -g_cur : g_cur);
            if (g_acc > lim || g_acc < -lim) {
                g_error = TRUE; strcpy(g_disp, "Error"); return;
            }
        }
        g_acc = g_acc * g_cur;
        break;
    case '/':
        if (g_cur == 0) { g_error = TRUE; strcpy(g_disp, "Error"); return; }
        g_acc = g_acc / g_cur;
        break;
    default:  g_acc = g_cur; break;       /* no pending op: just take cur */
    }
    if (g_acc > 999999999L || g_acc < -999999999L) {
        g_error = TRUE; strcpy(g_disp, "Error"); return;
    }
    show(g_acc);
}

static void calc_op(char op)
{
    if (g_error) return;
    /* No digits since the last operator: just replace the pending op
       (5 + - 3 must mean 5 - 3, not 5 + 5 - 3). */
    if (!g_fresh) {
        apply_pending();
        if (g_error) return;
    }
    g_op = op;
    g_fresh = TRUE;
}

static void calc_equals(void)
{
    if (g_error) return;
    apply_pending();
    if (g_error) return;
    g_op = 0;
    g_cur = g_acc;
    g_fresh = TRUE;
}

static void press(const char *label)
{
    char c = label[0];
    if (c >= '0' && c <= '9') calc_digit(c - '0');
    else if (c == 'C')        calc_reset();
    else if (c == '=')        calc_equals();
    else                      calc_op(c);   /* + - * /                    */
}

/* ---- layout ---------------------------------------------------------- */
#define GAP 2

static void disp_rect(const Rect *c, Rect *d)
{
    int dh = font_h() + 8;
    rect_set(d, c->x + GAP, c->y + GAP, c->w - 2 * GAP, dh);
}

static void key_rect(const Rect *c, int row, int col, Rect *k)
{
    Rect d;
    int gx, gy, bw, bh;
    disp_rect(c, &d);
    gy = d.y + d.h + GAP;
    gx = c->x + GAP;
    bw = (c->w - 5 * GAP) / 4;
    bh = (c->y + c->h - gy - GAP - 4 * GAP) / 4;
    rect_set(k, gx + col * (bw + GAP), gy + row * (bh + GAP), bw, bh);
}

/* ---- drawing --------------------------------------------------------- */
void calc_draw(const Rect *client)
{
    Rect d, k;
    int row, col, tw, tx;

    /* LCD-style display. */
    disp_rect(client, &d);
    vid_fillrect(d.x, d.y, d.w, d.h, C_BLACK);
    ui_sink(d.x, d.y, d.w, d.h);
    tw = font_text_width(g_disp);
    tx = d.x + d.w - 5 - tw;
    if (tx < d.x + 3) tx = d.x + 3;
    font_draw(tx, d.y + (d.h - font_h()) / 2, g_disp, C_GREEN);

    /* Keypad. */
    for (row = 0; row < 4; ++row)
        for (col = 0; col < 4; ++col) {
            key_rect(client, row, col, &k);
            /* A key that never renders sunken gives no sign it was heard:
               the only evidence anything happened was the display
               changing.  Hold the press for a few ticks. */
            ui_button(&k, KEYS[row][col],
                      (row == g_press_r && col == g_press_c &&
                       sys_ticks() - g_press_t < 3UL) ? TRUE : FALSE);
        }
}

/* ---- interaction ----------------------------------------------------- */
bool_t calc_click(const Rect *client, int mx, int my)
{
    int row, col;
    Rect k;
    for (row = 0; row < 4; ++row)
        for (col = 0; col < 4; ++col) {
            key_rect(client, row, col, &k);
            if (rect_contains(&k, mx, my)) {
                g_press_r = row; g_press_c = col; g_press_t = sys_ticks();
                press(KEYS[row][col]);
                return TRUE;
            }
        }
    return FALSE;
}

/* TRUE while a key is still showing pressed, so the shell repaints once
   more to lift it.  Without this the flash STUCK: nothing ticks the
   Calculator, so the window was painted once with the key sunken and
   stayed visually held down until some unrelated repaint. */
bool_t calc_tick(void)
{
    if (g_press_r < 0)
        return FALSE;
    if (sys_ticks() - g_press_t < 3UL)
        return FALSE;                  /* still within the flash          */
    g_press_r = -1;                    /* time to lift it: repaint once   */
    g_press_c = -1;
    return TRUE;
}

/* Light whichever keypad button carries this character, so typing looks
   the same as clicking. */
static void flash_key(int ch)
{
    int r, c;
    for (r = 0; r < 4; ++r)
        for (c = 0; c < 4; ++c)
            if (KEYS[r][c][0] == ch && KEYS[r][c][1] == '\0') {
                g_press_r = r; g_press_c = c; g_press_t = sys_ticks();
                return;
            }
}

bool_t calc_key(int key)
{
    if (key >= '0' && key <= '9') {
        flash_key(key);
        calc_digit(key - '0');
        return TRUE;
    }
    if (key == 13) flash_key('=');
    else           flash_key(key);
    switch (key) {
    case '+': case '-': case '*': case '/': calc_op((char)key); return TRUE;
    case '=':
    case 13:  calc_equals(); return TRUE;     /* Enter = equals           */
    /* No ESC here: main() consumes it before wm_key ever runs, so the
       old "case 27" was unreachable. */
    case 'c': case 'C': calc_reset(); return TRUE;
    case 8:                                   /* Backspace: drop a digit   */
        if (!g_fresh && !g_error) { g_cur /= 10; show(g_cur); return TRUE; }
        break;
    default: break;
    }
    return FALSE;
}
