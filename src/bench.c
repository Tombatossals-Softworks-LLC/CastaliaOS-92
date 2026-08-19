/* ======================================================================
 * bench.c - Benchmark applet for CASTALIA/386
 * ----------------------------------------------------------------------
 * Six quick, honest micro-benchmarks timed off the BIOS 18.2 Hz tick, each
 * in the SAME four-tick wall-clock window so the raw counts compare
 * cleanly: two integer kernels (an LCG mix and a heavier multiply/xor
 * hash), two memory kernels (a far dword copy and a far dword read) and
 * two video kernels (solid fills and horizontal spans).  Each raw count is
 * turned into an index against a 386SX/16 (which scores 1.00x), the six
 * fold into one composite CASTALIA SCORE with a machine-class verdict, and
 * the result bars CHARGE UP when the run finishes.  Enter or a click runs
 * it again.  All integer maths, no coprocessor, no assembly.
 * ====================================================================== */
#include <i86.h>
#include <stdio.h>
#include "bench.h"
#include "video.h"
#include "system.h"
#include "ui.h"
#include "font.h"
#include "keyboard.h"

#define NT       6
#define WINTICKS 4UL                    /* four BIOS ticks per sub-test      */
#define BAR_CAP  500UL                  /* index that fills a bar (486-class)*/
#define MBUF     2048                   /* dwords in each memory buffer (8KB)*/

/* Per-test raw counts and the 386SX/16 anchors (index 100 == "1.00x").  The
   integer/video anchors are the Oracle's measured kernels; the memory
   anchors are for these C far-pointer loops and are approximate. */
static unsigned long g_raw[NT];
static const unsigned long SCALE[NT] =
    { 260UL, 95UL, 100UL, 180UL, 45UL, 30UL };
static const char *const LAB[NT] =
    { "ALU", "Integer", "Mem copy", "Mem read", "Vid fill", "Vid line" };
static const u8 COL[NT] =
    { C_BLUE, C_CYAN, C_GREEN, C_YELLOW, C_TITLE, C_RED };

static bool_t        g_done  = FALSE;
/* volatile: the ALU/hash/read kernels store their result here and the copy
   kernel's destination is volatile too, so the -os optimiser cannot treat
   the timed work as dead stores and hollow out the loops (which would time
   an empty loop, not the kernel).  A plain static that is never read does
   NOT defeat dead-store elimination - it has to be volatile. */
static volatile unsigned long g_sink  = 0;
static unsigned long          g_start = 0;   /* tick results appeared (charge)*/

static unsigned long          far bench_a[MBUF];
static volatile unsigned long far bench_b[MBUF];

static unsigned long ticks(void) { return sys_ticks(); }

/* Spin to the next tick edge so every sub-test opens on a fresh tick. */
static unsigned long align_tick(void)
{
    unsigned long t = sys_ticks();
    while (sys_ticks() == t) ;
    return sys_ticks();
}

void bench_run(void)
{
    unsigned long t0, n;
    int i, k;

    for (k = 0; k < MBUF; ++k)                    /* prime the memory buffer */
        bench_a[k] = (unsigned long)k * 2654435761UL;

    /* 1. ALU: a 32-bit LCG mix, 250 rounds per rep. */
    n = 0; t0 = align_tick();
    while (ticks() - t0 < WINTICKS) {
        unsigned long acc = n;
        for (i = 0; i < 250; ++i) acc = acc * 69069UL + 1UL;
        g_sink = acc; ++n;
    }
    g_raw[0] = n;

    /* 2. Integer hash: a heavier multiply/shift/xor, 200 rounds per rep. */
    n = 0; t0 = align_tick();
    while (ticks() - t0 < WINTICKS) {
        unsigned long acc = n;
        for (i = 0; i < 200; ++i) {
            acc = acc * 2654435761UL;
            acc ^= acc >> 13;
            acc += 40503UL;
        }
        g_sink = acc; ++n;
    }
    g_raw[1] = n;

    /* 3. Memory copy: 8 KB of far dwords, A -> B. */
    n = 0; t0 = align_tick();
    while (ticks() - t0 < WINTICKS) {
        for (k = 0; k < MBUF; ++k) bench_b[k] = bench_a[k];
        ++n;
    }
    g_raw[2] = n;

    /* 4. Memory read: sum 8 KB of far dwords (read bandwidth). */
    n = 0; t0 = align_tick();
    while (ticks() - t0 < WINTICKS) {
        unsigned long s = 0;
        for (k = 0; k < MBUF; ++k) s += bench_a[k];
        g_sink = s; ++n;
    }
    g_raw[3] = n;

    /* 5. Video fill: full-width solid fills into the back buffer. */
    n = 0; t0 = align_tick();
    while (ticks() - t0 < WINTICKS) {
        vid_fillrect(0, 0, SCREEN_W, 100, (u8)(n & 0x0F));
        ++n;
    }
    g_raw[4] = n;

    /* 6. Video lines: 100 horizontal spans (the hline raster path). */
    n = 0; t0 = align_tick();
    while (ticks() - t0 < WINTICKS) {
        int yy;
        for (yy = 0; yy < 100; ++yy)
            vid_hline(0, yy, SCREEN_W, (u8)((n + yy) & 0x0F));
        ++n;
    }
    g_raw[5] = n;

    g_done  = TRUE;
    g_start = sys_ticks();
}

/* Index (x100) of a raw count against its 386SX/16 anchor. */
static unsigned long idx(int t)
{
    return SCALE[t] ? g_raw[t] * 100UL / SCALE[t] : 0UL;
}

/* Composite: the mean of the six indices. */
static unsigned long composite(void)
{
    unsigned long s = 0;
    int t;
    for (t = 0; t < NT; ++t) s += idx(t);
    return s / NT;
}

static const char *rating(unsigned long sc)
{
    if (sc == 0)     return "not yet measured";
    if (sc < 30)     return "XT / slow-286 class";
    if (sc < 80)     return "286 / early-386 class";
    if (sc < 170)    return "386-class throughput";
    if (sc < 500)    return "486-class throughput";
    if (sc < 1800)   return "Pentium-class throughput";
    return "modern silicon (emulated?)";
}

/* One result row: label, an index-scaled bar with a bright leading edge
   (animated by `charge`), and the raw count with its "xN.NN" index. */
static void row(int x, int y, int w, int t, int charge)
{
    char vs[28];
    int  labelw = 9 * font_adv();
    int  valw, bx, bw, bh = font_h();
    unsigned long ix = idx(t);

    sprintf(vs, "%lu (x%lu.%02lu)", g_raw[t], ix / 100UL, ix % 100UL);
    valw = font_text_width(vs);
    bx = x + labelw;
    bw = w - labelw - valw - 6;

    font_draw(x, y, LAB[t], C_BLACK);
    if (bw > 10) {
        ui_sink(bx, y - 1, bw, bh + 2);
        vid_fillrect(bx + 2, y + 1, bw - 4, bh - 2, C_DKGRAY);
        {
            unsigned long cap = (ix > BAR_CAP) ? BAR_CAP : ix;
            long fw = (long)(bw - 4) * (long)cap / (long)BAR_CAP;
            fw = fw * charge / 256;
            if (fw > bw - 4) fw = bw - 4;
            if (fw > 0) {
                vid_fillrect(bx + 2, y + 1, (int)fw, bh - 2, COL[t]);
                vid_vline(bx + 2 + (int)fw - 1, y + 1, bh - 2, C_WHITE);
            }
        }
    }
    font_draw(x + w - valw, y, vs, C_BLACK);
}

void bench_draw(const Rect *cl)
{
    int  x = cl->x + 6, w = cl->w - 12, y = cl->y + 5;
    int  lh = font_h() + 5, t, charge;
    unsigned long elapsed, sc;
    char buf[40];

    ui_text_center(cl->x, y, cl->w, "CASTALIA BENCHMARK", C_TITLE);
    y += font_h() + 4;

    if (!g_done) {
        ui_text_center(cl->x, y + 10, cl->w, "Running six tests...", C_BLACK);
        return;
    }

    elapsed = sys_ticks() - g_start;
    charge  = (elapsed * 42UL > 256UL) ? 256 : (int)(elapsed * 42UL);

    for (t = 0; t < NT; ++t) {
        row(x, y, w, t, charge);
        y += lh;
    }
    y += 3;

    /* The composite score, in a sunken readout, with the class verdict. */
    sc = composite();
    {
        int boxh = font_h() + 8;
        vid_fillrect(x, y, w, boxh, C_BLACK);
        ui_sink(x, y, w, boxh);
        sprintf(buf, "SCORE %lu", (sc * (unsigned long)charge) / 256UL);
        font_draw(x + 6, y + 4, buf, C_GREEN);
        {
            const char *r = rating(sc);
            font_draw(x + w - font_text_width(r) - 6, y + 4, r, C_CYAN);
        }
        y += boxh + 3;
    }

    ui_text_center(cl->x, y, cl->w,
                   "bars vs 386SX/16   Enter or click: run again", C_DKGRAY);
}

bool_t bench_animating(void)
{
    return (g_done && (sys_ticks() - g_start < 10UL)) ? TRUE : FALSE;
}

bool_t bench_key(int key)
{
    if (key == KEY_ENTER) {
        bench_run();
        return TRUE;
    }
    return FALSE;
}

bool_t bench_click(const Rect *cl, int mx, int my)
{
    (void)cl; (void)mx; (void)my;
    bench_run();
    return TRUE;
}
