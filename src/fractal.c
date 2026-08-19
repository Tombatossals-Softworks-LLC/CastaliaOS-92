/* ======================================================================
 * fractal.c - Mandelbrot explorer applet for CASTALIA/386
 * ----------------------------------------------------------------------
 * Fixed-point (Q12) integer Mandelbrot and Julia sets.  The escape-time
 * iteration count of each pixel indexes a reserved 128-colour DAC ramp
 * (slots 32..159, so the theme's slots 0..15 are untouched).  The picture is
 * computed a few rows per BIOS tick (fractal_tick) into a DOS-allocated far
 * buffer - grabbed on first open, so it never bloats the resident image -
 * and the window is repainted as it grows, so nothing ever freezes.
 * Left-click zooms in 2x; the J/M/R/O/I keys switch Julia sets, return to
 * the Mandelbrot, reframe, and zoom out/in.
 * ====================================================================== */
#include <stdio.h>
#include <dos.h>        /* _dos_allocmem, MK_FP                            */
#include "fractal.h"
#include "video.h"
#include "ui.h"
#include "font.h"

#define FW    192               /* image width  (fits a 200px window)        */
#define FH    128               /* image height                              */
#define SHIFT 12                /* fixed-point fractional bits (Q12)          */
#define ONE   (1L << SHIFT)
#define MAXIT 64                /* iteration ceiling                         */
#define BATCH 4                 /* rows computed per tick                     */
#define RAMP_BASE 32            /* first reserved DAC slot                   */

/* The 24 KB image buffer is allocated from DOS on first open (not a static
   far array), so it never bloats the resident program image. */
static u8 far  *g_fbuf = (u8 far *)0;
static unsigned g_fseg = 0;
static long   g_cx, g_cy;       /* view centre, Q12                          */
static long   g_span;           /* view width, Q12                           */
static int    g_row;            /* next row to compute (FH = finished)       */
static int    g_julia;          /* 0 = Mandelbrot, 1 = Julia                  */
static long   g_jcr, g_jci;     /* Julia constant c, Q12                     */
static int    g_jidx;           /* which preset Julia constant               */

/* A handful of pretty Julia constants (Q12: real*4096).  'J' cycles them. */
static const long far JULIA[][2] = {
    { -3277,   639 },   /* -0.8000 + 0.1560 i  (dendrite-ish)  */
    { -1638,  2458 },   /* -0.4000 + 0.6000 i                  */
    {  1167,    41 },   /*  0.2850 + 0.0100 i  (spirals)       */
    { -2875, -1574 },   /* -0.7018 - 0.3842 i  (the classic)   */
    { -3421,  -951 },   /* -0.8350 - 0.2321 i                  */
    {  1843,   585 }    /*  0.4500 + 0.1428 i                  */
};
#define NJULIA (int)(sizeof(JULIA) / sizeof(JULIA[0]))

/* A cheap triangle wave (0..254, peak at p=128) - three phase-shifted
   copies make a smooth rainbow without a sine table. */
static int tri(int p)
{
    p &= 255;
    return (p < 128) ? (p * 2) : ((255 - p) * 2);
}

static void set_ramp(void)
{
    u8  ramp[128 * 3];           /* on the stack - keep it out of DGROUP      */
    int i;
    ramp[0] = ramp[1] = ramp[2] = 0;              /* slot 32: interior black */
    for (i = 1; i < 128; ++i) {
        int a = i * 256 / 128;
        ramp[i * 3 + 0] = (u8)tri(a);
        ramp[i * 3 + 1] = (u8)tri(a + 85);
        ramp[i * 3 + 2] = (u8)tri(a + 170);
    }
    video_set_dac(RAMP_BASE, 128, ramp);
}

/* Escape-time iteration of z = z^2 + c from a starting z, all Q12.  The
   Mandelbrot set starts every point at z=0 with c = the pixel; a Julia set
   starts z = the pixel with c a fixed constant.  One routine serves both. */
static int iter(long zr, long zi, long cr, long ci)
{
    long zr2, zi2;
    int it = 0;
    while (it < MAXIT) {
        zr2 = (zr * zr) >> SHIFT;
        zi2 = (zi * zi) >> SHIFT;
        if (zr2 + zi2 > (4L << SHIFT))
            break;
        zi = ((zr * zi) >> (SHIFT - 1)) + ci;      /* 2*zr*zi + ci            */
        zr = zr2 - zi2 + cr;
        ++it;
    }
    return it;
}

static u8 colour(int it)
{
    if (it >= MAXIT)
        return RAMP_BASE;                          /* interior: black         */
    return (u8)(RAMP_BASE + 1 + (int)((long)it * 126 / (MAXIT - 1)));
}

static void compute_row(int y)
{
    long spanY = g_span * FH / FW;                 /* keep the aspect square  */
    long ci = g_cy - spanY / 2 + (long)y * spanY / FH;
    long x0 = g_cx - g_span / 2;
    int  x;
    unsigned o = (unsigned)y * FW;
    for (x = 0; x < FW; ++x) {
        long cr = x0 + (long)x * g_span / FW;
        int  it = g_julia ? iter(cr, ci, g_jcr, g_jci)   /* z0 = pixel        */
                          : iter(0, 0, cr, ci);          /* c  = pixel        */
        g_fbuf[o + x] = colour(it);
    }
}

/* Frame the whole current set and restart the render. */
static void reset_view(void)
{
    if (g_julia) {
        g_cx = 0; g_cy = 0; g_span = ONE * 16 / 5;  /* +/-1.6, the Julia frame */
    } else {
        g_cx = -ONE / 2; g_cy = 0; g_span = 3L * ONE;   /* the whole Mandelbrot */
    }
    g_row = 0;
}

/* Grab the image buffer from DOS the first time the applet opens (24 KB =
   1536 paragraphs).  Kept for the session once obtained. */
static bool_t ensure_buf(void)
{
    if (g_fbuf != (u8 far *)0)
        return TRUE;
    if (_dos_allocmem((FW * FH) / 16, &g_fseg) != 0) {
        g_fseg = 0;
        return FALSE;
    }
    g_fbuf = (u8 far *)MK_FP(g_fseg, 0);
    return TRUE;
}

void fractal_open(void)
{
    unsigned i;
    g_julia = 0;
    g_jidx  = 0;
    reset_view();
    if (!ensure_buf()) {
        g_row = FH;                                /* no buffer: render nothing */
        return;
    }
    for (i = 0; i < (unsigned)(FW * FH); ++i)
        g_fbuf[i] = RAMP_BASE;                     /* black until computed    */
    set_ramp();
}

bool_t fractal_key(int key)
{
    if (key == 'j' || key == 'J') {                /* next Julia set          */
        g_jidx = (g_jidx + 1) % NJULIA;
        g_jcr  = JULIA[g_jidx][0];
        g_jci  = JULIA[g_jidx][1];
        g_julia = 1;
        reset_view();
        return TRUE;
    }
    if (key == 'm' || key == 'M') {                /* back to the Mandelbrot  */
        g_julia = 0;
        reset_view();
        return TRUE;
    }
    if (key == 'r' || key == 'R') {                /* reframe the whole set   */
        reset_view();
        return TRUE;
    }
    if (key == 'o' || key == 'O' || key == '-') {  /* zoom out 2x             */
        if (g_span < (1L << 24))       /* past this the Q-math overflows   */
            g_span *= 2;
        g_row = 0;
        return TRUE;
    }
    if (key == 'i' || key == 'I' || key == '+' || key == '=') {  /* zoom in   */
        if (g_span > 8)                /* keep at least a sliver of world  */
            g_span /= 2;
        g_row = 0;
        return TRUE;
    }
    return FALSE;
}

/* Where the image sits inside the client (centred, small top margin). */
static void origin(const Rect *cl, int *ox, int *oy)
{
    *ox = cl->x + (cl->w - FW) / 2;
    *oy = cl->y + 2;
}

static void blit_rows(const Rect *cl, int r0, int r1)
{
    int ox, oy, y;
    if (g_fbuf == (u8 far *)0)
        return;
    origin(cl, &ox, &oy);
    for (y = r0; y < r1; ++y)
        vid_copy_row(ox, oy + y, g_fbuf + (unsigned)y * FW, FW);
}

/* Rows already blitted into the back buffer by the incremental path. */
static int g_drawn;

/* Paint the one-line status band under the image (shared by the full and
   the incremental draws) and report its screen rectangle. */
static void draw_status(const Rect *cl, Rect *r)
{
    int ox, oy;
    char buf[40];
    origin(cl, &ox, &oy);
    if (g_fbuf == (u8 far *)0)
        sprintf(buf, "Not enough memory for the fractal");
    else if (g_row < FH)
        sprintf(buf, "Rendering... %d%%", g_row * 100 / FH);
    else if (g_julia)
        sprintf(buf, "Julia %d/%d  J:next  M:set  O/I:zoom", g_jidx + 1, NJULIA);
    else
        sprintf(buf, "Mandelbrot  click:zoom  J:Julia");
    rect_set(r, cl->x, oy + FH + 3, cl->w, font_h());
    vid_fillrect(r->x, r->y, r->w, r->h, C_FACE);
    ui_text_center(cl->x, r->y, cl->w, buf, C_BLACK);
}

/* Compute the next BATCH of rows into the buffer.  TRUE while work remains,
   so the caller repaints the (framed) window through the normal path and
   the picture visibly grows a strip at a time. */
bool_t fractal_tick(void)
{
    int r1, y;
    if (g_fbuf == (u8 far *)0 || g_row >= FH)
        return FALSE;
    r1 = g_row + BATCH;
    if (r1 > FH) r1 = FH;
    for (y = g_row; y < r1; ++y)
        compute_row(y);
    g_row = r1;
    return TRUE;
}

void fractal_draw(const Rect *cl)
{
    int ox, oy;
    Rect sr;
    set_ramp();                                    /* restore ramp if reset   */
    origin(cl, &ox, &oy);
    vid_rect(ox - 1, oy - 1, FW + 2, FH + 2, C_DKGRAY);
    blit_rows(cl, 0, FH);                           /* whatever is computed    */
    g_drawn = g_row;                                /* back buffer is current  */
    draw_status(cl, &sr);
}

/* Incremental draw for the render ticks: blit ONLY the strip of rows the
   last fractal_tick() computed (plus the progress line) into the back
   buffer and report the touched rectangle.  The old path re-blitted the
   whole 24 KB image through per-pixel calls up to 18x a second while
   rendering; this moves ~4 rows per tick instead.  Returns FALSE when a
   full fractal_draw() is required (reset/zoom or nothing new). */
bool_t fractal_step_draw(const Rect *cl, Rect *dirty)
{
    int ox, oy;
    Rect sr;
    if (g_fbuf == (u8 far *)0 || g_drawn > g_row || g_drawn == g_row)
        return FALSE;
    origin(cl, &ox, &oy);
    blit_rows(cl, g_drawn, g_row);
    draw_status(cl, &sr);
    rect_set(dirty, ox, oy + g_drawn, FW, g_row - g_drawn);
    if (sr.x < dirty->x) { dirty->w += dirty->x - sr.x; dirty->x = sr.x; }
    if (sr.x + sr.w > dirty->x + dirty->w) dirty->w = sr.x + sr.w - dirty->x;
    dirty->h = (sr.y + sr.h) - dirty->y;
    g_drawn = g_row;
    return TRUE;
}

void fractal_click(const Rect *cl, int mx, int my)
{
    int ox, oy, px, py;
    long spanY;
    origin(cl, &ox, &oy);
    px = mx - ox;
    py = my - oy;
    if (px < 0 || px >= FW || py < 0 || py >= FH)
        return;
    spanY  = g_span * FH / FW;
    g_cx   = (g_cx - g_span / 2) + (long)px * g_span / FW;   /* recentre       */
    g_cy   = (g_cy - spanY  / 2) + (long)py * spanY  / FH;
    g_span = g_span / 2;                            /* zoom in 2x             */
    g_row  = 0;                                     /* recompute              */
}
