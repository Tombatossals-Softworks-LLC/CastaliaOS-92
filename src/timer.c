/* ======================================================================
 * timer.c - Stopwatch & kitchen timer for CASTALIA/386
 * ====================================================================== */
#include <stdio.h>
#include "timer.h"
#include "video.h"
#include "system.h"
#include "ui.h"
#include "font.h"
#include "keyboard.h"
#include "music.h"

/* ---- stopwatch state ------------------------------------------------- */
static int           sw_run;
static unsigned long sw_start;         /* tick the watch was started at   */
static unsigned long sw_acc;           /* ticks banked while stopped      */
static unsigned long sw_lap;           /* frozen lap value (tenths)       */
static int           sw_haslap;

/* ---- countdown state ------------------------------------------------- */
static int           tm_min = 3;       /* minutes to count (1..99)        */
static int           tm_run;
static unsigned long tm_end;           /* tick at which it hits zero      */
static int           tm_flash;         /* flashing "TIME!" phases left    */

/* What the window last displayed, so timer_tick() only reports a repaint
   when a digit would actually change. */
static unsigned long g_shown_tenths = 0xFFFFFFFFUL;
static long          g_shown_left   = -1;

/* The BIOS tick counter (0040:006C) resets to zero at midnight; a watch
   must never see time run backwards, so bank a day's worth of ticks
   whenever the raw counter jumps back and keep our own chain monotonic. */
static unsigned long mono_ticks(void)
{
    static unsigned long last = 0, base = 0;
    unsigned long t = sys_ticks();
    if (t < last)
        base += 0x1800B0UL;            /* BIOS ticks in 24 hours           */
    last = t;
    return t + base;
}

/* Button rectangles, refreshed by every draw for the click handler. */
static Rect b_startstop, b_lap, b_reset, b_minus, b_plus, b_go;

void timer_open(void)
{
    sw_run = 0; sw_acc = 0; sw_lap = 0; sw_haslap = 0;
    tm_run = 0; tm_flash = 0;
    g_shown_tenths = 0xFFFFFFFFUL;
    g_shown_left   = -1;
}

/* Elapsed stopwatch time in tenths of a second (ticks * 10 / 18.2). */
static unsigned long sw_tenths(void)
{
    unsigned long t = sw_acc;
    if (sw_run)
        t += mono_ticks() - sw_start;
    return t * 100UL / 182UL;
}

/* Countdown seconds remaining (negative once elapsed). */
static long tm_left(void)
{
    if (!tm_run)
        return (long)tm_min * 60L;
    return (long)(tm_end - mono_ticks()) * 10L / 182L;
}

bool_t timer_tick(void)
{
    bool_t changed = FALSE;
    if (sw_run && sw_tenths() != g_shown_tenths)
        changed = TRUE;
    if (tm_run) {
        long left = tm_left();
        if (left != g_shown_left)
            changed = TRUE;
        if (left <= 0) {               /* ding!  flash and ring three times */
            tm_run   = 0;
            tm_flash = 6;
            music_sfx(1046, 3);
            changed  = TRUE;
        }
    } else if (tm_flash > 0) {
        static unsigned long last_flash = 0;
        if (mono_ticks() - last_flash >= 5UL) {
            last_flash = mono_ticks();
            --tm_flash;
            if (tm_flash == 4 || tm_flash == 2)
                music_sfx(1046, 3);
            changed = TRUE;
        }
    }
    return changed;
}

static void sw_toggle(void)
{
    if (sw_run) {
        sw_acc += mono_ticks() - sw_start;
        sw_run  = 0;
    } else {
        sw_start = mono_ticks();
        sw_run   = 1;
    }
}

static void sw_reset(void)
{
    sw_run = 0; sw_acc = 0; sw_lap = 0; sw_haslap = 0;
}

static void tm_toggle(void)
{
    if (tm_run) {
        tm_run = 0;
    } else {
        tm_end   = mono_ticks() + (unsigned long)tm_min * 60UL * 182UL / 10UL;
        tm_run   = 1;
        tm_flash = 0;
    }
}

bool_t timer_key(int key)
{
    if (key == KEY_SPACE) { sw_toggle(); return TRUE; }
    if (key == 'l' || key == 'L') {
        sw_lap = sw_tenths(); sw_haslap = 1; return TRUE;
    }
    if (key == 'r' || key == 'R') { sw_reset(); return TRUE; }
    if (key == '+' || key == '=') {
        if (!tm_run && tm_min < 99) { ++tm_min; return TRUE; }
        return FALSE;
    }
    if (key == '-') {
        if (!tm_run && tm_min > 1) { --tm_min; return TRUE; }
        return FALSE;
    }
    if (key == KEY_ENTER) { tm_toggle(); return TRUE; }
    return FALSE;
}

void timer_click(const Rect *cl, int mx, int my)
{
    (void)cl;
    if (rect_contains(&b_startstop, mx, my)) sw_toggle();
    else if (rect_contains(&b_lap, mx, my)) { sw_lap = sw_tenths(); sw_haslap = 1; }
    else if (rect_contains(&b_reset, mx, my)) sw_reset();
    else if (rect_contains(&b_minus, mx, my)) { if (!tm_run && tm_min > 1)  --tm_min; }
    else if (rect_contains(&b_plus,  mx, my)) { if (!tm_run && tm_min < 99) ++tm_min; }
    else if (rect_contains(&b_go,    mx, my)) tm_toggle();
}

/* ---- drawing --------------------------------------------------------- */

/* Double-height digits for the main readout, from the active font face. */
static void big_readout(int x, int y, const char *s, u8 color)
{
    while (*s) {
        int rows, row, col;
        const u8 far *g = font_glyph((unsigned char)*s, &rows);
        for (row = 0; row < rows; ++row) {
            u8 bits = g[row];
            if (bits) {
                for (col = 0; col < 8; ++col)
                    if (bits & (0x80 >> col)) {
                        vid_fillrect(x + col * 2, y + row * 2, 2, 2, color);
                    }
            }
        }
        x += font_adv() * 2;
        ++s;
    }
}

static void fmt_tenths(char *buf, unsigned long tn)
{
    sprintf(buf, "%02lu:%02lu.%lu", tn / 600UL, (tn / 10UL) % 60UL, tn % 10UL);
}

void timer_draw(const Rect *cl)
{
    char buf[24];
    int  bw = font_adv() * 7 + 8;
    int  bh = font_h() + 6;
    int  y  = cl->y + 4;
    int  s  = font_h() / 8;             /* 1 in Mode 13h, 2 in Mode 12h    */
    unsigned long tn = sw_tenths();
    if (s < 1) s = 1;

    /* Stopwatch readout in a sunken well. */
    g_shown_tenths = tn;
    fmt_tenths(buf, tn);
    {
        int rw = 8 * font_adv() * 2 + 8, rh = font_h() * 2 + 6;
        int rx = cl->x + (cl->w - rw) / 2;
        vid_fillrect(rx, y, rw, rh, C_BLACK);
        ui_sink(rx, y, rw, rh);
        big_readout(rx + 5, y + 3, buf, sw_run ? C_GREEN : C_CYAN);
        y += rh + 3;
    }

    /* Lap line. */
    if (sw_haslap) {
        char lb[30];
        fmt_tenths(buf, sw_lap);
        sprintf(lb, "Lap  %s", buf);
        ui_text_center(cl->x, y, cl->w, lb, C_BLACK);
    } else {
        ui_text_center(cl->x, y, cl->w, "Space start/stop  L lap", C_DKGRAY);
    }
    y += font_h() + 3;

    /* Stopwatch buttons. */
    {
        int total = bw * 3 + 8, x = cl->x + (cl->w - total) / 2;
        rect_set(&b_startstop, x, y, bw, bh);
        ui_button(&b_startstop, sw_run ? "Stop" : "Start", sw_run);
        rect_set(&b_lap, x + bw + 4, y, bw, bh);
        ui_button(&b_lap, "Lap", FALSE);
        rect_set(&b_reset, x + 2 * (bw + 4), y, bw, bh);
        ui_button(&b_reset, "Reset", FALSE);
        y += bh + 4;
    }

    /* Divider, then the countdown timer. */
    vid_hline(cl->x + 4, y, cl->w - 8, C_SHADOW);
    vid_hline(cl->x + 4, y + 1, cl->w - 8, C_HILIGHT);
    y += 4;

    {
        long left = tm_run ? tm_left() : (long)tm_min * 60L;
        int  total, x;
        int  sq = bh;                   /* square +/- buttons              */
        char tb[16];
        if (left < 0) left = 0;
        g_shown_left = left;
        if (tm_flash & 1)
            sprintf(tb, " TIME! ");
        else
            sprintf(tb, "%02ld:%02ld", left / 60L, left % 60L);

        total = sq + 4 + font_adv() * 7 + 4 + sq + 6 + bw;
        x = cl->x + (cl->w - total) / 2;
        rect_set(&b_minus, x, y, sq, bh);
        ui_button(&b_minus, "-", FALSE);
        x += sq + 4;
        vid_fillrect(x, y, font_adv() * 7, bh, tm_flash ? C_RED : C_WHITE);
        ui_sink(x, y, font_adv() * 7, bh);
        ui_text_center(x, y + 3 * s, font_adv() * 7, tb,
                       tm_flash ? C_WHITE : C_BLACK);
        x += font_adv() * 7 + 4;
        rect_set(&b_plus, x, y, sq, bh);
        ui_button(&b_plus, "+", FALSE);
        x += sq + 6;
        rect_set(&b_go, x, y, bw, bh);
        ui_button(&b_go, tm_run ? "Halt" : "Go", tm_run);
        y += bh + 2;
        ui_text_center(cl->x, y, cl->w, "Timer: +/- minutes, Enter go",
                       C_DKGRAY);
    }
}
