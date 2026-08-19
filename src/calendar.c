/* ======================================================================
 * calendar.c - A month-view calendar for CASTALIA/386
 * ====================================================================== */
#include <stdio.h>
#include <dos.h>       /* _dos_getdate                                    */
#include "calendar.h"
#include "video.h"
#include "font.h"
#include "keyboard.h"

static int g_vm = 1, g_vy = 1995;      /* viewed month / year              */
static int g_tm = 1, g_ty = 1995, g_td = 1;   /* today                     */

static Rect g_prev_btn, g_next_btn;

static const char * const MONTHS[12] = {
    "January", "February", "March",     "April",   "May",      "June",
    "July",    "August",   "September", "October", "November", "December"
};

static int leap(int y)
{
    return ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0) ? 1 : 0;
}

static int days_in(int m, int y)
{
    static const int D[12] = { 31,28,31,30,31,30,31,31,30,31,30,31 };
    return (m == 2) ? D[1] + leap(y) : D[m - 1];
}

/* Day of week for y-m-d, 0 = Sunday (Sakamoto's method). */
static int dow(int y, int m, int d)
{
    static const int T[12] = { 0,3,2,5,0,3,5,1,4,6,2,4 };
    if (m < 3) --y;
    return (y + y/4 - y/100 + y/400 + T[m - 1] + d) % 7;
}

void calendar_open(void)
{
    struct dosdate_t dd;
    _dos_getdate(&dd);
    g_ty = (int)dd.year; g_tm = (int)dd.month; g_td = (int)dd.day;
    g_vy = g_ty; g_vm = g_tm;
}

static void month_step(int dir)
{
    g_vm += dir;
    if (g_vm < 1)  { g_vm = 12; --g_vy; }
    if (g_vm > 12) { g_vm = 1;  ++g_vy; }
    /* Stop AT the range edge - clamping only the year would land the
       view eleven months away from where it started. */
    if (g_vy < 1980) { g_vy = 1980; g_vm = 1;  }   /* the DOS epoch        */
    if (g_vy > 2099) { g_vy = 2099; g_vm = 12; }
}

void calendar_draw(const Rect *cl)
{
    char head[24];
    int lh = font_h() + 2;
    int gx = cl->x + 4, gy;
    int gw = cl->w - 8;
    int colw = gw / 7;
    int rowh, first, nd, cell, r, c;
    static const char * const DAYS[7] =
        { "Su", "Mo", "Tu", "We", "Th", "Fr", "Sa" };

    /* Header: < month year >. */
    rect_set(&g_prev_btn, cl->x + 4, cl->y + 3, font_adv() * 2 + 6, lh + 2);
    rect_set(&g_next_btn, cl->x + cl->w - font_adv() * 2 - 10, cl->y + 3,
             font_adv() * 2 + 6, lh + 2);
    ui_button(&g_prev_btn, "<", FALSE);
    ui_button(&g_next_btn, ">", FALSE);
    sprintf(head, "%s %d", MONTHS[g_vm - 1], g_vy);
    ui_text_center(cl->x, cl->y + 5, cl->w, head, C_TITLE);

    /* Day-of-week strip. */
    gy = cl->y + lh + 8;
    for (c = 0; c < 7; ++c)
        ui_text_center(gx + c * colw, gy, colw, DAYS[c],
                       (c == 0 || c == 6) ? C_RED : C_DKGRAY);
    gy += lh;
    vid_hline(gx, gy - 2, colw * 7, C_SHADOW);

    /* The day grid: 6 rows always, so the layout never jumps. */
    rowh = (cl->y + cl->h - gy - 4) / 6;
    if (rowh < lh) rowh = lh;
    first = dow(g_vy, g_vm, 1);
    nd    = days_in(g_vm, g_vy);
    cell  = 1 - first;
    for (r = 0; r < 6; ++r) {
        for (c = 0; c < 7; ++c, ++cell) {
            char b[4];
            int dx = gx + c * colw, dy = gy + r * rowh;
            if (cell < 1 || cell > nd)
                continue;
            sprintf(b, "%d", cell);
            if (cell == g_td && g_vm == g_tm && g_vy == g_ty) {
                vid_fillrect(dx + 1, dy - 1, colw - 2, lh, C_TITLE);
                ui_text_center(dx, dy, colw, b, C_WHITE);   /* today        */
            } else {
                ui_text_center(dx, dy, colw, b,
                               (c == 0 || c == 6) ? C_RED : C_BLACK);
            }
        }
    }
}

bool_t calendar_click(const Rect *cl, int mx, int my)
{
    (void)cl;
    if (rect_contains(&g_prev_btn, mx, my)) { month_step(-1); return TRUE; }
    if (rect_contains(&g_next_btn, mx, my)) { month_step(1);  return TRUE; }
    return FALSE;
}

bool_t calendar_key(int key)
{
    if (key == KEY_LEFT)  { month_step(-1);  return TRUE; }
    if (key == KEY_RIGHT) { month_step(1);   return TRUE; }
    if (key == KEY_PGUP)  { --g_vy; if (g_vy < 1980) g_vy = 1980; return TRUE; }
    if (key == KEY_PGDN)  { ++g_vy; if (g_vy > 2099) g_vy = 2099; return TRUE; }
    if (key == KEY_HOME)  { g_vy = g_ty; g_vm = g_tm; return TRUE; }
    return FALSE;
}
