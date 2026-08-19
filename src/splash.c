/* ======================================================================
 * splash.c - Boot splash for CASTALIA/386
 * ====================================================================== */
#include <i86.h>       /* int86 - BIOS tick counter                      */
#include <stdio.h>     /* sprintf (machine-specs line)                   */
#include <string.h>
#include "splash.h"
#include "video.h"
#include "ui.h"
#include "font.h"
#include "system.h"    /* system_cpu_name / system_total_ram_kb          */
#include "keyboard.h"  /* kb_poll / kb_flush - the "press a key" wait     */

/* ----------------------------------------------------------------------
 * Timing.  We pace the splash off the BIOS 18.2 Hz tick counter (INT 1Ah)
 * rather than Open Watcom's delay(): delay() calibrates a busy-loop to the
 * host CPU, which is fine on real 386-class iron but collapses to ~0 ms
 * under an emulator's dynamic core (the whole splash would flash by).  The
 * tick counter is wall-clock on every machine, real or emulated.
 * -------------------------------------------------------------------- */
static unsigned long splash_ticks(void)
{
    return sys_ticks();
}

/* Sleep until `n` BIOS ticks (~55 ms each) have elapsed.  sys_idle() halts
   the CPU between ticks instead of burning it in a spin loop. */
static void wait_ticks(unsigned n)
{
    unsigned long t0 = splash_ticks();
    while (splash_ticks() - t0 < (unsigned long)n)
        sys_idle();
}

/* DAC layout used during the splash (Mode 13h). */
#define SKY_BASE   16
#define SKY_N      96
#define CHR_BASE   116
#define CHR_N      40
#define COP_BASE   160
#define COP_N      32

typedef struct { int pos; u8 r, g, b; } GradStop;

/* Interpolate `count` RGB triples across the given stops (pos 0..255). */
static void grad_make(u8 *out, int count, const GradStop *st, int nst)
{
    int i;
    for (i = 0; i < count; ++i) {
        int p = (count > 1) ? i * 255 / (count - 1) : 0;
        int s = 0, p0, p1, t;
        while (s < nst - 2 && p > st[s + 1].pos)
            ++s;
        p0 = st[s].pos; p1 = st[s + 1].pos;
        /* (long) throughout: both (p-p0)*256 and diff*t overflow a 16-bit
           int on wide segments / large colour deltas. */
        t  = (p1 > p0) ? (int)((long)(p - p0) * 256 / (p1 - p0)) : 0;
        out[i * 3 + 0] =
            (u8)(st[s].r + (int)((long)(st[s + 1].r - st[s].r) * t / 256));
        out[i * 3 + 1] =
            (u8)(st[s].g + (int)((long)(st[s + 1].g - st[s].g) * t / 256));
        out[i * 3 + 2] =
            (u8)(st[s].b + (int)((long)(st[s + 1].b - st[s].b) * t / 256));
    }
}

/* Big logo text: each glyph scaled `scale`x.  grad != 0 fills by output
   scanline across DAC slots [gbase, gbase+gcount); otherwise solid. */
static void big_text(int x, int y, const char *s, int scale,
                     bool_t grad, u8 solid, int gbase, int gcount)
{
    while (*s) {
        int rows, row, sub, col;
        const u8 far *g = font_glyph((unsigned char)*s, &rows);
        for (row = 0; row < rows; ++row) {
            u8 bits = g[row];
            if (bits == 0)
                continue;
            for (sub = 0; sub < scale; ++sub) {
                int oy = y + row * scale + sub;
                u8  c  = grad
                       ? (u8)(gbase + (row * scale + sub) * gcount / (rows * scale))
                       : solid;
                for (col = 0; col < 8; ++col)
                    if (bits & (0x80 >> col))
                        vid_hline(x + col * scale, oy, scale, c);
            }
        }
        x += font_adv() * scale;
        ++s;
    }
}

static int big_width(const char *s, int scale)
{
    return (int)strlen(s) * font_adv() * scale;
}

/* Largest integer scale at which `s` still fits in frac/256 of the screen
   width.  Keeps the logo correct whatever the active font face/advance is. */
static int fit_scale(const char *s, int frac)
{
    int natural = big_width(s, 1);
    /* (long) cast is essential: SCREEN_W*frac overflows a 16-bit int. */
    int budget  = (int)((long)SCREEN_W * frac / 256);
    int sc = (natural > 0) ? budget / natural : 1;
    return (sc < 1) ? 1 : sc;
}

/* ---- Mode 13h: the full gradient glory --------------------------------*/
static void splash_gradient(void)
{
    static const GradStop sky[6] = {
        { 0,   6,  8, 40 }, { 80,  30, 40,120 }, { 140, 50,110,180 },
        { 188,210,150,110 }, { 220, 60, 40, 90 }, { 255,  6,  4, 18 }
    };
    /* Chrome: sky highlight above, warm ground reflection below, and the
       "horizon" where they meet.  The horizon used to bottom out at
       (36,72,170) - nearly black against this copper band - at 45% of the
       ramp.  A capital spans the whole 8-row cell, so on C/9/2 that read
       as chrome; but lowercase ink starts at row 2, so the same slot fell
       dead centre of the x-height and sliced every a/s/t/l/i in half.
       The ramp still turns over in the same place (that is what makes it
       metal) but its darkest step is now a mid steel-blue, so where it
       crosses an x-height it reads as shading rather than a cut. */
    static const GradStop chrome[7] = {
        {   0, 255,255,255 }, {  96,214,232,255 }, { 132, 70,100,175 },
        { 156,  40, 58,110 }, { 176,255,190, 90 }, { 208,255,236,180 },
        { 255,255,255,255 }
    };
    /* Copper band: vivid purple, but kept below white so chrome reads. */
    static const GradStop copper[5] = {
        { 0,  24, 10, 48 }, { 80,120, 40,180 }, { 128,200, 90,230 },
        { 176,120, 40,180 }, { 255, 24, 10, 48 }
    };
    u8 buf[96 * 3];
    int y, i, scale, lx, ly, bar, w;
    const char *logo = CAST_NAME;

    grad_make(buf, SKY_N, sky, 6);    video_set_dac(SKY_BASE, SKY_N, buf);
    grad_make(buf, CHR_N, chrome, 7); video_set_dac(CHR_BASE, CHR_N, buf);
    grad_make(buf, COP_N, copper, 5); video_set_dac(COP_BASE, COP_N, buf);

    /* Sky. */
    for (y = 0; y < SCREEN_H; ++y) {
        int sh = y * SKY_N / SCREEN_H;
        if (sh >= SKY_N) sh = SKY_N - 1;
        vid_hline(0, y, SCREEN_W, (u8)(SKY_BASE + sh));
    }

    /* A scatter of stars in the darker upper sky, for depth.  A tiny LCG
       gives a fixed, repeatable constellation - no Math/time dependency. */
    {
        unsigned seed = 0x2BD7u;
        int n, sky_top = SCREEN_H * 2 / 5;
        for (n = 0; n < 70; ++n) {
            int sx, sy;
            seed = (unsigned)(seed * 25173u + 13849u);
            sx = (int)((seed >> 3) % (unsigned)SCREEN_W);
            seed = (unsigned)(seed * 25173u + 13849u);
            sy = (int)((seed >> 3) % (unsigned)sky_top);
            vid_pixel(sx, sy, (u8)((n & 3) ? C_WHITE : C_LTBLUE));
        }
    }

    /* A copper band behind the logo. */
    scale = fit_scale(logo, 240);     /* 4 at 320 wide                     */
    w  = big_width(logo, scale);
    lx = (SCREEN_W - w) / 2;
    ly = SCREEN_H / 2 - 8 * scale / 2 - 14;
    bar = 8 * scale + 16;
    for (i = 0; i < bar; ++i)
        vid_hline(0, ly - 8 + i, SCREEN_W, (u8)(COP_BASE + i * COP_N / bar));

    /* Logo: a dark rim on all four sides, then the drop shadow, then the
       chrome face.  The rim is offset in *screen* pixels rather than by
       one logical pixel, so it stays a hairline at any scale - without it
       the pale end of the chrome dissolves straight into the copper. */
    {
        int r = 1;                    /* one screen pixel, always        */
        big_text(lx - r, ly,     logo, scale, FALSE, C_BLACK, 0, 0);
        big_text(lx + r, ly,     logo, scale, FALSE, C_BLACK, 0, 0);
        big_text(lx,     ly - r, logo, scale, FALSE, C_BLACK, 0, 0);
        big_text(lx,     ly + r, logo, scale, FALSE, C_BLACK, 0, 0);
    }
    big_text(lx + scale, ly + scale, logo, scale, FALSE, C_BLACK, 0, 0);
    big_text(lx, ly, logo, scale, TRUE, 0, CHR_BASE, CHR_N);

    /* Credits. */
    ui_text_center(0, ly + bar + 6, SCREEN_W, CAST_TAGLINE, C_WHITE);
    ui_text_center(0, ly + bar + 20, SCREEN_W,
                   "Tombatossals Softworks", C_CREAM);
    ui_text_center(0, ly + bar + 30, SCREEN_W,
                   "Anno MCMLXXXIX", C_LTBLUE);

    /* Detected machine specs - the premium "this is your hardware" touch. */
    {
        char specs[40];
        sprintf(specs, "%s    %lu KB RAM",
                system_cpu_name(), system_total_ram_kb());
        ui_text_center(0, SCREEN_H - 38, SCREEN_W, specs, C_CREAM);
    }

    /* Loading bar fills from the back buffer in a few quick steps.  The
       splash fades in from black first (a no-op when animations are off). */
    {
        int tw = 160, tx = (SCREEN_W - tw) / 2, ty = SCREEN_H - 22;
        ui_fill_face(tx - 2, ty - 2, tw + 4, 10);
        ui_sink(tx - 2, ty - 2, tw + 4, 10);
        vid_present();
        video_fade_in();
        for (i = 1; i <= 12; ++i) {
            vid_fillrect(tx, ty, tw * i / 12, 6, C_TITLE);
            vid_blit_rect(tx, ty, tw, 6);
            if (i & 1)
                wait_ticks(1);
        }
    }
    wait_ticks(5);
}

/* ---- Mode 12h: a clean 16-colour panel --------------------------------*/
static void splash_simple(void)
{
    const char *logo = CAST_NAME;
    int scale = fit_scale(logo, 190);        /* leaves a panel margin       */
    int lw = big_width(logo, scale);
    int lh = 8 * scale;                      /* normal 8x8 face, scaled     */
    int pw = lw + 80;
    int ph = lh + 110;                       /* logo + 2 credits + bar room */
    int px = (SCREEN_W - pw) / 2;
    int py = (SCREEN_H - ph) / 2;
    int lx = (SCREEN_W - lw) / 2;
    int ly = py + 24;
    int cy = ly + lh + 16;                   /* first credit baseline       */
    int i, tw, tx, ty;

    vid_clear(C_TITLE);
    ui_fill_face(px, py, pw, ph);
    ui_raise(px, py, pw, ph);
    vid_fillrect(px + 4, py + 4, pw - 8, 4, C_BLUE);

    big_text(lx + 2, ly + 2, logo, scale, FALSE, C_DKGRAY, 0, 0);   /* shadow */
    big_text(lx, ly, logo, scale, FALSE, C_TITLE, 0, 0);

    ui_text_center(px, cy,      pw, CAST_TAGLINE, C_BLACK);
    ui_text_center(px, cy + 12, pw, "Tombatossals Softworks", C_DKGRAY);
    {
        char specs[40];
        sprintf(specs, "%s    %lu KB RAM",
                system_cpu_name(), system_total_ram_kb());
        ui_text_center(px, cy + 26, pw, specs, C_BLUE);
    }

    tw = pw - 80; tx = (SCREEN_W - tw) / 2; ty = py + ph - 22;
    ui_sink(tx - 2, ty - 2, tw + 4, 10);
    vid_present();
    video_fade_in();
    for (i = 1; i <= 12; ++i) {
        vid_fillrect(tx, ty, tw * i / 12, 6, C_TITLE);
        vid_blit_rect(tx, ty, tw, 6);
        if (i & 1)
            wait_ticks(1);
    }
    wait_ticks(5);
}

void splash_show(void)
{
    if (video_is_big())
        splash_simple();
    else
        splash_gradient();
}

/* ---- The Windows-95 farewell screen ---------------------------------- */
static void amber_line(const char *s, int y, int sc)
{
    int w = big_width(s, sc);
    int x = (SCREEN_W - w) / 2;
    big_text(x + 1, y + 1, s, sc, FALSE, C_DKYELLOW, 0, 0);   /* bevel shadow */
    big_text(x,     y,     s, sc, FALSE, C_YELLOW,   0, 0);   /* amber face   */
}

void splash_shutdown(void)
{
    static const char *l1 = "It's now safe to";
    static const char *l2 = "turn off your";
    static const char *l3 = "computer.";
    int sc, gh, gap, y0;
    int cx = SCREEN_W / 2, u = 2 * ui_scale();

    /* Phase 1: the calm blue "shutting down" screen, the Castalia crest
       standing in for the Windows flag. */
    vid_clear(C_TITLE);
    ui_castle(cx - 9 * u, SCREEN_H / 3, u, TRUE);
    ui_text_center(0, SCREEN_H * 3 / 5, SCREEN_W,
                   "Please wait while your", C_WHITE);
    ui_text_center(0, SCREEN_H * 3 / 5 + font_h() + 3, SCREEN_W,
                   "computer shuts down...", C_LTBLUE);
    vid_present();
    kb_flush();                        /* ignore the Enter that chose Shut Down */
    wait_ticks(36);                    /* ~2 seconds on the 18.2 Hz tick     */

    /* Phase 2: the black "It's now safe to turn off" farewell. */
    vid_clear(C_BLACK);
    sc  = fit_scale(l1, 220);          /* the widest line sets the scale     */
    gh  = font_h() * sc;               /* glyph height at this scale         */
    gap = gh / 2;
    y0  = (SCREEN_H - (3 * gh + 2 * gap)) / 2 - gh / 2;
    if (y0 < 2) y0 = 2;
    amber_line(l1, y0,                     sc);
    amber_line(l2, y0 + (gh + gap),        sc);
    amber_line(l3, y0 + 2 * (gh + gap),    sc);
    ui_text_center(0, SCREEN_H - 20, SCREEN_W,
                   "Press any key to return to DOS.", C_LTBLUE);
    vid_present();

    kb_flush();                        /* drop anything pressed during phase 1 */
    while (kb_poll() == KEY_NONE)
        sys_idle();
    kb_flush();
}

/* ---- The blue-screen easter egg (Run "bsod" / "crash") --------------- */
void bsod_show(void)
{
    static const char *body[] = {
        "A fatal exception 0E has occurred at",
        "0028:C001CA57 in VXD CASTALIA(01) +",
        "00010E36.  The current application will",
        "be terminated.",
        "",
        "*  Press any key to terminate the current",
        "   application.",
        "*  Press CTRL+ALT+DEL again to restart your",
        "   computer.  You will lose any unsaved",
        "   information in all applications.",
        ""
    };
    int n = (int)(sizeof(body) / sizeof(body[0]));
    int i, y, lx = SCREEN_W / 16;
    const char *ttl = " Castalia ";
    int tw = font_text_width(ttl);
    int tx = (SCREEN_W - tw) / 2, ty = SCREEN_H / 5;

    vid_clear(C_BLUE);
    vid_fillrect(tx, ty, tw, font_h() + 2, C_FACE);   /* the inverse title  */
    font_draw(tx, ty + 1, ttl, C_BLUE);
    y = ty + (font_h() + 2) + font_h();
    for (i = 0; i < n; ++i) {
        font_draw(lx, y, body[i], C_WHITE);
        y += font_h() + 1;
    }
    ui_text_center(0, y + font_h(), SCREEN_W,
                   "Press any key to continue _", C_WHITE);
    vid_present();
    kb_flush();
    while (kb_poll() == KEY_NONE)
        sys_idle();
    kb_flush();
}
