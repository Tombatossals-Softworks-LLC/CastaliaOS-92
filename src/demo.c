/* ======================================================================
 * demo.c - The Light Show (demoscene effects) for CASTALIA/386
 * ----------------------------------------------------------------------
 * Everything here writes the linear Mode-13h back buffer directly (via
 * vid_backbuffer()) for full-frame speed, drives the 256-entry DAC for
 * the palette tricks, and paces off the BIOS 18.2 Hz tick.  No floating
 * point: a parabolic integer sine table and an integer square root do all
 * the trigonometry, so it runs on a bare 386SX with no coprocessor.
 * ====================================================================== */
#include <dos.h>
#include <string.h>    /* _fmemset (fireworks sky) */
#include "demo.h"
#include "video.h"
#include "system.h"
#include "font.h"
#include "keyboard.h"
#include "mouse.h"

#define DW 320
#define DH 200

#define N_EFFECTS 17

/* ---- tiny helpers ---------------------------------------------------- */

static unsigned long g_seed = 0x2545F491UL;
static unsigned rnd(void)
{
    g_seed = g_seed * 1103515245UL + 12345UL;
    return (unsigned)(g_seed >> 16);          /* high bits: long period    */
}

static unsigned long ticks(void)
{
    return sys_ticks();               /* fast BDA read - see system.h      */
}

/* Present a finished effect frame.  Waiting out the vertical blank first
   is free on an idle 386 - the pacing loop below only halts the CPU
   anyway - and it removes the shear line every effect used to show, since
   the 64000-byte blit no longer races the raster beam. */
static void demo_present(void)
{
    vid_vsync();
    vid_present();
}

/* Parabolic integer sine (Bhaskara), amplitude +/-127, period 256.
   The Light Show's lookup tables live in FAR memory: DGROUP (near data,
   shared with the whole shell) is nearly full, and these are only read
   inside the demo loop where far addressing costs nothing noticeable. */
static signed char far g_sin[256];
static void build_sin(void)
{
    int i;
    for (i = 0; i < 256; ++i) {
        long d = (long)i * 360 / 256;         /* degrees 0..359            */
        int neg = 0;
        long num, den, v;
        if (d > 180) { d -= 180; neg = 1; }
        num = 4L * d * (180 - d);
        den = 40500L - d * (180 - d);
        v = num * 127 / den;                  /* 0..127                    */
        g_sin[i] = (signed char)(neg ? -v : v);
    }
}
#define SIN(a) (g_sin[(a) & 255])
#define COS(a) (g_sin[((a) + 64) & 255])

static int isqrt(long v)
{
    long x, y;
    if (v <= 0) return 0;
    x = v; y = (x + 1) / 2;
    while (y < x) { x = y; y = (x + v / x) / 2; }
    return (int)x;
}

/* Integer atan2 -> 0..255 for a full turn (a linear per-octant approximation,
   accurate enough for the tunnel). */
static int iatan2(int y, int x)
{
    int ax = (x < 0) ? -x : x;
    int ay = (y < 0) ? -y : y;
    int a;
    if (ax == 0 && ay == 0) return 0;
    if (ax >= ay) a = (int)((long)ay * 32 / (ax + 1));         /* 0..32   */
    else          a = 64 - (int)((long)ax * 32 / (ay + 1));    /* 32..64  */
    if (x >= 0) return (y >= 0) ? a : (256 - a);
    else        return (y >= 0) ? (128 - a) : (128 + a);
}

/* Captions are shown in the interactive Light Show, hidden in the
   screensaver (a bare effect is calmer). */
static bool_t g_caption_on = TRUE;

/* Centre a one-line caption in its own bar so it reads over any effect. */
static void caption(const char *name, u8 fg, u8 bg)
{
    char line[48];
    int i = 0, j;
    const char *tail = "  -  SPACE next   ESC exit";
    if (!g_caption_on)
        return;
    for (j = 0; name[j] && i < 18; ++j) line[i++] = name[j];
    for (j = 0; tail[j] && i < (int)sizeof(line) - 1; ++j) line[i++] = tail[j];
    line[i] = '\0';
    vid_fillrect(0, 0, DW, font_h() + 3, bg);
    font_draw((DW - font_text_width(line)) / 2, 2, line, fg);
}

/* =====================================================================
 * 1. Plasma - compute the field once, then flow it by rotating the DAC.
 * =================================================================== */
static signed char far pl_col[DW];
static signed char far pl_row[DH];
static signed char far pl_diag[DW + DH];
static u8  far pl_map[768];
static u8  far pl_rain[240 * 3];

/* The three-phase rainbow every effect builds its DAC ramp from.  It was
   written out four separate times, and that is exactly how one copy came
   to be missing the widening cast the other two carry: at n = 240 the
   index reaches 239 * 256 = 61184, well past a 16-bit int, so the whole
   upper half of the master rainbow was garbage.  Written once, it can
   only ever be wrong once.

   dst is FAR deliberately: pl_rain lives in far memory, and in the medium
   model a plain `u8 *` parameter is NEAR - passing a far array to it
   truncates the segment and scribbles into DGROUP instead, with no
   warning even at -wx.  Near stack ramps convert to far safely. */
static void rainbow_ramp(u8 far *dst, int n)
{
    int i;
    for (i = 0; i < n; ++i) {
        int a = (int)((long)i * 256 / n);      /* long: i*256 tops 16 bits */
        dst[i * 3 + 0] = (u8)(128 + SIN(a));
        dst[i * 3 + 1] = (u8)(128 + SIN(a + 85));
        dst[i * 3 + 2] = (u8)(128 + SIN(a + 171));
    }
}

/* Black -> red -> gold -> white, the classic fire/firework heat ramp.
   Also previously duplicated character for character. */
static void heat_ramp(u8 far *dst, int n)
{
    int i;
    /* THIRDS, with the blue climb spread over the whole last third so the
       ramp arrives at pure white exactly at the top entry.

       The original hardcoded 64/128/192 regardless of n, so at n = 240 a
       fifth of the ramp was identical white - 49 consecutive DAC entries,
       which is why a firework burst blew out to a flat core with no
       falloff.  My first correction used quarters and cooled the top
       toward pale blue: that fixed the plateau but broke the two callers
       that write slot 239 as the firework's white-hot head and as the
       fire's source row, turning both blue.  This keeps both properties -
       the longest identical run is 2 entries, and 239 is still white. */
    int t = (n < 3) ? 1 : n / 3;
    int tail = n - 2 * t - 1;
    for (i = 0; i < n; ++i) {
        int r, g, b;
        if (i < t)          { r = i * 255 / t;  g = 0;   b = 0; }
        else if (i < 2 * t) { r = 255; g = (i - t) * 255 / t;  b = 0; }
        else                { r = 255; g = 255;
                              b = (tail > 0) ? (i - 2 * t) * 255 / tail : 255; }
        dst[i * 3 + 0] = (u8)(r > 255 ? 255 : (r < 0 ? 0 : r));
        dst[i * 3 + 1] = (u8)(g > 255 ? 255 : (g < 0 ? 0 : g));
        dst[i * 3 + 2] = (u8)(b > 255 ? 255 : (b < 0 ? 0 : b));
    }
}

static void plasma_build_tables(void)
{
    int i;
    for (i = 0; i < DW; ++i)       pl_col[i]  = SIN(i * 2);
    for (i = 0; i < DH; ++i)       pl_row[i]  = SIN(i * 3);
    for (i = 0; i < DW + DH; ++i)  pl_diag[i] = SIN(i);
    for (i = 0; i < 768; ++i) {                 /* sum(-381..381)+384      */
        int c = (int)((long)i * 240 / 768);
        pl_map[i] = (u8)(16 + (c > 239 ? 239 : c));
    }
    rainbow_ramp(pl_rain, 240);                /* smooth rainbow master   */
}

static void plasma_init(u8 far *fb)
{
    int x, y;
    for (y = 0; y < DH; ++y) {
        unsigned o = (unsigned)y * DW;
        int rv = pl_row[y];
        for (x = 0; x < DW; ++x)
            fb[o + x] = pl_map[pl_col[x] + rv + pl_diag[x + y] + 384];
    }
    caption("PLASMA", C_WHITE, C_BLACK);
    demo_present();
}

static void plasma_step(unsigned frame)
{
    u8 out[240 * 3];
    int j, s = frame % 240;
    for (j = 0; j < 240; ++j) {
        int k = (j + s) % 240;
        out[j * 3 + 0] = pl_rain[k * 3 + 0];
        out[j * 3 + 1] = pl_rain[k * 3 + 1];
        out[j * 3 + 2] = pl_rain[k * 3 + 2];
    }
    video_set_dac(16, 240, out);                /* pixels unchanged: no blit */
}

/* =====================================================================
 * 2. Copper bars - stacked sinusoidal raster bars, the Amiga signature.
 * =================================================================== */
#define CB_BARS 8
#define CB_ROWS 20
static void copper_init(void)
{
    static const u8 hue[CB_BARS][3] = {
        {255, 40, 40}, {255, 150, 20}, {245, 230, 30}, {60, 220, 60},
        {40, 210, 210}, {60, 110, 255}, {170, 70, 240}, {245, 60, 170}
    };
    u8 ramp[CB_BARS * CB_ROWS * 3];
    int b, r, d, inten, o;
    for (b = 0; b < CB_BARS; ++b)
        for (r = 0; r < CB_ROWS; ++r) {
            d = r - CB_ROWS / 2;
            if (d < 0) d = -d;
            inten = 255 - d * 26;
            if (inten < 30) inten = 30;
            o = (b * CB_ROWS + r) * 3;
            ramp[o + 0] = (u8)((long)hue[b][0] * inten / 255);
            ramp[o + 1] = (u8)((long)hue[b][1] * inten / 255);
            ramp[o + 2] = (u8)((long)hue[b][2] * inten / 255);
        }
    video_set_dac(16, CB_BARS * CB_ROWS, ramp);
}

static void copper_step(unsigned frame)
{
    int b, r;
    vid_fillrect(0, 0, DW, DH, C_BLACK);
    for (b = 0; b < CB_BARS; ++b) {
        int yc = DH / 2 + (int)((long)SIN(frame * 2 + b * 30) * 78 / 127);
        for (r = 0; r < CB_ROWS; ++r) {
            int y = yc - CB_ROWS / 2 + r;
            if (y >= 0 && y < DH)
                vid_hline(0, y, DW, (u8)(16 + b * CB_ROWS + r));
        }
    }
    caption("COPPER BARS", C_WHITE, C_BLACK);
    demo_present();
}

/* =====================================================================
 * 3. Starfield - a warp field flying toward the viewer.
 * =================================================================== */
#define STARS 420
static int far st_x[STARS], far st_y[STARS], far st_z[STARS];

static void star_reset(int i)
{
    st_x[i] = (int)(rnd() % 640) - 320;
    st_y[i] = (int)(rnd() % 400) - 200;
    st_z[i] = (int)(rnd() % 255) + 1;
}
static void starfield_init(void)
{
    u8 ramp[16 * 3];
    int i;
    for (i = 0; i < 16; ++i) {                  /* depth-shaded greys      */
        int g = 60 + i * 13; if (g > 255) g = 255;
        ramp[i * 3 + 0] = ramp[i * 3 + 1] = ramp[i * 3 + 2] = (u8)g;
    }
    video_set_dac(16, 16, ramp);
    for (i = 0; i < STARS; ++i) star_reset(i);
}
static void starfield_step(u8 far *fb, unsigned frame)
{
    int i;
    vid_fillrect(0, 0, DW, DH, C_BLACK);
    for (i = 0; i < STARS; ++i) {
        int sx, sy, b;
        st_z[i] -= 6;
        if (st_z[i] < 1) { star_reset(i); st_z[i] = 255; }
        sx = DW / 2 + st_x[i] * 64 / st_z[i];
        sy = DH / 2 + st_y[i] * 64 / st_z[i];
        if (sx < 0 || sx >= DW || sy < 0 || sy >= DH) continue;
        b = (255 - st_z[i]) >> 4; if (b > 15) b = 15;
        fb[(unsigned)sy * DW + sx] = (u8)(16 + b);
        if (st_z[i] < 90 && sx + 1 < DW)        /* near stars get a body   */
            fb[(unsigned)sy * DW + sx + 1] = (u8)(16 + b);
    }
    caption("STARFIELD", C_WHITE, C_BLACK);
    (void)frame;
    demo_present();
}

/* =====================================================================
 * 4. Fire - the classic bottom-up flame, the back buffer IS the field.
 * =================================================================== */
/* Fire lives in DAC slots 16..255 (240 shades) so it never disturbs the
   theme slots 0..15 that the caption uses; slot 16 is the cold black. */
/* Per-pixel decay noise.  A CONSTANT decay under a symmetric 1-2-1 blur
   is just a low-pass filter run 199 times a frame: every trace of
   horizontal variance was gone within about twenty rows, and what was
   left was a perfectly uniform black-red-yellow ramp that read as a
   sunset rather than a fire.  Randomising the decay per pixel keeps the
   flame ragged; a per-ROW shift makes it lean and waver.  A table
   because a multiply per pixel is 63000 multiplies a frame, which a
   386SX cannot spare - this is one far byte read and an AND. */
static u8 far fire_nz[256];

static void fire_init(u8 far *fb)
{
    u8 ramp[240 * 3];
    int x, y, heat;
    for (x = 0; x < 256; ++x)
        fire_nz[x] = (u8)(rnd() & 255);
    heat_ramp(ramp, 240);
    video_set_dac(16, 240, ramp);
    for (y = 0; y < DH; ++y) {                   /* prewarm so it starts tall */
        unsigned o = (unsigned)y * DW;
        heat = 239 - (DH - 1 - y) * 2;
        if (heat < 0) heat = 0;
        for (x = 0; x < DW; ++x) fb[o + x] = (u8)(16 + heat);
    }
}
static void fire_step(u8 far *fb, unsigned frame)
{
    int x, y;
    unsigned o = (unsigned)(DH - 1) * DW;
    for (x = 0; x < DW; ++x)                     /* stoke the source row    */
        fb[o + x] = (u8)(16 + 239);
    /* Propagate by a JITTERED COPY, not by averaging.
       This used to be a symmetric 1-2-1 blur with a constant decay, run
       199 times a frame - which is a low-pass filter applied 199 times.
       Every trace of horizontal structure was gone within about twenty
       rows and what remained was a flawless black-red-yellow ramp: it
       read as a sunset, not a fire.  Randomising the decay under the
       blur did not help either; the blur simply averaged the noise back
       out again.
       The classic algorithm instead copies each pixel from the row below
       at a random horizontal offset and subtracts a random amount.  A
       pixel that draws a big decay stays cooler than its neighbours, the
       jitter smears that cool pocket sideways as it rises, and those
       pockets ARE the tongues.  It is also cheaper: one read per pixel
       rather than three, and no adds. */
    for (y = 0; y < DH - 1; ++y) {               /* propagate upward        */
        const u8 far *srow = fb + (unsigned)(y + 1) * DW;
        u8 far       *drow = fb + (unsigned)y * DW;
        int rs = (int)(rnd() & 255);   /* fresh noise phase per row, so the
                                          pattern cannot stack into stripes */
        for (x = 0; x < DW; ++x) {
            int nz = fire_nz[(x + rs) & 255];
            /* -1, 0, 0, +1: symmetric, so the flame does not drift. */
            int sx = x + (nz & 1) - ((nz >> 1) & 1);
            int v;
            if (sx < 0)   sx = 0;
            if (sx >= DW) sx = DW - 1;
            /* Two decay terms, and the second is the one that matters.
               Per-pixel white noise alone does NOT make tongues: summed
               over the ~150 rows a column lives for, its variance washes
               out and every column dies at the same height - which is
               why the last attempt still came out as a flat ramp with
               grain on it.  cool[] is a COLUMN-persistent term: the same
               extra decay for a band of columns, every row of the frame,
               so those bands burn out low while their neighbours run
               high.  That difference IS the tongue.  x >> 2 makes the
               bands a few pixels wide, and frame >> 2 drifts them. */
            v = (int)srow[sx] - (nz & 1)
                - (fire_nz[((x >> 2) + (int)(frame >> 2)) & 255] & 3);
            if (v < 16) v = 16;
            drow[x] = (u8)v;
        }
    }
    caption("FIRE", C_WHITE, C_BLACK);
    (void)frame;
    demo_present();
}

/* =====================================================================
 * 5. Boing - the Amiga checkerboard ball on a magenta grid.
 * =================================================================== */
#define BR 42
static int bo_cx, bo_cy, bo_vx, bo_vy, bo_phase, bo_spin;
static void boing_init(void)
{
    static const u8 pal[4 * 3] = {
        170,   0, 170,      /* 240 grid   magenta   */
         80,   0,  80,      /* 241 shadow dark purp */
        230,  40,  40,      /* 242 ball   red       */
        245, 245, 245       /* 243 ball   white     */
    };
    video_set_dac(240, 4, pal);
    bo_cx = 120; bo_cy = 90; bo_vx = 4; bo_vy = 0; bo_phase = 0; bo_spin = 1;
}
static void boing_step(u8 far *fb, unsigned frame)
{
    int gx, gy, dy, floorY = DH - 24;
    /* background: magenta grid on black */
    vid_fillrect(0, 0, DW, DH, C_BLACK);
    for (gx = 0; gx <= DW; gx += 20) vid_vline(gx < DW ? gx : DW - 1, 0, DH, 240);
    for (gy = 0; gy <= DH; gy += 20) vid_hline(0, gy < DH ? gy : DH - 1, DW, 240);

    /* physics */
    bo_vy += 1;
    bo_cx += bo_vx; bo_cy += bo_vy;
    if (bo_cx < BR + 2)      { bo_cx = BR + 2;      bo_vx = -bo_vx; bo_spin = -bo_spin; }
    if (bo_cx > DW - BR - 2) { bo_cx = DW - BR - 2; bo_vx = -bo_vx; bo_spin = -bo_spin; }
    if (bo_cy > floorY - BR) { bo_cy = floorY - BR; bo_vy = -13; }
    bo_phase = (bo_phase + bo_spin + 16) & 15;

    /* shadow (a flat ellipse on the floor, offset for the light) */
    for (dy = -4; dy <= 4; ++dy) {
        int hw = isqrt((long)BR * BR * (16 - dy * dy) / 16);
        if (hw > 0) vid_hline(bo_cx + 12 - hw, floorY + dy, hw * 2, 241);
    }

    /* ball: checkerboard sphere */
    for (dy = -BR; dy <= BR; ++dy) {
        int yy = bo_cy + dy;
        int hw = isqrt((long)BR * BR - (long)dy * dy);
        int lat = (dy + BR) * 8 / (2 * BR + 1);
        int dx;
        if (yy < 0 || yy >= DH) continue;
        for (dx = -hw; dx <= hw; ++dx) {
            int xx = bo_cx + dx;
            int lon, par;
            if (xx < 0 || xx >= DW) continue;
            lon = (dx + hw) * 8 / (2 * hw + 1);
            par = (lon + bo_phase + lat) & 1;
            fb[(unsigned)yy * DW + xx] = (u8)(par ? 242 : 243);
        }
    }
    caption("BOING", C_WHITE, C_BLACK);
    (void)frame;
    demo_present();
}

/* =====================================================================
 * 6. Rotozoomer - a rotating, pulsing texture.  The classic no-multiply
 *    inner loop: per row set up (u,v) and step by fixed-point (cos,sin);
 *    per pixel just two adds and a texture lookup.
 * =================================================================== */
static u8 far roto_tex[64 * 64];

static void roto_init(u8 far *fb)
{
    u8 ramp[64 * 3];
    int u, v;
    rainbow_ramp(ramp, 64);                       /* cyclic rainbow ramp     */
    video_set_dac(16, 64, ramp);
    for (v = 0; v < 64; ++v)                      /* an XOR plaid texture    */
        for (u = 0; u < 64; ++u)
            roto_tex[v * 64 + u] = (u8)(16 + ((u ^ v) & 63));
    (void)fb;
}

static void roto_step(u8 far *fb, unsigned frame)
{
    int a = (int)(frame * 3);
    int scale = 200 + (int)((long)SIN(frame * 2) * 120 / 127);   /* 80..320 */
    long cosv = (long)COS(a) * scale / 128;
    long sinv = (long)SIN(a) * scale / 128;
    long u_row = 0, v_row = 0;
    int x, y;
    for (y = 0; y < DH; ++y) {
        unsigned o = (unsigned)y * DW;
        long u = u_row, v = v_row;
        for (x = 0; x < DW; ++x) {
            fb[o + x] = roto_tex[(((v >> 8) & 63) << 6) | ((u >> 8) & 63)];
            u += cosv; v += sinv;
        }
        u_row -= sinv; v_row += cosv;
    }
    caption("ROTOZOOM", C_WHITE, C_BLACK);
    demo_present();
}

/* =====================================================================
 * 7. Tunnel - a precomputed depth+angle field flowed by rotating the DAC
 *    (the plasma palette machinery, a different field).
 * =================================================================== */
static void tunnel_init(u8 far *fb)
{
    u8 rain[240 * 3];
    int x, y, i;
    for (i = 0; i < 240 * 3; ++i) rain[i] = pl_rain[i];   /* far -> near      */
    video_set_dac(16, 240, rain);                /* the plasma rainbow      */
    for (y = 0; y < DH; ++y) {
        int dy = y - DH / 2;
        unsigned o = (unsigned)y * DW;
        for (x = 0; x < DW; ++x) {
            int dx = x - DW / 2;
            int dist = isqrt((long)dx * dx + (long)dy * dy);
            int depth = (dist == 0) ? 255 : (2560 / (dist + 1));
            int val = (depth + iatan2(dy, dx)) & 255;
            fb[o + x] = (u8)(16 + (int)((long)val * 239 / 255));
        }
    }
    caption("TUNNEL", C_WHITE, C_BLACK);
    demo_present();
}

/* The caption uses DAC slot 16 - matrix_init's own bright head green,
   (200,255,200) - not the theme's C_GREEN, which is (0,130,0) and made
   this the one unreadable caption in the whole Light Show.  (It is also
   the effect that follows TUNNEL in the walk order, which is how it got
   reported as "the TUNNEL caption is dim".)
   =====================================================================
 * 8. Matrix rain - falling columns of glyphs, a bright head trailing into
 *    dimming greens.  Each cell keeps its glyph; a column only repaints its
 *    trail on the frames its head actually advances, so it stays cheap.
 * =================================================================== */
#define MX_CELL  8
#define MX_COLS  (DW / MX_CELL)          /* 40 */
#define MX_ROWS  (DH / MX_CELL)          /* 25 */
#define MX_TRAIL 12                      /* streak length (head + 11 shades) */
#define MX_UNIT  4                       /* speed accumulator threshold      */

static u8  far mtx_ch[MX_COLS * MX_ROWS];  /* glyph in each cell (0 = empty)  */
static int far mtx_hy[MX_COLS];            /* head row per column (may be <0) */
static u8  far mtx_spd[MX_COLS];           /* rows advanced per MX_UNIT frames*/
static u8  far mtx_acc[MX_COLS];           /* speed accumulator               */

static const char far MX_POOL[] =
    "ABCDEFGHJKLMNPRSTUVWXYZ0123456789@#$%&*+=<>?";

static void mtx_put(int col, int row, char ch, u8 color)
{
    char b[2];
    b[0] = ch; b[1] = '\0';
    font_draw(col * MX_CELL, row * MX_CELL, b, color);
}

static void mtx_advance(int c)
{
    int hy, d, row;
    ++mtx_hy[c];
    hy = mtx_hy[c];
    if (hy >= 0 && hy < MX_ROWS) {                 /* new bright head glyph   */
        char ch = MX_POOL[rnd() % (sizeof(MX_POOL) - 1)];
        mtx_ch[hy * MX_COLS + c] = (u8)ch;
        mtx_put(c, hy, ch, 16);
    }
    for (d = 1; d < MX_TRAIL; ++d) {               /* redraw the fading trail */
        u8 ch;
        row = hy - d;
        if (row < 0 || row >= MX_ROWS) continue;
        ch = mtx_ch[row * MX_COLS + c];
        if (ch) mtx_put(c, row, (char)ch, (u8)(16 + d));
    }
    row = hy - MX_TRAIL;                            /* erase the fallen tail   */
    if (row >= 0 && row < MX_ROWS) {
        vid_fillrect(c * MX_CELL, row * MX_CELL, MX_CELL, MX_CELL, C_BLACK);
        mtx_ch[row * MX_COLS + c] = 0;
    }
    if (hy - MX_TRAIL >= MX_ROWS) {                 /* fully off-screen: reseed*/
        mtx_hy[c]  = -(int)(rnd() % (MX_ROWS + MX_TRAIL));
        mtx_spd[c] = (u8)(1 + rnd() % 4);
    }
}

static void matrix_init(u8 far *fb)
{
    u8 ramp[MX_TRAIL * 3];
    int i, c;
    ramp[0] = 200; ramp[1] = 255; ramp[2] = 200;   /* slot 16: white-green head*/
    for (i = 1; i < MX_TRAIL; ++i) {               /* slots 17..27: dimming    */
        int g = 235 - (i - 1) * 20;
        if (g < 20) g = 20;
        ramp[i * 3 + 0] = (u8)(g / 7);
        ramp[i * 3 + 1] = (u8)g;
        ramp[i * 3 + 2] = (u8)(g / 4);
    }
    video_set_dac(16, MX_TRAIL, ramp);
    for (i = 0; i < MX_COLS * MX_ROWS; ++i) mtx_ch[i] = 0;
    vid_fillrect(0, 0, DW, DH, C_BLACK);
    for (c = 0; c < MX_COLS; ++c) {
        mtx_hy[c]  = -(int)(rnd() % (MX_ROWS + MX_TRAIL));
        mtx_spd[c] = (u8)(1 + rnd() % 4);
        mtx_acc[c] = 0;
    }
    caption("MATRIX RAIN", (u8)(16 + 0), C_BLACK);  /* see note below */
    demo_present();
    (void)fb;
}

static void matrix_step(u8 far *fb, unsigned frame)
{
    int c;
    for (c = 0; c < MX_COLS; ++c) {
        mtx_acc[c] = (u8)(mtx_acc[c] + mtx_spd[c]);
        while (mtx_acc[c] >= MX_UNIT) { mtx_acc[c] -= MX_UNIT; mtx_advance(c); }
    }
    caption("MATRIX RAIN", (u8)(16 + 0), C_BLACK);
    (void)fb; (void)frame;
    demo_present();
}

/* =====================================================================
 * 9. Vector balls - two perpendicular rings of shaded spheres tumbling in
 *    3D.  No polygon filler: each ball is a depth-shaded disc, painter-
 *    sorted back to front.  Rotation is fixed-point off the sine table.
 * =================================================================== */
#define VB_N 32
static int far vb_x0[VB_N], far vb_y0[VB_N], far vb_z0[VB_N];
static int far vb_sx[VB_N], far vb_sy[VB_N], far vb_sz[VB_N];
static int far vb_ord[VB_N];

static void vball_init(void)
{
    u8 ramp[16 * 3];
    int i;
    for (i = 0; i < 16; ++i) {                    /* cool dark->bright ramp  */
        ramp[i * 3 + 0] = (u8)(i * 200 / 15);
        ramp[i * 3 + 1] = (u8)(40 + i * 200 / 15);
        ramp[i * 3 + 2] = (u8)(70 + i * 185 / 15);
    }
    video_set_dac(16, 16, ramp);
    for (i = 0; i < VB_N; ++i) {                  /* two perpendicular rings */
        int a  = (i % 16) * 256 / 16;
        int cs = COS(a), sn = SIN(a);
        if (i < 16) { vb_x0[i] = cs * 64 / 127; vb_y0[i] = sn * 64 / 127; vb_z0[i] = 0; }
        else        { vb_x0[i] = cs * 64 / 127; vb_y0[i] = 0;             vb_z0[i] = sn * 64 / 127; }
    }
}

static void vball_disc(u8 far *fb, int cx, int cy, int r, u8 col)
{
    int dy;
    for (dy = -r; dy <= r; ++dy) {
        int yy = cy + dy, hw, x0, x1, x;
        unsigned o;
        if (yy < 0 || yy >= DH) continue;
        hw = isqrt((long)r * r - (long)dy * dy);
        x0 = cx - hw; x1 = cx + hw;
        if (x0 < 0) x0 = 0;
        if (x1 >= DW) x1 = DW - 1;
        o = (unsigned)yy * DW;
        for (x = x0; x <= x1; ++x) fb[o + x] = col;
    }
}

/* A shaded sphere: concentric discs with the highlight drifting up-left,
   brightest last.  vball_disc fills ONE colour, so what the header calls
   "shaded spheres" was a ring of flat octagonal confetti - the depth cue
   was there (near balls bigger and brighter) but each ball had no form
   of its own.  Four shells is enough at r <= 6 and costs four isqrt
   walks over a disc that is at most 13 pixels across. */
static void vball_ball(u8 far *fb, int cx, int cy, int r, int rim)
{
    int k;
    for (k = 0; k < 4; ++k) {
        int rr = r - r * k / 5;
        int c  = rim + k * 2;
        if (rr < 1)  rr = 1;
        if (c > 15)  c = 15;
        vball_disc(fb, cx - r * k / 8, cy - r * k / 8, rr, (u8)(16 + c));
    }
}

static void vball_step(u8 far *fb, unsigned frame)
{
    int ax = (int)(frame * 2), ay = (int)(frame * 3);
    int sinX = SIN(ax), cosX = COS(ax), sinY = SIN(ay), cosY = COS(ay);
    int i, j;
    for (i = 0; i < VB_N; ++i) {
        long x0 = vb_x0[i], y0 = vb_y0[i], z0 = vb_z0[i];
        long x1 = (x0 * cosY - z0 * sinY) / 128;
        long z1 = (x0 * sinY + z0 * cosY) / 128;
        long y2 = (y0 * cosX - z1 * sinX) / 128;
        long z2 = (y0 * sinX + z1 * cosX) / 128;
        int  denom = (int)(z2 + 180);
        vb_sx[i]  = 160 + (int)(x1 * 140 / denom);
        vb_sy[i]  = 100 + (int)(y2 * 140 / denom);
        vb_sz[i]  = (int)z2;
        vb_ord[i] = i;
    }
    for (i = 1; i < VB_N; ++i) {                   /* insertion sort by depth */
        int k = vb_ord[i], kz = vb_sz[k];
        j = i - 1;
        while (j >= 0 && vb_sz[vb_ord[j]] > kz) { vb_ord[j + 1] = vb_ord[j]; --j; }
        vb_ord[j + 1] = k;
    }
    vid_fillrect(0, 0, DW, DH, C_BLACK);
    /* denom = z2 + 180, so a LARGER vb_sz is FARTHER - and all three
       depth cues were reading it backwards: far balls came out bigger,
       brighter, and (because the sort is ascending and this walked
       forwards) painted over the near ones.  Near is now bigger, near is
       brighter, and the walk runs back to front like wf_step's. */
    for (i = VB_N - 1; i >= 0; --i) {              /* draw far -> near        */
        int b  = vb_ord[i];
        /* The RIM shade, with headroom above it for the three brighter
           shells - a near ball at 15 would have had nowhere to go. */
        int sh = 9 - (vb_sz[b] + 64) * 9 / 128;
        int r  = 6 - (vb_sz[b] + 64) * 3 / 128;
        if (sh < 0) sh = 0;
        if (sh > 9) sh = 9;
        if (r < 2) r = 2;
        vball_ball(fb, vb_sx[b], vb_sy[b], r, sh);
    }
    caption("VECTOR BALLS", C_WHITE, C_BLACK);
    demo_present();
}

/* =====================================================================
 * 10. Sine scroller - the demoscene's signature: a giant greetings line
 *     rolling right-to-left on a sine wave, every glyph scaled 4x and
 *     rainbow-shaded per scan line from the plasma palette.
 * =================================================================== */
#define SCR_SCALE 4
#define SCR_CHW   (6 * SCR_SCALE)        /* advance of one big glyph       */

static const char far SCR_MSG[] =
    "   CASTALIA 92 ... A GRAPHICAL ENVIRONMENT FOR DOS ... "
    "TOMBATOSSALS SOFTWORKS PRESENTS THE LIGHT SHOW ... "
    "GREETINGS TO ALL 386 OWNERS OF THE WORLD ...      ";
#define SCR_LEN (int)(sizeof(SCR_MSG) - 1)

static void scroller_init(u8 far *fb)
{
    u8 rain[240 * 3];
    int i;
    for (i = 0; i < 240 * 3; ++i) rain[i] = pl_rain[i];   /* far -> near   */
    video_set_dac(16, 240, rain);
    vid_fillrect(0, 0, DW, DH, C_BLACK);
    (void)fb;
}

/* One big glyph at (x,y): each lit source pixel becomes a SCALE x SCALE
   block, coloured by its scan line so the letters shimmer vertically. */
static void scr_glyph(char ch, int x, int y, unsigned frame)
{
    int rows, row, col;
    const u8 far *g = font_glyph((unsigned char)ch, &rows);
    for (row = 0; row < rows; ++row) {
        u8 bits = g[row];
        u8 c;
        if (bits == 0)
            continue;
        c = (u8)(16 + (unsigned)(frame * 2 + row * 24) % 240);
        for (col = 0; col < 8; ++col)
            if (bits & (0x80 >> col))
                vid_fillrect(x + col * SCR_SCALE, y + row * SCR_SCALE,
                             SCR_SCALE, SCR_SCALE, c);
    }
}

static void scroller_step(u8 far *fb, unsigned frame)
{
    long sp = (long)frame * 3;                     /* scroll position       */
    int  first = (int)(sp / SCR_CHW);              /* leftmost char index   */
    int  xoff  = (int)(sp % SCR_CHW);
    int  k;
    vid_fillrect(0, 0, DW, DH, C_BLACK);
    for (k = 0; k <= DW / SCR_CHW + 1; ++k) {
        int  x  = k * SCR_CHW - xoff;
        int  y  = DH / 2 - (8 * SCR_SCALE) / 2
                + (int)((long)SIN(x + frame * 5) * 44 / 127);
        char ch = SCR_MSG[(unsigned)(first + k) % SCR_LEN];
        if (ch != ' ')
            scr_glyph(ch, x, y, frame + (unsigned)k * 12);
    }
    caption("SINE SCROLLER", C_WHITE, C_BLACK);
    (void)fb;
    demo_present();
}

/* =====================================================================
 * 11. Twister - a twisting square column: per scan line the four edges
 *     of the rotated square are projected, and the one or two visible
 *     faces become horizontal spans shaded by their apparent width.
 * =================================================================== */
static void twister_init(void)
{
    static const u8 hue[4][3] = {
        { 60, 200, 220 }, { 235, 90, 200 }, { 255, 170, 50 }, { 110, 120, 255 }
    };
    u8 ramp[4 * 16 * 3];
    int f, i;
    for (f = 0; f < 4; ++f)
        for (i = 0; i < 16; ++i) {
            int o = (f * 16 + i) * 3;
            ramp[o + 0] = (u8)((int)hue[f][0] * (40 + i * 4) / 100);
            ramp[o + 1] = (u8)((int)hue[f][1] * (40 + i * 4) / 100);
            ramp[o + 2] = (u8)((int)hue[f][2] * (40 + i * 4) / 100);
        }
    video_set_dac(16, 4 * 16, ramp);
}

static void twister_step(u8 far *fb, unsigned frame)
{
    int y, f;
    for (y = 0; y < DH; ++y) {
        /* Rotation angle: a base spin plus a travelling sine twist. */
        int a = (int)(frame * 2)
              + (int)((long)SIN(y + frame * 3) * 40 / 127);
        int e[4];
        for (f = 0; f < 4; ++f)
            e[f] = DW / 2 + (int)((long)SIN(a + f * 64) * 78 / 127);
        vid_hline(0, y, DW, C_BLACK);
        for (f = 0; f < 4; ++f) {
            int x0 = e[f], x1 = e[(f + 1) & 3];
            if (x1 > x0) {                          /* face toward us       */
                int sh = (x1 - x0) * 15 / 111;      /* wider = brighter     */
                if (sh > 15) sh = 15;
                vid_hline(x0, y, x1 - x0, (u8)(16 + f * 16 + sh));
            }
        }
    }
    caption("TWISTER", C_WHITE, C_BLACK);
    (void)fb;
    demo_present();
}

/* =====================================================================
 * 12. Fireworks - rockets climb from the bottom and burst into sprays of
 *     gravity-bound sparks.  The whole frame dims a little every tick, so
 *     everything that once glowed fades into the night sky - the classic
 *     heat-palette trick doing celebration duty.
 * =================================================================== */
#define FW_MAXP  224                   /* spark pool                      */
#define FW_MAXR  3                     /* simultaneous rockets            */

static int far fw_px[FW_MAXP], far fw_py[FW_MAXP];   /* Q4 fixed point    */
static int far fw_vx[FW_MAXP], far fw_vy[FW_MAXP];
static u8  far fw_heat[FW_MAXP];                     /* 0 = free slot     */

static int fw_rx[FW_MAXR], fw_ry[FW_MAXR];           /* rockets (Q4)      */
static int fw_rvy[FW_MAXR], fw_rtop[FW_MAXR];        /* speed, burst line */
static int fw_ron[FW_MAXR];

static void fireworks_init(u8 far *fb)
{
    u8 ramp[240 * 3];
    int i;
    heat_ramp(ramp, 240);              /* black -> red -> gold -> white */
    video_set_dac(16, 240, ramp);
    _fmemset(fb, 16, (unsigned)DW * DH);
    for (i = 0; i < FW_MAXP; ++i) fw_heat[i] = 0;
    for (i = 0; i < FW_MAXR; ++i) fw_ron[i]  = 0;
}

static void fw_burst(int cx, int cy)
{
    int n = 56 + (int)(rnd() % 32), i, made = 0;
    for (i = 0; i < FW_MAXP && made < n; ++i) {
        int a, spd;
        if (fw_heat[i])
            continue;
        a   = (int)(rnd() & 255);
        spd = 12 + (int)(rnd() % 36);              /* Q4 pixels/frame     */
        fw_px[i]   = cx;
        fw_py[i]   = cy;
        fw_vx[i]   = COS(a) * spd / 127;
        fw_vy[i]   = SIN(a) * spd / 127;
        fw_heat[i] = (u8)(200 + (rnd() % 40));
        ++made;
    }
}

static void fireworks_step(u8 far *fb, unsigned frame)
{
    int i;
    unsigned o;

    /* Everything cools: the sky swallows old light. */
    for (o = 0; o < (unsigned)DW * DH; ++o) {
        u8 v = fb[o];
        if (v > 16)
            fb[o] = (u8)((v >= 16 + 9) ? v - 9 : 16);
    }

    /* Rockets. */
    for (i = 0; i < FW_MAXR; ++i) {
        if (!fw_ron[i]) {
            if ((rnd() % 24) == 0) {               /* light a new fuse    */
                fw_rx[i]   = (int)(30 + rnd() % (DW - 60)) << 4;
                fw_ry[i]   = (DH - 2) << 4;
                fw_rvy[i]  = -(40 + (int)(rnd() % 28));
                fw_rtop[i] = 24 + (int)(rnd() % 70);
                fw_ron[i]  = 1;
            }
            continue;
        }
        fw_ry[i] += fw_rvy[i];
        if ((fw_ry[i] >> 4) <= fw_rtop[i]) {       /* bang!               */
            fw_burst(fw_rx[i], fw_ry[i]);
            fw_ron[i] = 0;
            continue;
        }
        {
            int x = fw_rx[i] >> 4, y = fw_ry[i] >> 4;
            /* x was checked in the spark loop below but not here: rockets
               happen to fly straight up, so it stayed in range purely by
               construction.  Give a rocket any horizontal drift and this
               writes outside the frame buffer. */
            if (x >= 0 && x < DW) {
                if (y >= 1 && y < DH)
                    fb[(unsigned)y * DW + x] = 255;    /* white-hot head  */
                if (y + 2 >= 0 && y + 2 < DH)
                    fb[(unsigned)(y + 2) * DW + x] = 170;  /* golden tail */
            }
        }
    }

    /* Sparks: fly, fall, cool, and land on the frame buffer. */
    for (i = 0; i < FW_MAXP; ++i) {
        int x, y;
        if (!fw_heat[i])
            continue;
        fw_vy[i] += 3;                             /* gravity (Q4)        */
        fw_px[i] += fw_vx[i];
        fw_py[i] += fw_vy[i];
        fw_heat[i] = (u8)((fw_heat[i] > 7) ? fw_heat[i] - 7 : 0);
        x = fw_px[i] >> 4; y = fw_py[i] >> 4;
        if (x < 0 || x >= DW || y < 1 || y >= DH) {
            fw_heat[i] = 0;
            continue;
        }
        if ((u8)(16 + fw_heat[i]) > fb[(unsigned)y * DW + x])
            fb[(unsigned)y * DW + x] = (u8)(16 + fw_heat[i]);
    }

    caption("FIREWORKS", C_WHITE, C_BLACK);
    (void)frame;
    demo_present();
}

/* =====================================================================
 * 13. Wireframe - a rainbow wire-cube tumbling on all three axes over a
 *     quiet starfield, its eight corners glowing depth-shaded spheres.
 *     Vector graphics off the same fixed-point rotation the vector balls
 *     use, so it needs no coprocessor: a clamped integer Bresenham draws
 *     each edge and vball_disc lights the corners.  Near corners (the ones
 *     perspective throws outward) are drawn larger and brighter, so the
 *     cube reads as solid depth even as a bare frame of lines.
 * =================================================================== */
#define WF_R     72                    /* half-span of the cube (world)   */
#define WF_CAM   260                   /* eye distance for the projection */
#define WF_STARS 64                    /* backdrop star count             */

static const signed char wf_v[8][3] = {
    {-1, -1, -1}, { 1, -1, -1}, { 1,  1, -1}, {-1,  1, -1},
    {-1, -1,  1}, { 1, -1,  1}, { 1,  1,  1}, {-1,  1,  1}
};
static const unsigned char wf_e[12][2] = {
    {0, 1}, {1, 2}, {2, 3}, {3, 0},    /* far face   */
    {4, 5}, {5, 6}, {6, 7}, {7, 4},    /* near face  */
    {0, 4}, {1, 5}, {2, 6}, {3, 7}     /* the pillars*/
};
static int far wf_sx[8], far wf_sy[8], far wf_sz[8];   /* projected verts */
static int far wf_stx[WF_STARS], far wf_sty[WF_STARS]; /* fixed stars     */

static void wf_init(u8 far *fb)
{
    u8 ramp[240 * 3];
    int i;
    rainbow_ramp(ramp, 224);                    /* 16..239: flowing rainbow */
    for (i = 0; i < 16; ++i) {                  /* 240..255: white -> grey  */
        int g = 255 - i * 13;
        if (g < 40) g = 40;
        ramp[(224 + i) * 3 + 0] = (u8)g;
        ramp[(224 + i) * 3 + 1] = (u8)g;
        ramp[(224 + i) * 3 + 2] = (u8)g;
    }
    video_set_dac(16, 240, ramp);
    for (i = 0; i < WF_STARS; ++i) {
        wf_stx[i] = (int)(rnd() % DW);
        wf_sty[i] = (int)(rnd() % DH);
    }
    (void)fb;
}

/* Clamped integer Bresenham straight into the linear back buffer.  The
   unsigned compares fold the "off the left/top" and "off the right/bottom"
   rejects into one test each, so an edge whose corners run off-screen is
   still safe to walk. */
static void wf_line(u8 far *fb, int x0, int y0, int x1, int y1, u8 col)
{
    int dx = (x1 > x0) ? x1 - x0 : x0 - x1;
    int dy = (y1 > y0) ? y1 - y0 : y0 - y1;
    int sx = (x0 < x1) ? 1 : -1;
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx - dy, e2;
    for (;;) {
        if ((unsigned)x0 < (unsigned)DW && (unsigned)y0 < (unsigned)DH)
            fb[(unsigned)y0 * DW + x0] = col;
        if (x0 == x1 && y0 == y1)
            break;
        e2 = err << 1;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 <  dx) { err += dx; y0 += sy; }
    }
}

static void wf_step(u8 far *fb, unsigned frame)
{
    int ax = (int)(frame * 2);                  /* pitch, yaw and roll     */
    int ay = (int)(frame * 3);
    int az = (int)frame;
    int sinX = SIN(ax), cosX = COS(ax);
    int sinY = SIN(ay), cosY = COS(ay);
    int sinZ = SIN(az), cosZ = COS(az);
    int i;

    vid_fillrect(0, 0, DW, DH, C_BLACK);
    for (i = 0; i < WF_STARS; ++i)              /* a quiet grey starfield  */
        fb[(unsigned)wf_sty[i] * DW + wf_stx[i]] = (u8)(248 + (i & 3));

    for (i = 0; i < 8; ++i) {                   /* rotate Z, then Y, then X */
        long x0 = (long)wf_v[i][0] * WF_R;
        long y0 = (long)wf_v[i][1] * WF_R;
        long z0 = (long)wf_v[i][2] * WF_R;
        long xz = (x0 * cosZ - y0 * sinZ) / 128;
        long yz = (x0 * sinZ + y0 * cosZ) / 128;
        long x1 = (xz * cosY - z0 * sinY) / 128;
        long z1 = (xz * sinY + z0 * cosY) / 128;
        long y2 = (yz * cosX - z1 * sinX) / 128;
        long z2 = (yz * sinX + z1 * cosX) / 128;
        int  denom = (int)(z2 + WF_CAM);
        wf_sx[i] = DW / 2 + (int)(x1 * 190 / denom);
        wf_sy[i] = DH / 2 + (int)(y2 * 190 / denom);
        wf_sz[i] = (int)z2;
    }

    for (i = 0; i < 12; ++i) {                  /* rainbow edges, flowing  */
        int a = wf_e[i][0], b = wf_e[i][1];
        unsigned h = ((unsigned)(i * 18) + frame * 3) % 224;
        wf_line(fb, wf_sx[a], wf_sy[a], wf_sx[b], wf_sy[b], (u8)(16 + h));
    }

    for (i = 0; i < 8; ++i) {                   /* glowing corner spheres  */
        int r  = 4 - wf_sz[i] * 3 / (2 * WF_R);      /* near = larger      */
        int sh = (wf_sz[i] + WF_R) * 15 / (2 * WF_R);/* near = brighter    */
        if (sh < 0)  sh = 0;
        if (sh > 15) sh = 15;
        if (r < 2)   r = 2;
        if (r > 7)   r = 7;
        vball_disc(fb, wf_sx[i], wf_sy[i], r, (u8)(240 + sh));
    }

    caption("WIREFRAME", C_WHITE, C_BLACK);
    (void)frame;
    demo_present();
}

/* =====================================================================
 * 14. Voxel landscape - a Comanche-style terrain flyover.  A procedurally
 *     generated 128x128 height+colour map (rolling hills, lakes, sandy
 *     shores, rock and snowy ranges) is ray-marched front-to-back one
 *     z-slice at a time, with a per-column y-buffer so a near ridge
 *     occludes everything behind it - the whole scene in integer maths off
 *     the shared sine table, no coprocessor.  A vertical sky gradient fills
 *     the top; the camera flies forward with a gentle bank, bob and pitch.
 *     The world coordinates are Q8 fixed point in longs (a wrapped 128-unit
 *     map overflows a 16-bit int), the projection is one divide per slice.
 * =================================================================== */
#define VX_MAP    128                  /* map is power-of-two: wrap with &  */
#define VX_ZFAR   120                  /* draw distance (slices)            */
#define VX_HAZE   10                   /* fog slots reserved per band       */
/* Vertical scale of the projection, and the reason it is this large.  A
   ring at distance z spans 2z map cells across all DW columns, so it is
   only worth drawing once z is big enough that a cell is a few pixels
   wide - at z=2 the entire screen is four cells, i.e. 80-pixel slabs.
   A ring is clipped off the bottom while (ph-h)*HSCALE/z/256 > DH-horizon,
   so HSCALE alone decides how far out the first *visible* ring sits:
   with a ~110-unit drop to the terrain, 2048 pushes it to z=7 and the
   near field lands at ~23 columns per cell.  380 put it at z=2. */
#define VX_HSCALE 2048
#define VX_WATER  96                   /* height below which is open water  */

/* Palette layout.  Terrain colour is baked per map cell, so the only place
   a distance cue can be applied is the drawcall, and the only thing the
   drawcall can do to a baked slot is add or subtract.  So every terrain
   band reserves its first VX_HAZE slots as a fog ramp running from the
   horizon haze up to the band's own dark end; the colour builder never
   emits an index below VX_HAZE, and the drawcall subtracts distance to
   walk the colour down into that band's fog.  Bands stay disjoint, so a
   far-away lake fades to haze instead of underflowing into the sky. */
#define VX_SKY    0                    /* palette band bases (slot-16)      */
#define VX_SKY_N  56
#define VX_WTR    56
#define VX_WTR_N  32
#define VX_SAND   88
#define VX_SAND_N 18
#define VX_GRASS  106
#define VX_GRSS_N 64
#define VX_ROCK   170
#define VX_ROCK_N 40
#define VX_SNOW   210
#define VX_SNOW_N 30                   /* bands sum to the 240 DAC slots    */
#define VX_FOG_R  170                  /* what the far distance dissolves   */
#define VX_FOG_G  195                  /* into: the pale end of the sky,    */
#define VX_FOG_B  225                  /* held back so terrain still reads  */

static u8  far vx_h[VX_MAP * VX_MAP];   /* height map  (0..239)             */
static u8  far vx_c[VX_MAP * VX_MAP];   /* colour map  (DAC slot per cell)  */
static int far vx_ybuf[DW];             /* per-column highest painted row   */

static int vx_clamp(int v, int lo, int hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

/* Write a linear RGB band of n entries into the DAC ramp at slot base,
   fading (r0,g0,b0) -> (r1,g1,b1) - the sky/water/grass/... gradients. */
static void vx_band(u8 *ramp, int base, int n,
                    int r0, int g0, int b0, int r1, int g1, int b1)
{
    int i, o;
    for (i = 0; i < n; ++i) {
        o = (base + i) * 3;
        ramp[o + 0] = (u8)(r0 + (r1 - r0) * i / (n - 1));
        ramp[o + 1] = (u8)(g0 + (g1 - g0) * i / (n - 1));
        ramp[o + 2] = (u8)(b0 + (b1 - b0) * i / (n - 1));
    }
}

/* A terrain band: VX_HAZE fog slots fading haze -> the band's dark end,
   then the band's own gradient.  Index VX_HAZE is written twice with the
   same colour, which is what makes the two ramps join without a seam. */
static void vx_terrain(u8 *ramp, int base, int n,
                       int r0, int g0, int b0, int r1, int g1, int b1)
{
    vx_band(ramp, base, VX_HAZE + 1,
            VX_FOG_R, VX_FOG_G, VX_FOG_B, r0, g0, b0);
    vx_band(ramp, base + VX_HAZE, n - VX_HAZE, r0, g0, b0, r1, g1, b1);
}

/* Map a terrain height to a slot inside band [base,base+n), leaving the
   fog reserve below it untouched.  v is the band-local brightness. */
static int vx_slot(int base, int n, int v)
{
    return base + vx_clamp(v + VX_HAZE, VX_HAZE, n - 1);
}

static void vx_init(u8 far *fb)
{
    u8 ramp[240 * 3];
    int x, y;

    vx_band   (ramp, VX_SKY,   VX_SKY_N,   40,  70, 150, 205, 225, 245);
    vx_terrain(ramp, VX_WTR,   VX_WTR_N,    8,  24,  72,  40, 120, 175);
    vx_terrain(ramp, VX_SAND,  VX_SAND_N, 186, 176, 120, 214, 205, 150);
    vx_terrain(ramp, VX_GRASS, VX_GRSS_N,  24,  66,  26, 120, 180,  80);
    vx_terrain(ramp, VX_ROCK,  VX_ROCK_N,  66,  58,  52, 158, 150, 140);
    vx_terrain(ramp, VX_SNOW,  VX_SNOW_N, 140, 152, 178, 255, 255, 255);
    video_set_dac(16, 240, ramp);

    for (y = 0; y < VX_MAP; ++y)               /* build the height field   */
        for (x = 0; x < VX_MAP; ++x) {
            long h = (long)SIN(x * 6) * SIN(y * 6) / 127;
            long mnt;
            int  H;
            h += (long)SIN(x * 13 + 40) * SIN(y * 11 + 90) / 127 / 2;
            h += (long)SIN(x * 23) * SIN(y * 29 + 50) / 127 / 4;
            H = 122 + (int)(h / 2);
            mnt = (long)SIN(x * 2 + 18) * SIN(y * 2 + 60) / 127;
            if (mnt > 36) H += (int)((mnt - 36) * 17 / 10);   /* snowy ridge */
            vx_h[y * VX_MAP + x] = (u8)vx_clamp(H, 8, 239);
        }

    for (y = 0; y < VX_MAP; ++y)               /* colour + slope lighting  */
        for (x = 0; x < VX_MAP; ++x) {
            int H  = vx_h[y * VX_MAP + x];
            int he = vx_h[y * VX_MAP + ((x + 1) & (VX_MAP - 1))];
            int hs = vx_h[((y + 1) & (VX_MAP - 1)) * VX_MAP + x];
            int slope = (he - H) + (hs - H) / 2;      /* sun from NW-ish    */
            int light = vx_clamp(6 - slope, 0, 12);
            int slot;
            if (H < VX_WATER)
                slot = vx_slot(VX_WTR, VX_WTR_N, H * 21 / VX_WATER);
            else if (H < VX_WATER + 8)
                slot = vx_slot(VX_SAND, VX_SAND_N, H - VX_WATER);
            else if (H < 150)
                slot = vx_slot(VX_GRASS, VX_GRSS_N,
                               (H - 104) * 40 / 46 + light);
            else if (H < 200)
                slot = vx_slot(VX_ROCK, VX_ROCK_N,
                               (H - 150) * 18 / 50 + light);
            else
                slot = vx_slot(VX_SNOW, VX_SNOW_N,
                               (H - 200) * 8 / 40 + light);
            vx_c[y * VX_MAP + x] = (u8)(16 + slot);
        }
    (void)fb;
}

/* Paint a vertical span [ytop,ybot) of one column - the voxel drawcall. */
static void vx_span(u8 far *fb, int x, int ytop, int ybot, u8 col)
{
    unsigned o;
    if (ytop < 0)  ytop = 0;
    if (ybot > DH) ybot = DH;
    o = (unsigned)ytop * DW + x;
    while (ytop < ybot) { fb[o] = col; o += DW; ++ytop; }
}

static void vx_step(u8 far *fb, unsigned frame)
{
    long camx = ((long)SIN(frame * 3) * 1200 / 127) & 0x7FFFL;   /* Q8 map  */
    long camy = ((long)frame * 110) & 0x7FFFL;                   /* fly on  */
    int  ang     = (int)((long)SIN(frame * 2) * 20 / 127);       /* bank    */
    /* Camera height, and the reason it is well above the map.  Terrain
       tops out at 239, so at 176 the flyover was threading between the
       ridges: every one of them projected as a flat wall filling a third
       of the frame, because a ridge at eye level lands at the horizon and
       paints everything below it.  296 clears the highest peak by 57 and
       looks down on it, which also pushes the nearest visible ring out to
       z=11 - about 14 columns per map cell instead of 80. */
    int  ph      = 296 + (int)((long)SIN(frame * 4) * 10 / 127); /* bob     */
    int  horizon = 62 + (int)((long)SIN(frame * 3) * 6 / 127);   /* pitch   */
    int  sinA = SIN(ang), cosA = COS(ang);
    int  x, z, i;

    for (i = 0; i < DH; ++i) {                   /* sky gradient backdrop   */
        int t = vx_clamp(i * (VX_SKY_N - 1) / (horizon + 1), 0, VX_SKY_N - 1);
        vid_hline(0, i, DW, (u8)(16 + VX_SKY + t));
    }
    for (x = 0; x < DW; ++x)
        vx_ybuf[x] = DH;

    z = 1;
    while (z < VX_ZFAR) {                         /* march near -> far       */
        long fwdx = (long)sinA * z * 2, fwdy = (long)cosA * z * 2;
        long rgtx = (long)cosA * z * 2, rgty = -(long)sinA * z * 2;
        long plx = camx + fwdx - rgtx, ply = camy + fwdy - rgty;
        long dx = (rgtx * 2) / DW, dy = (rgty * 2) / DW;
        int  invh = VX_HSCALE / z;
        /* One haze step per march ring, not per pixel. */
        int  haze = (int)(z * VX_HAZE / VX_ZFAR);
        if (haze > VX_HAZE) haze = VX_HAZE;
        for (i = 0; i < DW; ++i) {
            int idx = (int)((ply >> 8) & (VX_MAP - 1)) * VX_MAP
                    + (int)((plx >> 8) & (VX_MAP - 1));
            int yy = horizon + (int)(((long)(ph - vx_h[idx]) * invh) >> 8);
            if (yy < 0) yy = 0;
            if (yy < vx_ybuf[i]) {
                vx_span(fb, i, yy, vx_ybuf[i], (u8)(vx_c[idx] - haze));
                vx_ybuf[i] = yy;
            }
            plx += dx; ply += dy;
        }
        z += 1 + (z >> 5);                        /* level-of-detail march   */
    }

    caption("VOXEL LANDSCAPE", C_WHITE, C_BLACK);
    (void)frame;
    demo_present();
}

/* =====================================================================
 * 15. Corridor - a Wolfenstein-style raycast maze walk, the indoor twin of
 *     the voxel flyover.  A 16x16 grid (one row packed per int) is
 *     ray-marched per screen column to the first wall; a vertical span is
 *     drawn shaded by distance - near bright, far fading into the ceiling
 *     colour as fog - between a sky-blue ceiling gradient and a warm floor.
 *     An auto-pilot walks the corridors, turning when a wall is close
 *     ahead.  All integer maths off the shared sine table; the camera is
 *     Q8 fixed point and every column is one march, so it runs on a 386SX.
 * =================================================================== */
#define RC_MAPW   16
#define RC_WSCALE 240L                 /* wall height at one cell (px)      */
#define RC_STEP   24                   /* march step, Q8 (~0.094 cell)      */
#define RC_MAXC   20                   /* give up after this many cells     */
#define RC_BW0    0                    /* palette band bases (slot-16):     */
#define RC_BW1    64                   /* bright / dark wall sides (64 each)*/
#define RC_BCEIL  128                  /* ceiling gradient (40)             */
#define RC_BFLOOR 168                  /* floor gradient (40)               */

#define RC_TEXW   16                   /* brick texture is 16x16            */
#define RC_TEXH   16

static const unsigned rc_map[16] = {
    0xFFFF, 0x8001, 0xB7BD, 0xA425, 0xB575, 0xA111, 0x97D5, 0xF445,
    0x857D, 0x9D41, 0x915D, 0xB745, 0x8475, 0xBD05, 0x8179, 0xFFFF
};
/* The heading is one byte, so COS/SIN alone give one direction per table
   slot.  Across a 52/256 field of view that is 52 distinct rays for 320
   columns: walls came out in six-pixel slabs, with a THIRTEEN-pixel one
   dead ahead (truncation toward zero widens the centre bucket).  rc_sin
   is the same parabolic sine at +/-RC_ONE instead of +/-127, read at a Q8
   angle and interpolated between slots, so every column gets its own ray.
   The march also runs in Q16 rather than Q8: an integer Q8 step is only
   +/-24 units long, which re-quantised the direction to ~2.4 degrees and
   would have thrown the finer angles away again. */
#define RC_ONE    2048                 /* rc_sin amplitude (direction unit) */
#define RC_STEP16 (RC_STEP * 256 / RC_ONE)  /* march step per direction unit,
                                          Q16.  3 for RC_STEP 24; exact only
                                          while RC_STEP is a multiple of 8. */
static int far rc_sin[256];
static void rc_build_sin(void)
{
    int i;
    for (i = 0; i < 256; ++i) {
        long d = (long)i * 360 / 256;         /* degrees 0..359            */
        int  neg = 0;
        long num, den;
        if (d > 180) { d -= 180; neg = 1; }
        num = 4L * d * (180 - d);
        den = 40500L - d * (180 - d);
        num = num * RC_ONE / den;
        rc_sin[i] = (int)(neg ? -num : num);
    }
}
/* Sine at a Q8 angle: table slot in the high byte, 1/256 of a slot in the
   low.  Unsigned so a negative relative angle wraps the way the table
   wants it to. */
static int rc_sinq(unsigned aq)
{
    int i = (int)((aq >> 8) & 255);
    int a = rc_sin[i];
    return a + (rc_sin[(i + 1) & 255] - a) * (int)(aq & 255) / 256;
}
#define RC_SIN(aq) rc_sinq((unsigned)(aq))
#define RC_COS(aq) rc_sinq((unsigned)(aq) + 64u * 256u)

static u8 far rc_tex[RC_TEXW * RC_TEXH];  /* per-texel darkness (0 bright)  */
/* Ceiling and floor colour per scan line: a function of the row alone, so
   it is baked once instead of recomputed for all 320 columns. */
static u8 far rc_ceil[DH];
static u8 far rc_floor[DH];
static long rc_px, rc_py;              /* camera position, Q8 map units     */
static int  rc_ang;                    /* camera heading, 0..255            */

static int rc_wall(int cx, int cy)
{
    if (cx < 0 || cy < 0 || cx >= RC_MAPW || cy >= RC_MAPW) return 1;
    return (rc_map[cy] >> cx) & 1;
}

/* A running-bond brick texture as a "darkness" field: 0 = bright brick
   face, higher = mortar or shading.  Added to the distance shade per wall
   pixel, so the pattern shows near and washes into the fog far away. */
static void rc_build_tex(void)
{
    int ty, tx;
    for (ty = 0; ty < RC_TEXH; ++ty) {
        int row = ty / 4;
        for (tx = 0; tx < RC_TEXW; ++tx) {
            int xoff = (row & 1) ? 4 : 0;
            int mortar = (ty % 4 == 0) || (((tx + xoff) % 8) == 0);
            rc_tex[ty * RC_TEXW + tx] =
                (u8)(mortar ? 11 : (1 + ((tx * 5 + ty * 3) & 3)));
        }
    }
}

static void rc_band(u8 *ramp, int base, int i, int n,
                    int r0, int g0, int b0, int r1, int g1, int b1)
{
    int o = (base + i) * 3;
    ramp[o + 0] = (u8)(r0 + (r1 - r0) * i / (n - 1));
    ramp[o + 1] = (u8)(g0 + (g1 - g0) * i / (n - 1));
    ramp[o + 2] = (u8)(b0 + (b1 - b0) * i / (n - 1));
}

static void rc_init(u8 far *fb)
{
    u8 ramp[240 * 3];
    int i;
    for (i = 0; i < 64; ++i) rc_band(ramp, RC_BW0,   i, 64, 205,120, 80, 34,30,40);
    for (i = 0; i < 64; ++i) rc_band(ramp, RC_BW1,   i, 64, 150, 85, 58, 26,24,34);
    for (i = 0; i < 40; ++i) rc_band(ramp, RC_BCEIL, i, 40,  60, 62, 96, 26,26,42);
    for (i = 0; i < 40; ++i) rc_band(ramp, RC_BFLOOR,i, 40,  30, 27, 22, 96,86,68);
    video_set_dac(16, 208, ramp);
    rc_build_sin();
    rc_build_tex();
    for (i = 0; i < DH; ++i) {
        int c = (DH / 2 - i) * 40 / (DH / 2);
        if (c < 0) c = 0;
        if (c > 39) c = 39;
        rc_ceil[i] = (u8)(16 + RC_BCEIL + c);
        c = (i - DH / 2) * 40 / (DH / 2);
        if (c < 0) c = 0;
        if (c > 39) c = 39;
        rc_floor[i] = (u8)(16 + RC_BFLOOR + c);
    }
    rc_px = (1L << 8) + 128;            /* start centred in cell (1,1)       */
    rc_py = (1L << 8) + 128;
    rc_ang = 0;
    (void)fb;
}

static void rc_step(u8 far *fb, unsigned frame)
{
    int rdx0 = COS(rc_ang), rdy0 = SIN(rc_ang);
    int x;
    /* Q8 angles: da is the step between columns, rel the leftmost column's
       offset from the heading.  Both fit an int - 52*256/DW is 41, and
       160*41 is 6560 - so only the absolute angle needs the unsigned wrap. */
    int      da  = 52 * 256 / DW;
    int      rel = -(DW / 2) * da;
    unsigned abase;
    long     px16, py16;
    int      pcx0, pcy0;

    /* Auto-pilot: step forward; if a wall is close ahead, pivot instead. */
    {
        long nx = rc_px + (long)rdx0 * 34 / 128;
        long ny = rc_py + (long)rdy0 * 34 / 128;
        int  ax = (int)((nx + (long)rdx0 * 140 / 128) >> 8);
        int  ay = (int)((ny + (long)rdy0 * 140 / 128) >> 8);
        if (rc_wall(ax, ay)) rc_ang = (rc_ang + 5) & 255;
        else { rc_px = nx; rc_py = ny; }
    }

    abase = (unsigned)rc_ang << 8;
    px16  = rc_px << 8;                    /* the camera, once, in Q16      */
    py16  = rc_py << 8;
    pcx0  = (int)(rc_px >> 8);
    pcy0  = (int)(rc_py >> 8);

    for (x = 0; x < DW; ++x, rel += da) {
        unsigned ra  = abase + (unsigned)rel;
        int  rdx = RC_COS(ra), rdy = RC_SIN(ra);
        long sx = px16, sy = py16, dist = 0, cd, texStep, texPos;
        int  pcx = pcx0, pcy = pcy0;
        int  cells = 0, side = 0, fix, wallH, top, bot, shade, i, base, y0, y1, texX;
        /* Both step deltas are invariant across the whole march, yet this
           loop used to redo two 32-bit multiplies and two 32-bit divides
           on EVERY step - 20-60 steps x 320 columns is 6000-20000 needless
           32-bit divides a frame.  They also fit in 16 bits, but only
           just: |rd| <= RC_ONE = 2048 and RC_STEP16 = 3 give 6144, and
           the product overflows an int once RC_ONE * RC_STEP16 passes
           32767.  Widen both to long if either constant grows. */
        int stepx = rdx * RC_STEP16;
        int stepy = rdy * RC_STEP16;
        while (cells < RC_MAXC) {
            int cx, cy;
            sx += stepx;
            sy += stepy;
            dist += RC_STEP;
            cx = (int)(sx >> 16); cy = (int)(sy >> 16);
            if (cx != pcx || cy != pcy) {
                if (cx != pcx && cy == pcy)      side = 0;
                else if (cy != pcy && cx == pcx) side = 1;
                if (rc_wall(cx, cy)) break;
                if (cx != pcx) ++cells;
                if (cy != pcy) ++cells;
                pcx = cx; pcy = cy;
            }
        }
        fix = RC_COS(rel); if (fix < 128) fix = 128;    /* de-fisheye        */
        cd = dist * fix / RC_ONE; if (cd < 64) cd = 64;
        wallH = (int)(RC_WSCALE * 256 / cd); if (wallH < 1) wallH = 1;
        top = (DH - wallH) / 2; bot = top + wallH;
        /* The ceiling and floor shades depend only on the scan line, never
           on the column, so the same 200 multiply+divide pairs were being
           recomputed for all 320 columns - up to 64000 a frame.  rc_init
           bakes them into rc_ceil[]/rc_floor[] once. */
        for (i = 0; i < top && i < DH; ++i)             /* ceiling           */
            fb[(unsigned)i * DW + x] = rc_ceil[i];
        for (i = (bot > 0 ? bot : 0); i < DH; ++i)      /* floor             */
            fb[(unsigned)i * DW + x] = rc_floor[i];
        y0 = (top < 0) ? 0 : top;                       /* textured wall span*/
        y1 = (bot > DH) ? DH : bot;
        shade = (int)((cd >> 8) * 52 / 12);             /* distance shade    */
        if (shade < 0)  shade = 0;
        if (shade > 52) shade = 52;
        base = side ? RC_BW1 : RC_BW0;
        texX = (int)(((side ? (sx >> 8) : (sy >> 8)) & 255) * RC_TEXW / 256);
        if (texX < 0)          texX = 0;
        if (texX >= RC_TEXW)   texX = RC_TEXW - 1;
        texStep = ((long)RC_TEXH << 8) / wallH;         /* Q8 texels/pixel   */
        texPos  = (long)(y0 - top) * texStep;
        for (i = y0; i < y1; ++i) {
            int ty = (int)(texPos >> 8), sh;
            if (ty < 0)         ty = 0;
            if (ty >= RC_TEXH)  ty = RC_TEXH - 1;
            sh = shade + rc_tex[ty * RC_TEXW + texX];   /* brick modulates   */
            if (sh > 63) sh = 63;
            fb[(unsigned)i * DW + x] = (u8)(16 + base + sh);
            texPos += texStep;
        }
    }

    caption("CORRIDOR", C_WHITE, C_BLACK);
    (void)frame;
    demo_present();
}

/* =====================================================================
 * 16. Kaleidoscope - the plasma field folded into 8-fold dihedral symmetry
 *     (reflect each pixel into one octant with abs + a diagonal swap), then
 *     flowed by rotating the same rainbow DAC the plasma uses.  A hypnotic
 *     mandala for the price of one plasma fill; the step is plasma_step.
 * =================================================================== */
static void kaleido_init(u8 far *fb)
{
    int x, y;
    for (y = 0; y < DH; ++y) {
        int dy  = y - DH / 2;
        int fyr = (dy < 0) ? -dy : dy;
        unsigned o = (unsigned)y * DW;
        for (x = 0; x < DW; ++x) {
            int dx = x - DW / 2;
            int fx = (dx < 0) ? -dx : dx;
            int fy = fyr, t;
            if (fx < fy) { t = fx; fx = fy; fy = t; }   /* fold to one octant */
            fb[o + x] = pl_map[pl_col[fx] + pl_row[fy] + pl_diag[fx + fy] + 384];
        }
    }
    caption("KALEIDOSCOPE", C_WHITE, C_BLACK);
    demo_present();
}

/* =====================================================================
 * 17. Ribbons - the beloved screensaver of every early-90s office: two
 *     four-cornered polylines carom around the screen towing a dozen
 *     rainbow afterimages.  Each frame clears the canvas and redraws the
 *     whole trail (96 short Bresenham lines - cheaper than one plasma
 *     fill), so crossing ribbons never chew holes in each other.  State
 *     lives in FAR memory like every other effect's tables.
 * =================================================================== */
#define RB_N     2                     /* ribbons                          */
#define RB_PTS   4                     /* corners per ribbon               */
#define RB_TRAIL 12                    /* afterimages towed behind         */

static int far rb_x[RB_N][RB_TRAIL][RB_PTS];
static int far rb_y[RB_N][RB_TRAIL][RB_PTS];
static int rb_vx[RB_N][RB_PTS], rb_vy[RB_N][RB_PTS];
static int rb_head;                    /* newest slot in the trail ring    */

static void rb_poly(u8 far *fb, int r, int t, u8 col)
{
    int p;
    for (p = 0; p < RB_PTS; ++p) {
        int q = (p + 1) % RB_PTS;
        wf_line(fb, rb_x[r][t][p], rb_y[r][t][p],
                    rb_x[r][t][q], rb_y[r][t][q], col);
    }
}

static void ribbons_init(u8 far *fb)
{
    u8 ramp[240 * 3];
    int r, p, t;
    rainbow_ramp(ramp, 240);                    /* 16..255: a full rainbow */
    video_set_dac(16, 240, ramp);
    _fmemset(fb, 0, (unsigned)DW * DH);
    for (r = 0; r < RB_N; ++r)
        for (p = 0; p < RB_PTS; ++p) {
            int x = 20 + (int)(rnd() % (DW - 40));
            int y = 15 + (int)(rnd() % (DH - 30));
            for (t = 0; t < RB_TRAIL; ++t) {
                rb_x[r][t][p] = x;
                rb_y[r][t][p] = y;
            }
            rb_vx[r][p] = ((rnd() & 1) ? 1 : -1) * (2 + (int)(rnd() % 3));
            rb_vy[r][p] = ((rnd() & 1) ? 1 : -1) * (2 + (int)(rnd() % 3));
        }
    rb_head = 0;
    caption("RIBBONS", C_WHITE, C_BLACK);
}

static void ribbons_step(u8 far *fb, unsigned frame)
{
    int r, p, t;
    int next = (rb_head + 1) % RB_TRAIL;        /* oldest slot: reuse it   */

    for (r = 0; r < RB_N; ++r)                  /* advance the corners     */
        for (p = 0; p < RB_PTS; ++p) {
            int x = rb_x[r][rb_head][p] + rb_vx[r][p];
            int y = rb_y[r][rb_head][p] + rb_vy[r][p];
            if (x < 1)      { x = 1;      rb_vx[r][p] =  2 + (int)(rnd() % 3); }
            if (x > DW - 2) { x = DW - 2; rb_vx[r][p] = -2 - (int)(rnd() % 3); }
            if (y < 1)      { y = 1;      rb_vy[r][p] =  2 + (int)(rnd() % 3); }
            if (y > DH - 2) { y = DH - 2; rb_vy[r][p] = -2 - (int)(rnd() % 3); }
            rb_x[r][next][p] = x;
            rb_y[r][next][p] = y;
        }
    rb_head = next;

    _fmemset(fb, 0, (unsigned)DW * DH);         /* clear + full redraw     */
    for (t = RB_TRAIL - 1; t >= 0; --t) {       /* oldest first            */
        int slot = (rb_head + RB_TRAIL - t) % RB_TRAIL;
        for (r = 0; r < RB_N; ++r) {
            unsigned h = ((unsigned)frame * 2 + (unsigned)r * 120
                          + (unsigned)t * 7) % 240;
            rb_poly(fb, r, slot, (u8)(16 + h));
        }
    }
    caption("RIBBONS", C_WHITE, C_BLACK);       /* the clear ate the bar   */
    demo_present();                              /* steps that paint, blit  */
}

/* =====================================================================
 * Driver.
 * =================================================================== */
static void init_effect(int e, u8 far *fb)
{
    switch (e) {
    case 0: plasma_init(fb);   break;
    case 1: copper_init();     break;
    case 2: starfield_init();  break;
    case 3: fire_init(fb);     break;
    case 4: boing_init();      break;
    case 5: roto_init(fb);     break;
    case 6: tunnel_init(fb);   break;
    case 7: matrix_init(fb);   break;
    case 8: vball_init();      break;
    case 9: scroller_init(fb); break;
    case 10: twister_init();   break;
    case 11: fireworks_init(fb); break;
    case 12: wf_init(fb);      break;
    case 13: vx_init(fb);      break;
    case 14: rc_init(fb);      break;
    case 15: kaleido_init(fb); break;
    case 16: ribbons_init(fb); break;
    }
}
static void step_effect(int e, u8 far *fb, unsigned frame)
{
    switch (e) {
    case 0: plasma_step(frame);         break;
    case 1: copper_step(frame);         break;
    case 2: starfield_step(fb, frame);  break;
    case 3: fire_step(fb, frame);       break;
    case 4: boing_step(fb, frame);      break;
    case 5: roto_step(fb, frame);       break;
    case 6: plasma_step(frame);         break;   /* tunnel: rotate the DAC  */
    case 7: matrix_step(fb, frame);     break;
    case 8: vball_step(fb, frame);      break;
    case 9: scroller_step(fb, frame);   break;
    case 10: twister_step(fb, frame);   break;
    case 11: fireworks_step(fb, frame); break;
    case 12: wf_step(fb, frame);        break;
    case 13: vx_step(fb, frame);        break;
    case 14: rc_step(fb, frame);        break;
    case 15: plasma_step(frame);        break;   /* kaleidoscope: cycle DAC  */
    case 16: ribbons_step(fb, frame);   break;
    }
}

/* Fade to black, bring in effect e's palette and first frame, and fade
   back up: every entry into and hop between effects is a smooth dissolve
   instead of a hard palette snap.  (All no-ops with animations=false.) */
static void switch_effect(int e, u8 far *fb, unsigned *frame)
{
    video_fade_out();
    init_effect(e, fb);
    step_effect(e, fb, 0);
    *frame = 1;
    video_fade_in();
}

/* Wait until the BIOS tick advances (steady ~18 fps), polling the
   keyboard so a keypress is never missed during the wait.  The CPU is
   halted between interrupts - the Light Show idles at ~0% CPU too. */
static int pace_and_poll(unsigned long last)
{
    for (;;) {
        int k = kb_poll();
        if (k != KEY_NONE) return k;
        if (ticks() - last >= 1UL) return KEY_NONE;
        sys_idle();
    }
}

void demo_run(const char *theme)
{
    u8 far *fb = vid_backbuffer();
    int effect = 0;
    unsigned frame = 0;
    unsigned long last;

    if (fb == (u8 far *)0) {                     /* Mode 12h: not offered   */
        vid_fillrect(0, 0, SCREEN_W, SCREEN_H, C_DESKTOP);
        vid_fillrect(SCREEN_W / 2 - 150, SCREEN_H / 2 - 20, 300, 40, C_FACE);
        vid_bevel(SCREEN_W / 2 - 150, SCREEN_H / 2 - 20, 300, 40, C_HILIGHT, C_SHADOW);
        font_draw(SCREEN_W / 2 - 138, SCREEN_H / 2 - 6,
                  "The Light Show needs 256-colour mode (video=mode13h).",
                  C_BLACK);
        vid_present();
        kb_flush();
        while (kb_poll() == KEY_NONE)
            sys_idle();                      /* wait for any key, at 0% CPU */
        return;
    }

    build_sin();
    plasma_build_tables();
    kb_flush();
    switch_effect(effect, fb, &frame);           /* fades the desktop away  */
    last = ticks();

    for (;;) {
        int key;
        step_effect(effect, fb, frame);          /* plasma flows the DAC    */
        key = pace_and_poll(last);
        last = ticks();
        ++frame;
        if (key == KEY_ESC) break;
        if (key == KEY_RIGHT || key == KEY_SPACE || key == KEY_ENTER) {
            effect = (effect + 1) % N_EFFECTS;
            switch_effect(effect, fb, &frame);
        } else if (key == KEY_LEFT) {
            effect = (effect + N_EFFECTS - 1) % N_EFFECTS;
            switch_effect(effect, fb, &frame);
        }
    }

    video_fade_out();                            /* dissolve the effect     */
    video_set_theme(theme);                      /* restore the desktop     */
}

/* The idle screensaver: run effects with no captions, drifting from one to
   the next, until any key or mouse activity.  Restores the palette on exit. */
void demo_screensaver(const char *theme, bool_t have_mouse)
{
    u8 far *fb = vid_backbuffer();
    int effect, mx0 = 0, my0 = 0;
    unsigned frame = 0;
    unsigned long last;
    bool_t running = TRUE;

    if (fb == (u8 far *)0)
        return;                                  /* Mode 12h: not offered   */

    build_sin();
    plasma_build_tables();
    g_caption_on = FALSE;
    effect = (int)(ticks() % N_EFFECTS);
    if (have_mouse) { mouse_update(); mx0 = mouse_x(); my0 = mouse_y(); }
    kb_flush();
    switch_effect(effect, fb, &frame);           /* fades the desktop away  */
    last = ticks();

    while (running) {
        step_effect(effect, fb, frame);
        for (;;) {                               /* pace a tick, watch input */
            if (kb_poll() != KEY_NONE) { running = FALSE; break; }
            if (have_mouse) {
                mouse_update();
                if (mouse_buttons() ||
                    mouse_x() - mx0 > 3 || mx0 - mouse_x() > 3 ||
                    mouse_y() - my0 > 3 || my0 - mouse_y() > 3) {
                    running = FALSE; break;
                }
            }
            if (ticks() - last >= 1UL) break;
            sys_idle();                          /* sleep between interrupts */
        }
        last = ticks();
        if (++frame % 300 == 0) {                /* drift to the next effect */
            effect = (effect + 1) % N_EFFECTS;
            switch_effect(effect, fb, &frame);
        }
    }

    g_caption_on = TRUE;
    video_fade_out();                            /* dissolve back to desktop */
    video_set_theme(theme);
    kb_flush();
}
