/* ======================================================================
 * clock.c - Clock & calendar applet for CASTALIA/386 (v0.5)
 * ====================================================================== */
#include <dos.h>
#include <stdio.h>
#include <string.h>
#include "clock.h"
#include "video.h"
#include "ui.h"
#include "font.h"

static const char *MONTHS[12] = {
    "January", "February", "March", "April", "May", "June",
    "July", "August", "September", "October", "November", "December"
};
static const char *DOWLONG[7] = {
    "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"
};
static const char *DOW2[7] = { "Su", "Mo", "Tu", "We", "Th", "Fr", "Sa" };

/* ---- analog clock face ---------------------------------------------- */
/* A small parabolic sine LUT (far - DGROUP is nearly full), an integer
   square root for the round face, and a Bresenham line for the hands. */
static signed char far c_sin[256];
static int          c_sin_built = 0;
static void build_csin(void)
{
    int i;
    if (c_sin_built) return;
    c_sin_built = 1;
    for (i = 0; i < 256; ++i) {
        long dd = (long)i * 360 / 256, num, den, v;
        int neg = 0;
        if (dd > 180) { dd -= 180; neg = 1; }
        num = 4L * dd * (180 - dd);
        den = 40500L - dd * (180 - dd);
        v   = num * 127 / den;
        c_sin[i] = (signed char)(neg ? -v : v);
    }
}
#define SINC(a) (c_sin[(a) & 255])

static int c_isqrt(long v)
{
    long x, y;
    if (v <= 0) return 0;
    x = v; y = (x + 1) / 2;
    while (y < x) { x = y; y = (x + v / x) / 2; }
    return (int)x;
}

static void c_line(int x0, int y0, int x1, int y1, u8 col)
{
    int dx = x1 - x0, dy = y1 - y0;
    int sx = (dx < 0) ? -1 : 1, sy = (dy < 0) ? -1 : 1, e, e2;
    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;
    e = dx - dy;
    for (;;) {
        vid_pixel(x0, y0, col);
        if (x0 == x1 && y0 == y1) break;
        e2 = 2 * e;
        if (e2 > -dy) { e -= dy; x0 += sx; }
        if (e2 <  dx) { e += dx; y0 += sy; }
    }
}

/* One hand: tick is 0..59 around the dial (0 = 12 o'clock, clockwise). */
static void hand(int acx, int acy, int tick, int len, u8 col)
{
    int ph = tick * 256 / 60;
    int ex = acx + (int)((long)len * SINC(ph)      / 127);
    int ey = acy - (int)((long)len * SINC(ph + 64) / 127);
    c_line(acx, acy, ex, ey, col);
}

static void draw_analog(int acx, int acy, int R, int hh, int mm, int ss)
{
    int dy, i, hht;
    for (dy = -R; dy <= R; ++dy) {                 /* dark rim              */
        int hw = c_isqrt((long)R * R - (long)dy * dy);
        vid_hline(acx - hw, acy + dy, 2 * hw + 1, C_DKGRAY);
    }
    for (dy = -(R - 2); dy <= (R - 2); ++dy) {     /* cream face            */
        int hw = c_isqrt((long)(R - 2) * (R - 2) - (long)dy * dy);
        vid_hline(acx - hw, acy + dy, 2 * hw + 1, C_CREAM);
    }
    for (i = 0; i < 12; ++i) {                     /* hour ticks            */
        int ph = (i * 5) * 256 / 60;
        int tx = acx + (int)((long)(R - 3) * SINC(ph)      / 127);
        int ty = acy - (int)((long)(R - 3) * SINC(ph + 64) / 127);
        vid_fillrect(tx - 1, ty - 1, 2, 2, C_BLACK);
    }
    hht = (hh % 12) * 5 + mm / 12;
    hand(acx, acy, hht, R - 9, C_BLACK);           /* hour                  */
    hand(acx, acy, mm,  R - 5, C_BLACK);           /* minute                */
    hand(acx, acy, ss,  R - 4, C_RED);             /* second                */
    vid_fillrect(acx - 1, acy - 1, 3, 3, C_BLACK); /* hub                   */
}

static int days_in_month(int month1, unsigned year)
{
    static const int dm[12] = { 31,28,31,30,31,30,31,31,30,31,30,31 };
    if (month1 == 2 &&
        (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0)))
        return 29;
    return dm[month1 - 1];
}

void clock_draw(const Rect *client)
{
    struct dostime_t t;
    struct dosdate_t d;
    char buf[40];
    int  cx = client->x;
    int  cw = client->w;
    int  y;
    int  ndays, dow1, day, cellw, cellh, gx, gy;
    int  R = 20, acx = cx + 6 + 20, acy = client->y + 8 + 20;

    _dos_gettime(&t);
    _dos_getdate(&d);
    build_csin();

    /* Analog face on the left; a compact LCD HH:MM:SS well to its right. */
    draw_analog(acx, acy, R, (int)t.hour, (int)t.minute, (int)t.second);
    {
        int rx = cx + 6 + 2 * R + 8;
        int rw = cw - (6 + 2 * R + 8) - 6;
        int dh = font_h() + 8;
        vid_fillrect(rx, acy - dh / 2, rw, dh, C_BLACK);
        ui_sink(rx, acy - dh / 2, rw, dh);
        sprintf(buf, "%02u:%02u:%02u", t.hour, t.minute, t.second);
        ui_text_center(rx, acy - dh / 2 + (dh - font_h()) / 2, rw, buf, C_GREEN);
    }
    y = client->y + 8 + 2 * R + 4;

    /* Full date line. */
    if (d.dayofweek < 7)
        sprintf(buf, "%s %u %s %u", DOWLONG[d.dayofweek], d.day,
                MONTHS[d.month - 1], d.year);
    else
        sprintf(buf, "%u %s %u", d.day, MONTHS[d.month - 1], d.year);
    ui_text_center(cx, y, cw, buf, C_BLACK);
    y += font_h() + 6;

    /* Calendar header: "Month Year". */
    sprintf(buf, "%s %u", MONTHS[d.month - 1], d.year);
    ui_text_center(cx, y, cw, buf, C_TITLE);
    y += font_h() + 3;

    /* Calendar grid. */
    ndays = days_in_month(d.month, d.year);
    dow1  = ((int)d.dayofweek - ((int)d.day - 1)) % 7;
    if (dow1 < 0) dow1 += 7;

    cellw = (cw - 8) / 7;
    cellh = font_h() + 3;
    gx = cx + 4;
    gy = y;

    {   /* weekday header */
        int c;
        for (c = 0; c < 7; ++c)
            ui_text_center(gx + c * cellw, gy, cellw, DOW2[c], C_DKGRAY);
    }
    gy += cellh;

    for (day = 1; day <= ndays; ++day) {
        int idx = dow1 + day - 1;
        int col = idx % 7;
        int row = idx / 7;
        int x = gx + col * cellw;
        int cy = gy + row * cellh;
        char ds[4];
        sprintf(ds, "%d", day);
        if ((unsigned)day == d.day) {
            vid_fillrect(x, cy - 1, cellw, cellh, C_BLUE);
            ui_text_center(x, cy, cellw, ds, C_WHITE);
        } else {
            ui_text_center(x, cy, cellw, ds, C_BLACK);
        }
    }
}
