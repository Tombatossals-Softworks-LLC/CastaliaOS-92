/* ======================================================================
 * video.c - VGA graphics subsystem for CASTALIA/386
 * ----------------------------------------------------------------------
 * Two video back-ends live behind one set of vid_* primitives:
 *
 *   Mode 13h  320x200x256  - a LINEAR framebuffer at A000:0000, one byte
 *                            per pixel.  The back buffer is a single
 *                            64000-byte DOS block; present() is one far
 *                            memcpy.  Simple and fast; the MVP default.
 *
 *   Mode 12h  640x480x16    - PLANAR.  Memory is four 1-bit planes; a
 *                            pixel's colour bits live in the same byte
 *                            offset across planes 0..3, selected with the
 *                            Sequencer Map Mask (port 3C4h/3C5h, index 2)
 *                            for writes.  Our back buffer is four 38400-
 *                            byte plane buffers in RAM; present() copies
 *                            each plane to VGA with that plane selected.
 *                            The software cursor pokes VGA directly using
 *                            the Graphics Controller Set/Reset + Bit Mask
 *                            (port 3CEh/3CFh) registers and then RESTORES
 *                            their defaults, because present()/blit assume
 *                            the default write path.
 *
 * This is the ONLY file that touches VGA hardware or its registers.  All
 * higher layers draw through the primitives and never know which mode is
 * active - that is what makes Mode 12h a single-file addition.
 * ====================================================================== */
#include <i86.h>
#include <dos.h>
#include <string.h>
#include <conio.h>     /* outp                                           */
#include "video.h"

#define MODE_13 13
#define MODE_12 12

/* Runtime screen size (declared in video.h via SCREEN_W/SCREEN_H). */
int castalia_screen_w = 320;
int castalia_screen_h = 200;

static int g_mode      = MODE_13;
static int g_bios_mode = 0x13;

static u8 far *g_vga = (u8 far *)0;   /* A000:0000 in both modes          */

/* Mode 13h back buffer. */
static u8 far  *g_back     = (u8 far *)0;
static unsigned g_back_seg = 0;
#define FRAME_BYTES 64000U

/* Mode 12h plane buffers. */
static u8 far  *g_plane[4];
static unsigned g_plane_seg = 0;
#define PLANE_STRIDE 80               /* 640 / 8 bytes per scan line      */
#define PLANE_BYTES  38400U           /* 80 * 480                         */

/* Full-frame scene cache (see video.h).  One DOS block sized like the
   back buffer; zero cost when the allocation fails. */
static unsigned g_cache_seg = 0;

/* ----------------------------------------------------------------------
 * Palette tables (16 colours, RGB 0..255; scaled to the DAC's 0..63).
 * The slot order matches the C_* indices in video.h.
 * -------------------------------------------------------------------- */
/* "Classic" - the authentic Windows-95 scheme: teal desktop, silver
   (C0C0C0) 3D face, pure-white highlight and 808080 shadow for crisp
   bevels, navy title bars, black text.  The default. */
/* C_CREAM (slot 15) is a real cream, not a third copy of white: with
   slots 5, 6 and 15 all 255,255,255 the Cardfile's two stacked cards and
   the Agenda's checkboxes-on-board collapsed into flat outlines.  The
   Redmond and DOS palettes already used 255,255,224 here. */
static const u8 far pal_classic[16 * 3] = {
      0,  0,  0,    0,  0,128,    0,128,128,  192,192,192,
    128,128,128,  255,255,255,  255,255,255,   58,110,165,
    168,  0,  0,  232,200, 32,  160,120,  0,    0,128,  0,
     64, 64, 64,    0,  0,192,    0,168,168,  255,255,224
};
static const u8 far pal_penumbra[16 * 3] = {
      0,  0,  0,   24, 24, 64,   32, 40, 56,  160,160,176,
     96, 96,112,  208,208,224,  240,240,255,   80,112,200,
    200, 64, 64,  216,184, 64,  144,104, 24,   64,176, 96,
     64, 64, 80,   64, 96,232,   64,168,184,  224,224,240
};
static const u8 far pal_beige[16 * 3] = {
      0,  0,  0,   72, 56,104,  160,144,112,  214,206,184,
    150,142,120,  238,232,216,  255,252,240,  120,104,168,
    176, 48, 48,  224,196, 96,  168,128, 40,   72,144, 64,
    104, 96, 80,   72, 56,168,   96,160,160,  255,250,224
};
static const u8 far pal_winsteel[16 * 3] = {
      0,  0,  0,    8, 32, 96,   96,112,128,  188,196,204,
    120,132,144,  224,230,238,  255,255,255,   72,120,200,
    176, 40, 40,  232,208, 96,  150,120, 16,   32,152, 96,
     80, 92,104,   32, 72,216,   64,168,192,  240,246,255
};

/* "Moncloa 92" - a warm institutional scheme: sandy desktop, maroon
   title bars, warm grays.  An early-90s Iberian office look. */
static const u8 far pal_moncloa[16 * 3] = {
      0,  0,  0,  128,  0, 32,  170,154,118,  202,198,186,
    142,138,126,  238,234,222,  255,252,245,   96,112,168,
    176, 32, 32,  226,194, 84,  168,128, 32,   56,128, 72,
     98, 92, 82,   32, 64,160,   84,152,160,  252,248,228
};

/* "Workbench" - a homage to the 1.x Amiga desktop: a blue backdrop, gray
   gadgets, white/orange accents.  The look this whole project salutes. */
static const u8 far pal_workbench[16 * 3] = {
      0,  0,  0,   40, 72,150,   60,100,170,  196,196,196,
    124,124,124,  236,236,236,  255,255,255,  150,180,230,
    200, 48, 48,  255,150, 40,  210,110,  0,   64,150, 80,
     72, 72, 72,   40, 72,200,   96,176,196,  255,236,205
};

/* "Ocean" - a cool deep-sea scheme: teal-blue desktop, cool grays. */
static const u8 far pal_ocean[16 * 3] = {
      4, 12, 24,   12, 60, 96,   24, 86,104,  186,200,206,
    118,134,142,  226,238,242,  248,252,255,  110,170,200,
    188, 72, 72,  230,200,110,  170,130, 40,   56,160,140,
     60, 76, 84,   30, 96,176,   80,180,190,  232,244,246
};

/* "Rose" - a warm, elegant mauve/rose scheme. */
static const u8 far pal_rose[16 * 3] = {
     24, 12, 18,  120, 40, 80,  150,110,124,  214,200,206,
    150,130,138,  240,230,234,  255,250,252,  150,130,190,
    196, 60, 76,  230,190,120,  180,120, 60,  110,150, 90,
     90, 74, 82,  110, 80,170,  150,150,180,  252,240,244
};

/* "Midnight" - a deep indigo/violet night: near-black desktop, royal violet
   title bars, cool lilac chrome, electric-periwinkle accents. */
static const u8 far pal_midnight[16 * 3] = {
      4,  4, 10,   82, 52,150,   20, 18, 48,  188,186,206,
    116,112,146,  226,224,242,  246,244,255,  132,144,222,
    214, 74,110,  238,208,120,  182,132, 58,   92,190,158,
     54, 50, 86,   96, 88,224,  120,172,232,  236,232,250
};

/* "Amber" - a warm sunset/amber-phosphor desktop: espresso backdrop, burnt
   amber title bars, tan chrome, ember accents.  Reads like a CRT at dusk. */
static const u8 far pal_amber[16 * 3] = {
     26, 12,  2,  158, 74,  6,   44, 24,  8,  226,184,126,
    172,122, 64,  250,220,158,  255,240,196,  236,168, 72,
    214, 78, 32,  255,206, 96,  198,128, 24,  176,158, 44,
     78, 48, 18,  200,102, 24,  228,178, 92,  255,236,188
};

/* "Matrix" - a green-phosphor terminal made desktop: near-black green
   backdrop, deep-green title bars, vivid phosphor accents. */
static const u8 far pal_matrix[16 * 3] = {
      0,  8,  2,    0,116, 30,    2, 22,  8,  116,206,140,
     44,120, 64,  182,238,192,  208,255,206,   64,214,120,
    220, 92, 60,  198,240,120,  120,176, 40,   46,232, 86,
     18, 58, 26,   40,196,110,  122,236,168,  200,255,198
};

static const u8 far pal_redmond[16 * 3] = {
    /* The pure-VGA Windows scheme: primaries straight off the 16-colour
       card - no tinting anywhere.  Louder than classic, exactly period. */
      0,  0,  0,    0,  0,128,    0,128,128,  192,192,192,
    128,128,128,  255,255,255,  255,255,255,    0,  0,255,
    255,  0,  0,  255,255,  0,  128,128,  0,    0,168,  0,
     64, 64, 64,    0,  0,255,    0,255,255,  255,255,224
};

/* "Sunset" - a dusk scheme: a dusky-plum desktop, burnt-coral title bars,
   warm pink-gray chrome and violet accents.  A cosy evening look. */
static const u8 far pal_sunset[16 * 3] = {
      0,  0,  0,  196, 88, 52,   74, 44, 78,  214,196,196,
    150,130,132,  250,244,244,  255,252,250,  180,120,140,
    200, 60, 60,  236,196, 96,  180,128, 40,   90,160, 96,
     84, 64, 72,  120, 90,180,  150,150,190,  252,240,236
};

/* "Forest" - a woodland scheme: a deep-moss desktop, forest-green title
   bars, sage-gray chrome and leaf-green accents.  Calm and natural. */
static const u8 far pal_forest[16 * 3] = {
      0,  0,  0,   44, 96, 48,   40, 64, 36,  196,200,180,
    128,136,116,  240,244,230,  250,252,242,  130,170,120,
    190, 70, 50,  226,196, 90,  168,128, 40,   70,170, 80,
     66, 74, 58,   70,120, 90,  120,180,150,  248,246,224
};

/* "Hot Dog" - the garish Windows-3.1 "Hot Dog Stand" homage: a bright-red
   desktop and title bars over a bright-yellow face.  Loud, and beloved. */
static const u8 far pal_hotdog[16 * 3] = {
      0,  0,  0,  220, 30, 30,  220, 40, 30,  248,224, 60,
    180,150, 20,  255,250,180,  255,248,200,  240,140, 40,
    210,  0,  0,  255,220,  0,  190,140,  0,   40,150, 40,
    120, 60, 20,   40, 60,200,   40,180,180,  255,248,200
};

/* "Slate" - a cool, modern blue-gray: a slate desktop, steel-blue title
   bars and cool light-gray chrome.  Calm and contemporary. */
static const u8 far pal_slate[16 * 3] = {
      0,  0,  0,   56, 84,120,   60, 72, 88,  192,198,206,
    120,130,142,  238,242,248,  250,252,255,  120,150,190,
    196, 72, 72,  232,206,110,  168,128, 40,   70,165,120,
     66, 74, 86,   60, 96,180,   90,175,195,  244,248,252
};

/* "Sakura" - a soft cherry-blossom scheme: a dusty-rose desktop, deep-pink
   title bars and pale pink-gray chrome.  Gentle and warm. */
static const u8 far pal_sakura[16 * 3] = {
      0,  0,  0,  190, 90,120,  200,150,165,  226,210,216,
    158,138,146,  250,242,246,  255,250,252,  200,150,175,
    206, 74, 96,  236,200,120,  180,130, 70,  120,175,120,
     96, 80, 88,  150,110,180,  170,170,200,  253,244,248
};

/* "DOS" - a nod to the blue of the DOS editors: a bright-blue desktop,
   navy title bars and silver chrome.  Pure early-90s nostalgia. */
static const u8 far pal_dos[16 * 3] = {
      0,  0,  0,    0,  0,150,    0,  0,168,  192,192,192,
    128,128,128,  250,250,250,  255,255,255,   90,110,200,
    200, 40, 40,  232,200, 40,  160,120,  0,    0,150,  0,
     64, 64, 80,    0,  0,220,    0,180,180,  255,255,224
};

/* Per-slot RGB overrides from the INI ([colors]); applied by load_palette. */
static u8 g_ov_set[16];
static u8 g_ov_rgb[16 * 3];

/* ----------------------------------------------------------------------
 * DAC shadow + fades.  g_dac mirrors all 256 hardware DAC entries (6-bit
 * values, as the DAC wants them).  Every palette write goes through
 * dac_store()+dac_flush(); while g_dark is set the flush is suppressed, so
 * whole scenes can be composed invisibly and revealed with a fade.  The
 * shadow lives in FAR memory - DGROUP is nearly full - and the DAC is
 * programmed straight through ports 3C8h/3C9h, which needs no BIOS
 * transfer buffer at all (this also freed ~1 KB of near data that the old
 * INT 10h path kept in DGROUP).
 * -------------------------------------------------------------------- */
static u8 far g_dac[256 * 3];
static bool_t g_dark  = FALSE;
static bool_t g_fades = TRUE;

#define FADE_STEPS 5

void video_enable_fades(bool_t on) { g_fades = on; }
bool_t video_is_dark(void)         { return g_dark; }

/* Program hardware DAC entries [start, start+count) from the shadow. */
static void dac_flush(int start, int count)
{
    int i, n = count * 3;
    const u8 far *p = g_dac + start * 3;
    if (g_dark)
        return;
    outp(0x3C8, (u8)start);
    for (i = 0; i < n; ++i)
        outp(0x3C9, p[i]);
}

/* Store 0..255-scaled RGB triples into the shadow (scaled to 6-bit). */
static void dac_store(int start, int count, const u8 far *rgb255)
{
    int i, n = count * 3;
    u8 far *p = g_dac + start * 3;
    for (i = 0; i < n; ++i)
        p[i] = (u8)(rgb255[i] >> 2);
}

/* Wait for the start of the next vertical blank (Input Status 1, 3DAh). */
void vid_vsync(void)
{
    while (inp(0x3DA) & 0x08)
        ;
    while (!(inp(0x3DA) & 0x08))
        ;
}

/* Write the whole hardware DAC as shadow * num / den (fade step). */
static void dac_hw_scaled(int num, int den)
{
    int i;
    outp(0x3C8, 0);
    for (i = 0; i < 256 * 3; ++i)
        outp(0x3C9, (u8)(g_dac[i] * num / den));
}

void video_blackout(void)
{
    int i;
    if (!g_fades)
        return;
    g_dark = TRUE;
    outp(0x3C8, 0);
    for (i = 0; i < 256 * 3; ++i)
        outp(0x3C9, 0);
}

void video_fade_out(void)
{
    int s;
    if (!g_fades || g_dark)
        return;
    for (s = FADE_STEPS - 1; s >= 0; --s) {
        vid_vsync();
        dac_hw_scaled(s, FADE_STEPS);
    }
    g_dark = TRUE;
}

void video_fade_in(void)
{
    int s;
    if (!g_dark)
        return;
    g_dark = FALSE;
    if (!g_fades) {                    /* fades off: snap straight back    */
        dac_flush(0, 256);
        return;
    }
    for (s = 1; s <= FADE_STEPS; ++s) {
        vid_vsync();
        dac_hw_scaled(s, FADE_STEPS);
    }
}

void video_set_overrides(const u8 *set, const u8 *rgb)
{
    int i;
    for (i = 0; i < 16; ++i) {
        g_ov_set[i] = set[i];
        g_ov_rgb[i * 3 + 0] = rgb[i * 3 + 0];
        g_ov_rgb[i * 3 + 1] = rgb[i * 3 + 1];
        g_ov_rgb[i * 3 + 2] = rgb[i * 3 + 2];
    }
}

/* ----------------------------------------------------------------------
 * Low-level helpers.
 * -------------------------------------------------------------------- */
static void bios_set_mode(unsigned mode)
{
    union REGS r;
    r.x.ax = mode;
    int86(0x10, &r, &r);
}

/* ----------------------------------------------------------------------
 * Active title-bar gradient.  Mode 13h has a free 256-entry DAC, so we
 * reserve a 32-step ramp (well clear of slot 0..15 themes and the boot
 * splash's 16..191) and sweep it dark->light across the bar, the classic
 * raised "active window" look.  Mode 12h has only 16 reachable colours, so
 * there the bar stays solid (vid_title_bar handles that).
 * -------------------------------------------------------------------- */
#define TGRAD_BASE 224
#define TGRAD_N    32

static void load_title_ramp(const u8 far *rgb255)
{
    u8 far *dac = g_dac + TGRAD_BASE * 3;
    int i, tr, tg, tb, dr, dg, db, lr, lg, lb;

    tr = rgb255[3]; tg = rgb255[4]; tb = rgb255[5];   /* slot 1 = C_TITLE   */
    if (g_ov_set[1]) { tr = g_ov_rgb[3]; tg = g_ov_rgb[4]; tb = g_ov_rgb[5]; }

    /* The iconic Windows-95/98 active-title sweep: LEFT is the solid title
       colour (navy), RIGHT lifts toward a bright blue - green and (mostly)
       blue rise while red stays low, so a navy bar blooms into sky blue.
       (A warm title stays warm-ish; the bias just cools the blues.) */
    dr = tr;  dg = tg;  db = tb;
    lr = tr + (255 - tr) * 28 / 100;
    lg = tg + (255 - tg) * 50 / 100;
    lb = tb + (255 - tb) * 80 / 100;

    for (i = 0; i < TGRAD_N; ++i) {
        int f = i * 255 / (TGRAD_N - 1);              /* 0..255 left->right */
        /* (long): (light-dark)*f overflows a 16-bit int. */
        dac[i * 3 + 0] = (u8)((dr + (int)((long)(lr - dr) * f / 255)) >> 2);
        dac[i * 3 + 1] = (u8)((dg + (int)((long)(lg - dg) * f / 255)) >> 2);
        dac[i * 3 + 2] = (u8)((db + (int)((long)(lb - db) * f / 255)) >> 2);
    }
    dac_flush(TGRAD_BASE, TGRAD_N);
}

/* ----------------------------------------------------------------------
 * Optional desktop gradient.  Another reserved Mode 13h ramp (slots
 * 192..223), a subtle vertical sweep of the theme's desktop colour from a
 * little lighter at the top to a little darker at the bottom, so the
 * pattern=gradient desktop has depth without leaving the palette.
 * -------------------------------------------------------------------- */
#define DGRAD_BASE 192
#define DGRAD_N    32

static void load_desktop_ramp(const u8 far *rgb255)
{
    u8 far *dac = g_dac + DGRAD_BASE * 3;
    int i, dr, dg, db;

    dr = rgb255[C_DESKTOP * 3 + 0];
    dg = rgb255[C_DESKTOP * 3 + 1];
    db = rgb255[C_DESKTOP * 3 + 2];
    if (g_ov_set[C_DESKTOP]) {
        dr = g_ov_rgb[C_DESKTOP * 3 + 0];
        dg = g_ov_rgb[C_DESKTOP * 3 + 1];
        db = g_ov_rgb[C_DESKTOP * 3 + 2];
    }
    for (i = 0; i < DGRAD_N; ++i) {
        /* A smoothstep sweep (soft at both ends) from a bright glow at the
           top to a deep base at the foot - more depth than the old linear
           fade, so the default desktop reads like lit sky over dark ground.
           t and sm are 0..256 fixed point; mul rides 140% down to 64%. */
        long t   = (long)i * 256 / (DGRAD_N - 1);
        long sm  = t * t * (768 - 2 * t) / 65536;      /* smoothstep 0..256 */
        /* Half smoothstep, half linear.  Pure smoothstep has zero slope
           at both ends, and after the shift to a 6-bit DAC that collapsed
           the top quarter of the screen into a single flat slab - the
           ramp was spending its resolution where the eye needs it least.
           Blending keeps the lit-sky-over-dark-ground character while
           giving the ends somewhere to go. */
        long sm2 = (sm + t) / 2;
        int  mul = 140 - (int)(76 * sm2 / 256);        /* 140% .. 64%       */
        /* long: mul peaks at 140, so any component from 235 up overflows
           a 16-bit int (235*140 = 32900) and the clamps below - which
           only bounded the HIGH side - let a wrapped negative through to
           the (u8) cast.  Reachable from [colors] desktop=250,250,250.
           load_title_ramp already widens for exactly this reason. */
        int  rr  = (int)((long)dr * mul / 100);
        int  gg  = (int)((long)dg * mul / 100);
        int  bb  = (int)((long)db * mul / 100);
        if (rr > 255) rr = 255;
        if (gg > 255) gg = 255;
        if (bb > 255) bb = 255;
        if (rr < 0) rr = 0;
        if (gg < 0) gg = 0;
        if (bb < 0) bb = 0;
        dac[i * 3 + 0] = (u8)(rr >> 2);
        dac[i * 3 + 1] = (u8)(gg >> 2);
        dac[i * 3 + 2] = (u8)(bb >> 2);
    }
    dac_flush(DGRAD_BASE, DGRAD_N);
}

/* Load DAC registers 0..15 from an RGB-0..255 table. */
static void load_palette(const u8 far *rgb255)
{
    int i;
    dac_store(0, 16, rgb255);
    /* Apply any per-slot overrides from the INI on top of the theme. */
    for (i = 0; i < 16; ++i) {
        if (g_ov_set[i]) {
            g_dac[i * 3 + 0] = (u8)(g_ov_rgb[i * 3 + 0] >> 2);
            g_dac[i * 3 + 1] = (u8)(g_ov_rgb[i * 3 + 1] >> 2);
            g_dac[i * 3 + 2] = (u8)(g_ov_rgb[i * 3 + 2] >> 2);
        }
    }
    dac_flush(0, 16);

    /* Refresh the reserved Mode 13h ramps from this theme. */
    if (g_mode == MODE_13) {
        load_title_ramp(rgb255);
        load_desktop_ramp(rgb255);
    }
}

/* In Mode 12h, make the 16 attribute palette registers map colour i to
   DAC entry i (identity), so our DAC RGB is what shows on screen. */
static void set_attr_identity(void)
{
    static const u8 tbl[17] = { 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15, 0 };
    union REGS  r;
    struct SREGS s;
    segread(&s);
    s.es   = s.ds;
    r.x.ax = 0x1002;              /* set all palette registers + overscan */
    r.x.dx = (unsigned)tbl;
    int86x(0x10, &r, &r, &s);
}

/* Map a theme name to its 16-slot RGB table (classic for NULL/unknown). */
static const u8 far *theme_palette(const char *name)
{
    const u8 far *p = pal_classic;
    if (name != NULL) {
        if      (strcmp(name, "penumbra") == 0) p = pal_penumbra;
        else if (strcmp(name, "beige")    == 0) p = pal_beige;
        else if (strcmp(name, "bureau")   == 0) p = pal_beige;
        else if (strcmp(name, "winsteel") == 0) p = pal_winsteel;
        else if (strcmp(name, "steel")    == 0) p = pal_winsteel;
        else if (strcmp(name, "moncloa")  == 0) p = pal_moncloa;
        else if (strcmp(name, "workbench")== 0) p = pal_workbench;
        else if (strcmp(name, "ocean")    == 0) p = pal_ocean;
        else if (strcmp(name, "redmond")  == 0) p = pal_redmond;
        else if (strcmp(name, "rose")     == 0) p = pal_rose;
        else if (strcmp(name, "midnight") == 0) p = pal_midnight;
        else if (strcmp(name, "amber")    == 0) p = pal_amber;
        else if (strcmp(name, "matrix")   == 0) p = pal_matrix;
        else if (strcmp(name, "sunset")   == 0) p = pal_sunset;
        else if (strcmp(name, "forest")   == 0) p = pal_forest;
        else if (strcmp(name, "hotdog")   == 0) p = pal_hotdog;
        else if (strcmp(name, "slate")    == 0) p = pal_slate;
        else if (strcmp(name, "sakura")   == 0) p = pal_sakura;
        else if (strcmp(name, "dos")      == 0) p = pal_dos;
    }
    return p;
}

void video_set_theme(const char *name)
{
    load_palette(theme_palette(name));
}

/* Read a named theme's slot RGB (0..255 each) WITHOUT applying it - the
   Settings panel uses this to preview every theme's colours at once. */
void video_theme_rgb(const char *name, int slot, u8 *r, u8 *g, u8 *b)
{
    const u8 far *p = theme_palette(name);
    if (slot < 0)  slot = 0;
    if (slot > 15) slot = 15;
    *r = p[slot * 3 + 0];
    *g = p[slot * 3 + 1];
    *b = p[slot * 3 + 2];
}

/* TRUE if the mode string asks for the 640x480 planar mode. */
static bool_t wants_mode12(const char *mode)
{
    if (mode == NULL)
        return FALSE;
    return (strstr(mode, "12") != NULL) ? TRUE : FALSE;
}

/* ----------------------------------------------------------------------
 * Lifecycle.
 * -------------------------------------------------------------------- */
bool_t video_init(const char *mode)
{
    g_vga = (u8 far *)MK_FP(0xA000, 0);

    if (wants_mode12(mode)) {
        int p;
        /* 4 planes * 2400 paragraphs (38400 bytes) = 9600 paragraphs. */
        if (_dos_allocmem(9600, &g_plane_seg) != 0) {
            g_plane_seg = 0;
            return FALSE;
        }
        for (p = 0; p < 4; ++p)
            g_plane[p] = (u8 far *)MK_FP(g_plane_seg + (unsigned)p * 2400, 0);
        g_mode = MODE_12;
        g_bios_mode = 0x12;
        castalia_screen_w = 640;
        castalia_screen_h = 480;
    } else {
        if (_dos_allocmem(4000, &g_back_seg) != 0) {
            g_back_seg = 0;
            return FALSE;
        }
        g_back = (u8 far *)MK_FP(g_back_seg, 0);
        g_mode = MODE_13;
        g_bios_mode = 0x13;
        castalia_screen_w = 320;
        castalia_screen_h = 200;
    }

    /* The scene cache: same size as the back buffer.  Optional. */
    if (_dos_allocmem((g_mode == MODE_12) ? 9600 : 4000, &g_cache_seg) != 0)
        g_cache_seg = 0;

    bios_set_mode(g_bios_mode);
    if (g_mode == MODE_12)
        set_attr_identity();
    vid_clear_clip();              /* bound the clip to the chosen mode  */
    video_set_theme("classic");
    /* No present here: the mode set leaves the screen black, and the boot
       splash composes and presents the first visible frame (possibly under
       a blackout, so the splash can fade in from black). */
    vid_clear(C_DESKTOP);
    return TRUE;
}

bool_t video_is_big(void)
{
    return (g_mode == MODE_12) ? TRUE : FALSE;
}

void video_set_dac(int start, int count, const u8 *rgb255)
{
    if (count <= 0 || start < 0 || start > 255)
        return;
    if (start + count > 256)
        count = 256 - start;
    dac_store(start, count, rgb255);
    dac_flush(start, count);
}

void video_slot_rgb(int slot, u8 *r, u8 *g, u8 *b)
{
    const u8 far *p;
    if (slot < 0) slot = 0;
    if (slot > 255) slot = 255;
    p = g_dac + slot * 3;              /* 6-bit DAC values -> 0..255        */
    *r = (u8)(p[0] * 255 / 63);
    *g = (u8)(p[1] * 255 / 63);
    *b = (u8)(p[2] * 255 / 63);
}

void video_text_mode(void)
{
    bios_set_mode(0x03);
}

void video_graphics_mode(void)
{
    bios_set_mode(g_bios_mode);
    if (g_mode == MODE_12)
        set_attr_identity();
    video_set_theme("classic");
}

void video_shutdown(void)
{
    bios_set_mode(0x03);
    if (g_back_seg != 0)  { _dos_freemem(g_back_seg);  g_back_seg = 0;  }
    if (g_plane_seg != 0) { _dos_freemem(g_plane_seg); g_plane_seg = 0; }
    if (g_cache_seg != 0) { _dos_freemem(g_cache_seg); g_cache_seg = 0; }
    g_back = (u8 far *)0;
}

/* ======================================================================
 * Mode 13h (linear) primitives.
 * ==================================================================== */

/* Hand-tuned far copy: a 386 "rep movsd" moves dwords, HALF the iterations
   of the 8086-built _fmemcpy (rep movsw).  This one helper feeds both
   presents (the 64000-byte Mode 13h frame and the four 38400-byte Mode 12h
   planes) and the full-width blits - the most frequent large copies in the
   whole shell.  Safe on any 386+, which is the whole target. */
extern void copy32(unsigned dseg, unsigned doff,
                   unsigned sseg, unsigned soff, unsigned ndwords);
#pragma aux copy32 =              \
    ".386"                        \
    "push ds"                     \
    "mov   es, ax"                \
    "mov   ds, dx"                \
    "cld"                         \
    "rep   movsd"                 \
    "pop   ds"                    \
    parm [ax] [di] [dx] [si] [cx] \
    modify [cx si di es];

/* The write-side twin of copy32: "rep stosd" stores four bytes per cycle
   where the 8086-built _fmemset stores two.  Solid fills are the shell's
   second-biggest memory traffic after the present - the desktop, every
   window face, the taskbar, every dialog and every applet background - so
   the same argument that justified copy32 applies here.
   The 16-bit compiler will not name eax in a parm list, so the caller hands
   over the byte doubled into a word and the helper widens it to the full
   dword itself. */
extern void fill32(unsigned dseg, unsigned doff, unsigned ndwords,
                   unsigned vword);
#pragma aux fill32 =              \
    ".386"                        \
    "mov   es, dx"                \
    "movzx eax, ax"               \
    "mov   dx, ax"                \
    "shl   eax, 16"               \
    "mov   ax, dx"                \
    "cld"                         \
    "rep   stosd"                 \
    parm [dx] [di] [cx] [ax]      \
    modify [ax cx dx di es];

/* _fmemset with a dword core.  Aligns to a 4-byte boundary first (an
   unaligned stosd costs the 386 an extra bus cycle per store), then the
   0-3 byte tail.  Short runs go straight to _fmemset - the setup would
   cost more than it saves. */
static void fmemset32(u8 far *d, u8 c, unsigned n)
{
    unsigned head, nd, tail;
    if (n < 16) { _fmemset(d, c, n); return; }
    head = (4U - (FP_OFF(d) & 3U)) & 3U;
    if (head) { _fmemset(d, c, head); d += head; n -= head; }
    nd   = n >> 2;
    tail = n & 3U;
    fill32(FP_SEG(d), FP_OFF(d), nd,
           (unsigned)c | ((unsigned)c << 8));
    if (tail) _fmemset(d + (nd << 2), c, tail);
}

static void l_clear(u8 c)            { fmemset32(g_back, c, FRAME_BYTES); }
static void l_pixel(int x, int y, u8 c) { g_back[(unsigned)y * 320 + x] = c; }

static void l_hline(int x, int y, int w, u8 c)
{
    fmemset32(g_back + (unsigned)y * 320 + x, c, (unsigned)w);
}
static void l_vline(int x, int y, int h, u8 c)
{
    unsigned o = (unsigned)y * 320 + x;
    while (h-- > 0) { g_back[o] = c; o += 320; }
}

/* Pre-clipped solid fill: one offset computation, one _fmemset per row
   (and a single _fmemset for full-width fills - the desktop, the taskbar). */
static void l_fillrect(int x, int y, int w, int h, u8 c)
{
    unsigned o = (unsigned)y * 320 + x;
    if (x == 0 && w == 320) {
        fmemset32(g_back + o, c, (unsigned)h * 320U);
        return;
    }
    while (h-- > 0) { fmemset32(g_back + o, c, (unsigned)w); o += 320; }
}

/* Pre-clipped 50% checkerboard fill (dithered drop shadows). */
static void l_dither(int x, int y, int w, int h, u8 c)
{
    int row;
    for (row = 0; row < h; ++row) {
        int ph = (x + y + row) & 1;            /* first lit pixel's parity  */
        u8 far *d = g_back + (unsigned)(y + row) * 320 + (unsigned)(x + ph);
        int n = (w - ph + 1) >> 1;
        while (n-- > 0) { *d = c; d += 2; }
    }
}

/* Pre-clipped 8-pixel bitmask row (the text fast path): bit 7 = (x,y). */
static void l_bits8(int x, int y, u8 bits, u8 c)
{
    u8 far *d = g_back + (unsigned)y * 320 + (unsigned)x;
    if (bits & 0x80) d[0] = c;
    if (bits & 0x40) d[1] = c;
    if (bits & 0x20) d[2] = c;
    if (bits & 0x10) d[3] = c;
    if (bits & 0x08) d[4] = c;
    if (bits & 0x04) d[5] = c;
    if (bits & 0x02) d[6] = c;
    if (bits & 0x01) d[7] = c;
}

static void l_present(void) { copy32(0xA000, 0, g_back_seg, 0, 16000); }
static void l_blit(int x, int y, int w, int h)
{
    unsigned o = (unsigned)y * 320 + x;
    unsigned nd, tail;
    if (x == 0 && w == 320) {                  /* contiguous run: one movsd */
        copy32(0xA000, o, g_back_seg, o, (unsigned)((unsigned)h * 320U >> 2));
        return;
    }
    /* Partial-width rows are the FAST PATH: every animating window, every
       menu drop, every mouse-trail repair blits one.  Move the bulk of each
       row as dwords (rep movsd) like the full-width case instead of the
       word-at-a-time _fmemcpy - half the loop iterations - and mop up the
       last 0..3 bytes.  Unaligned dword moves are legal on 386 and still
       beat two word moves. */
    nd   = (unsigned)w >> 2;
    tail = (unsigned)w & 3U;
    while (h-- > 0) {
        if (nd)
            copy32(0xA000, o, g_back_seg, o, nd);
        if (tail)
            _fmemcpy(g_vga + o + (nd << 2), g_back + o + (nd << 2), tail);
        o += 320;
    }
}
static void l_vga_pixel(int x, int y, u8 c) { g_vga[(unsigned)y * 320 + x] = c; }

/* ======================================================================
 * Mode 12h (planar) primitives.
 * ==================================================================== */
static void map_mask(u8 m) { outp(0x3C4, 0x02); outp(0x3C5, m); }

static void p_clear(u8 c)
{
    int p;
    for (p = 0; p < 4; ++p)
        fmemset32(g_plane[p], ((c >> p) & 1) ? 0xFF : 0x00, PLANE_BYTES);
}

static void p_pixel(int x, int y, u8 c)
{
    unsigned idx = (unsigned)y * PLANE_STRIDE + (x >> 3);
    u8 m = (u8)(0x80 >> (x & 7));
    int p;
    for (p = 0; p < 4; ++p) {
        if ((c >> p) & 1) g_plane[p][idx] |= m;
        else              g_plane[p][idx] &= (u8)~m;
    }
}

/* Pre-clipped planar fill.  The edge masks are computed ONCE for the whole
   rectangle (the old path re-derived per-pixel masks for the partial bytes
   of every row of every plane); the middle bytes are straight _fmemsets. */
static void p_fillrect(int x, int y, int w, int h, u8 c)
{
    int x1 = x + w - 1;
    unsigned bx0 = (unsigned)(x >> 3), bx1 = (unsigned)(x1 >> 3);
    u8 mfirst = (u8)(0xFF >> (x & 7));
    u8 mlast  = (u8)(0xFF << (7 - (x1 & 7)));
    unsigned rb = (unsigned)y * PLANE_STRIDE;
    int p, row;

    if (bx0 == bx1) {                          /* all inside one byte      */
        u8 m = (u8)(mfirst & mlast), nm = (u8)~m;
        for (p = 0; p < 4; ++p) {
            u8 far *pl = g_plane[p];
            unsigned i = rb + bx0;
            if ((c >> p) & 1)
                for (row = 0; row < h; ++row, i += PLANE_STRIDE) pl[i] |= m;
            else
                for (row = 0; row < h; ++row, i += PLANE_STRIDE) pl[i] &= nm;
        }
        return;
    }
    for (p = 0; p < 4; ++p) {
        u8 far *pl = g_plane[p];
        u8 bit  = (u8)((c >> p) & 1);
        u8 fill = bit ? 0xFF : 0x00;
        u8 nmf  = (u8)~mfirst, nml = (u8)~mlast;
        unsigned span = bx1 - bx0, i = rb + bx0;
        unsigned nmid = span - 1;
        for (row = 0; row < h; ++row, i += PLANE_STRIDE) {
            if (bit) { pl[i] |= mfirst; pl[i + span] |= mlast; }
            else     { pl[i] &= nmf;    pl[i + span] &= nml;   }
            if (nmid > 0)
                fmemset32(pl + i + 1, fill, nmid);
        }
    }
}

static void p_hline(int x, int y, int w, u8 c)
{
    p_fillrect(x, y, w, 1, c);
}

/* Pre-clipped 50% checkerboard fill.  A whole byte of the pattern is 0xAA
   on even rows and 0x55 on odd rows (pixel px is lit when (px+y) is even),
   so each row is a handful of masked byte ops per plane instead of the old
   one-call-per-pixel path. */
/* Lay colour `c` over a span at the 8-pixel density pattern `pat`.
   p_dither was this with 0xAA/0x55 hardcoded - a fixed 50% checker, which
   is why the Mode 12h gradient attempts failed: one density gives ONE
   intermediate shade, so teal-then-half-black reads as two flat bands
   rather than a ramp.  With four densities it is a ramp. */
static void p_pattern(int x, int y, int w, int h, u8 c, const u8 *pat4)
{
    int x1 = x + w - 1;
    unsigned bx0 = (unsigned)(x >> 3), bx1 = (unsigned)(x1 >> 3);
    u8 mfirst = (u8)(0xFF >> (x & 7));
    u8 mlast  = (u8)(0xFF << (7 - (x1 & 7)));
    int row, p;

    if (bx0 == bx1)
        mfirst = (u8)(mfirst & mlast);

    for (row = 0; row < h; ++row) {
        int yy = y + row;
        u8 pat = pat4[yy & 3];
        unsigned rb = (unsigned)yy * PLANE_STRIDE;
        u8 mf = (u8)(mfirst & pat), ml = (u8)(mlast & pat);
        if (pat == 0x00)
            continue;                  /* nothing to lay down on this row  */
        for (p = 0; p < 4; ++p) {
            u8 far *pl = g_plane[p] + rb;
            unsigned b;
            if ((c >> p) & 1) {
                pl[bx0] |= mf;
                if (bx1 > bx0) {
                    for (b = bx0 + 1; b < bx1; ++b) pl[b] |= pat;
                    pl[bx1] |= ml;
                }
            } else {
                u8 np = (u8)~pat;
                pl[bx0] &= (u8)~mf;
                if (bx1 > bx0) {
                    for (b = bx0 + 1; b < bx1; ++b) pl[b] &= np;
                    pl[bx1] &= (u8)~ml;
                }
            }
        }
    }
}

/* The 50% checker the shadow and the scrollbar trough want. */
static void p_dither(int x, int y, int w, int h, u8 c)
{
    static const u8 HALF[4] = { 0xAA, 0x55, 0xAA, 0x55 };
    p_pattern(x, y, w, h, c, HALF);
}

/* Pre-clipped 8-pixel bitmask row: the mask spans at most two plane bytes,
   so a glyph row costs at most 8 masked byte ops instead of 8 pixel calls
   through the 4-plane read-modify-write path. */
static void p_bits8(int x, int y, u8 bits, u8 c)
{
    unsigned idx = (unsigned)y * PLANE_STRIDE + (unsigned)(x >> 3);
    u16 m  = (u16)((u16)bits << (8 - (x & 7)));
    u8  mL = (u8)(m >> 8), mR = (u8)m;
    int p;
    for (p = 0; p < 4; ++p) {
        u8 far *pl = g_plane[p];
        if ((c >> p) & 1) {
            if (mL) pl[idx]     |= mL;
            if (mR) pl[idx + 1] |= mR;
        } else {
            if (mL) pl[idx]     &= (u8)~mL;
            if (mR) pl[idx + 1] &= (u8)~mR;
        }
    }
}

static void p_vline(int x, int y, int h, u8 c)
{
    unsigned idx = (unsigned)y * PLANE_STRIDE + (x >> 3);
    u8 m = (u8)(0x80 >> (x & 7));
    int p, k;
    for (p = 0; p < 4; ++p) {
        u8 bit = (u8)((c >> p) & 1);
        unsigned i = idx;
        for (k = 0; k < h; ++k) {
            if (bit) g_plane[p][i] |= m; else g_plane[p][i] &= (u8)~m;
            i += PLANE_STRIDE;
        }
    }
}

static void p_present(void)
{
    int p;
    for (p = 0; p < 4; ++p) {
        map_mask((u8)(1 << p));
        copy32(0xA000, 0, FP_SEG(g_plane[p]), FP_OFF(g_plane[p]),
               PLANE_BYTES / 4);
    }
    map_mask(0x0F);
}

static void p_blit(int x, int y, int w, int h)
{
    int bx0 = x >> 3;
    int bx1 = (x + w - 1) >> 3;
    int bw  = bx1 - bx0 + 1;
    int p, row;
    for (p = 0; p < 4; ++p) {
        map_mask((u8)(1 << p));
        for (row = 0; row < h; ++row) {
            unsigned off = (unsigned)(y + row) * PLANE_STRIDE + bx0;
            _fmemcpy(g_vga + off, g_plane[p] + off, (unsigned)bw);
        }
    }
    map_mask(0x0F);
}

/* Direct planar pixel for the cursor, via Set/Reset + Bit Mask, restoring
   the default write path afterwards so present()/blit stay correct. */
static void p_vga_pixel(int x, int y, u8 c)
{
    unsigned idx = (unsigned)y * PLANE_STRIDE + (x >> 3);
    u8 m = (u8)(0x80 >> (x & 7));
    volatile u8 d;
    map_mask(0x0F);
    outp(0x3CE, 0x08); outp(0x3CF, m);        /* bit mask = this pixel    */
    outp(0x3CE, 0x00); outp(0x3CF, c);        /* set/reset = colour       */
    outp(0x3CE, 0x01); outp(0x3CF, 0x0F);     /* enable set/reset (all)   */
    d = g_vga[idx];                           /* latch load               */
    g_vga[idx] = 0xFF;                         /* write (masked by above)  */
    outp(0x3CE, 0x01); outp(0x3CF, 0x00);     /* restore: no set/reset    */
    outp(0x3CE, 0x08); outp(0x3CF, 0xFF);     /* restore: full bit mask   */
    (void)d;
}

/* ======================================================================
 * Public primitives - clip, then dispatch to the active back-end.
 * ==================================================================== */
/* ---- clip rectangle --------------------------------------------------
 * Every public primitive below clips to this box instead of to the raw
 * screen.  Two things fall out of it:
 *
 *   - draw_window() can fence an applet inside its own client area, so a
 *     miscomputed width truncates instead of painting over the frame, the
 *     drop shadow and whatever is behind them.  Applets no longer each
 *     hand-roll their own truncation.
 *
 *   - a partial present can recompose ONLY the region it is about to blit.
 *     Restoring the cached scene used to be a flat 64000-byte copy and the
 *     whole window stack was re-composed on top of it - for a 40x12 clock.
 *
 * Default is the full screen, so anything that never sets it is unchanged.
 * ------------------------------------------------------------------- */
static int g_clx0 = 0, g_cly0 = 0;
/* SCREEN_H_MAX, not SCREEN_W_MAX: seeding the bottom bound with the WIDTH
   left every primitive clipping to 640x640 until the first vid_set_clip or
   vid_clear_clip, so during boot and the splash a y >= 200 write in Mode
   13h wrapped inside the back-buffer segment, and in Mode 12h could run
   past the four-plane allocation.  video_init() now also calls
   vid_clear_clip() so the default tracks the mode actually selected. */
static int g_clx1 = SCREEN_W_MAX, g_cly1 = SCREEN_H_MAX;

void vid_clear_clip(void)
{
    g_clx0 = 0; g_cly0 = 0;
    g_clx1 = SCREEN_W; g_cly1 = SCREEN_H;
}

void vid_set_clip(int x, int y, int w, int h)
{
    vid_clear_clip();
    if (x > g_clx0) g_clx0 = x;
    if (y > g_cly0) g_cly0 = y;
    if (x + w < g_clx1) g_clx1 = x + w;
    if (y + h < g_cly1) g_cly1 = y + h;
    if (g_clx1 < g_clx0) g_clx1 = g_clx0;
    if (g_cly1 < g_cly0) g_cly1 = g_cly0;
}

/* Set the clip to the intersection of two rectangles. */
void vid_set_clip_isect(const Rect *a, const Rect *b)
{
    int x0 = (a->x > b->x) ? a->x : b->x;
    int y0 = (a->y > b->y) ? a->y : b->y;
    int x1 = (a->x + a->w < b->x + b->w) ? a->x + a->w : b->x + b->w;
    int y1 = (a->y + a->h < b->y + b->h) ? a->y + a->h : b->y + b->h;
    vid_set_clip(x0, y0, (x1 > x0) ? x1 - x0 : 0, (y1 > y0) ? y1 - y0 : 0);
}

void vid_get_clip(Rect *r)
{
    rect_set(r, g_clx0, g_cly0, g_clx1 - g_clx0, g_cly1 - g_cly0);
}

bool_t vid_clip_hits(int x, int y, int w, int h)
{
    return (x < g_clx1 && x + w > g_clx0 &&
            y < g_cly1 && y + h > g_cly0) ? TRUE : FALSE;
}

void vid_clear(u8 color)
{
    if (g_mode == MODE_13) l_clear(color);
    else                   p_clear(color);
}

void vid_pixel(int x, int y, u8 color)
{
    if (x < g_clx0 || x >= g_clx1 || y < g_cly0 || y >= g_cly1)
        return;
    if (g_mode == MODE_13) l_pixel(x, y, color);
    else                   p_pixel(x, y, color);
}

void vid_hline(int x, int y, int w, u8 color)
{
    if (y < g_cly0 || y >= g_cly1 || w <= 0)
        return;
    if (x < g_clx0) { w -= g_clx0 - x; x = g_clx0; }
    if (x + w > g_clx1) w = g_clx1 - x;
    if (w <= 0)
        return;
    if (g_mode == MODE_13) l_hline(x, y, w, color);
    else                   p_hline(x, y, w, color);
}

void vid_vline(int x, int y, int h, u8 color)
{
    if (x < g_clx0 || x >= g_clx1 || h <= 0)
        return;
    if (y < g_cly0) { h -= g_cly0 - y; y = g_cly0; }
    if (y + h > g_cly1) h = g_cly1 - y;
    if (h <= 0)
        return;
    if (g_mode == MODE_13) l_vline(x, y, h, color);
    else                   p_vline(x, y, h, color);
}

void vid_fillrect(int x, int y, int w, int h, u8 color)
{
    if (w <= 0 || h <= 0)
        return;
    if (x < g_clx0) { w -= g_clx0 - x; x = g_clx0; }
    if (y < g_cly0) { h -= g_cly0 - y; y = g_cly0; }
    if (x + w > g_clx1) w = g_clx1 - x;
    if (y + h > g_cly1) h = g_cly1 - y;
    if (w <= 0 || h <= 0)
        return;
    if (g_mode == MODE_13) l_fillrect(x, y, w, h, color);
    else                   p_fillrect(x, y, w, h, color);
}

void vid_dither_rect(int x, int y, int w, int h, u8 color)
{
    if (w <= 0 || h <= 0)
        return;
    if (x < g_clx0) { w -= g_clx0 - x; x = g_clx0; }
    if (y < g_cly0) { h -= g_cly0 - y; y = g_cly0; }
    if (x + w > g_clx1) w = g_clx1 - x;
    if (y + h > g_cly1) h = g_cly1 - y;
    if (w <= 0 || h <= 0)
        return;
    if (g_mode == MODE_13) l_dither(x, y, w, h, color);
    else                   p_dither(x, y, w, h, color);
}

void vid_bits8(int x, int y, u8 bits, u8 color)
{
    if (bits == 0 || y < g_cly0 || y >= g_cly1)
        return;
    if (x < g_clx0 || x + 8 > g_clx1) {        /* clipped: per-pixel path  */
        int i;
        for (i = 0; i < 8; ++i)
            if (bits & (u8)(0x80 >> i))
                vid_pixel(x + i, y, color);
        return;
    }
    if (g_mode == MODE_13) l_bits8(x, y, bits, color);
    else                   p_bits8(x, y, bits, color);
}

/* Draw a whole glyph: `n` scan lines of 8-pixel bitmasks starting at
   (x,y).  font.c used to call vid_bits8 once PER ROW - up to 8 (16 in
   Mode 12h) far calls per character, each re-running the clip tests and
   the g_mode dispatch for the same glyph.  Clip once, dispatch once, and
   walk the rows internally; text is on every title, list row, menu item
   and label, so this is the hottest path in the shell. */
void vid_glyph(int x, int y, const u8 far *rows, int n, u8 color)
{
    int r;
    if (n <= 0 || y >= g_cly1 || x >= g_clx1 || x + 8 <= g_clx0)
        return;
    if (x < g_clx0 || x + 8 > g_clx1) {    /* straddles an edge: slow path */
        for (r = 0; r < n; ++r)
            if (rows[r] != 0)
                vid_bits8(x, y + r, rows[r], color);
        return;
    }
    if (y < g_cly0) { rows += g_cly0 - y; n -= g_cly0 - y; y = g_cly0; }
    if (y + n > g_cly1) n = g_cly1 - y;
    if (n <= 0)
        return;
    if (g_mode == MODE_13) {
        u8 far *d = g_back + (unsigned)y * 320 + (unsigned)x;
        for (r = 0; r < n; ++r) {
            u8 bits = rows[r];
            if (bits) {
                if (bits & 0x80) d[0] = color;
                if (bits & 0x40) d[1] = color;
                if (bits & 0x20) d[2] = color;
                if (bits & 0x10) d[3] = color;
                if (bits & 0x08) d[4] = color;
                if (bits & 0x04) d[5] = color;
                if (bits & 0x02) d[6] = color;
                if (bits & 0x01) d[7] = color;
            }
            d += 320;
        }
    } else {
        for (r = 0; r < n; ++r)
            if (rows[r] != 0)
                p_bits8(x, y + r, rows[r], color);
    }
}

void vid_copy_row(int x, int y, const u8 far *src, int w)
{
    if (y < g_cly0 || y >= g_cly1 || w <= 0)
        return;
    if (x < g_clx0) { src += g_clx0 - x; w -= g_clx0 - x; x = g_clx0; }
    if (x + w > g_clx1) w = g_clx1 - x;
    if (w <= 0)
        return;
    if (g_mode == MODE_13) {
        _fmemcpy(g_back + (unsigned)y * 320 + (unsigned)x, src, (unsigned)w);
        return;
    }
    /* Mode 12h: leading/trailing pixels one by one, whole 8-pixel groups
       as a chunky-to-planar conversion writing one byte per plane. */
    while (w > 0 && (x & 7)) { p_pixel(x, y, (u8)(*src & 15)); ++x; ++src; --w; }
    while (w >= 8) {
        unsigned idx = (unsigned)y * PLANE_STRIDE + (unsigned)(x >> 3);
        u8 b0 = 0, b1 = 0, b2 = 0, b3 = 0;
        int i;
        for (i = 0; i < 8; ++i) {
            u8 v = src[i], m = (u8)(0x80 >> i);
            if (v & 1) b0 |= m;
            if (v & 2) b1 |= m;
            if (v & 4) b2 |= m;
            if (v & 8) b3 |= m;
        }
        g_plane[0][idx] = b0;
        g_plane[1][idx] = b1;
        g_plane[2][idx] = b2;
        g_plane[3][idx] = b3;
        x += 8; src += 8; w -= 8;
    }
    while (w > 0) { p_pixel(x, y, (u8)(*src & 15)); ++x; ++src; --w; }
}

void vid_desktop_fill(int x, int y, int w, int h)
{
    /* Clip like every other public primitive (the Mode 13h path hands
       raw coordinates to the pre-clipped l_hline). */
    if (x < g_clx0) { w -= g_clx0 - x; x = g_clx0; }
    if (y < g_cly0) { h -= g_cly0 - y; y = g_cly0; }
    if (x + w > g_clx1) w = g_clx1 - x;
    if (y + h > g_cly1) h = g_cly1 - y;
    if (w <= 0 || h <= 0)
        return;
    if (g_mode == MODE_13) {
        /* Ordered dither BETWEEN adjacent ramp slots, vertically.
           32 slots over 200 rows gave a hard 6-row band per slot - 22
           measurable steps down the most-looked-at surface in the
           product, with the smoothstep collapsing the top 25 rows into
           one flat slab.  Every shipped wallpaper dithers its gradient;
           the one the shell draws itself did not.
           Dithering DOWN the rows rather than across them keeps one
           solid colour per scanline, so this still costs exactly one
           rep-stosd hline per row - the quality is free.  Adjacent slots
           differ by about 4/255, so the interleave reads as a smooth
           ramp rather than as stripes. */
        static const u8 DITH4[4] = { 0, 8, 4, 12 };   /* ordered, 0..15 */
        int yy, end = y + h;
        for (yy = y; yy < end; ++yy) {
            long pos = (long)yy * ((DGRAD_N - 1) * 16L) / SCREEN_H;
            int  sh  = (int)(pos >> 4);
            int  fr  = (int)(pos & 15);
            if (sh < 0) sh = 0;
            if (sh > DGRAD_N - 1) sh = DGRAD_N - 1;
            if (fr > (int)DITH4[yy & 3] && sh < DGRAD_N - 1)
                ++sh;
            l_hline(x, yy, w, (u8)(DGRAD_BASE + sh));
        }
    } else {
        /* 16 colours, dithered in FIVE densities of C_BLACK over the
           desktop colour.  Two earlier attempts were reverted as
           downgrades and the note here blamed the planar dither for "not
           behaving as assumed"; it was behaving exactly as written - it
           was a fixed 50% checker, so it could only ever produce ONE
           intermediate shade, and teal-then-half-black reads as two flat
           bands rather than a ramp.  p_pattern takes the density now.
           Black rather than a grey slot deliberately: mixing teal with
           C_SHADOW was the first attempt, and grey over a colour comes
           out muddy where black just darkens it. */
        /* 16 colours: FLAT, and this is the fourth attempt to change
           that.  Recording the real reason so nobody spends a fifth.

           The previous note blamed p_dither for "not behaving as
           assumed".  It was behaving exactly as written - it was a fixed
           50% checker, so it could only ever make ONE intermediate
           shade.  Generalising it to any density (p_pattern, above, now
           used by the scrollbar trough and the shadow) let me try the
           thing properly: five fixed bands, then a continuous 17-level
           4x4 Bayer ramp, then that ramp capped at 6/16 so it would not
           run to black the way Mode 13h's never does.  All three were
           photographed and all three are worse than the flat fill.

           The limit is the palette, not the code.  Darkening teal needs
           a darker TEAL to interleave with, and the 16 theme slots do
           not contain one - C_DKGRAY over teal is the muddy grey of
           attempt one, and C_BLACK goes muddy well before it goes dark.
           Two colours give too few levels, and the dither reads as
           stipple texture rather than as shade.  Mode 12h stays flat
           until it has a colour to ramp toward. */
        vid_fillrect(x, y, w, h, C_DESKTOP);
    }
}

void vid_rect(int x, int y, int w, int h, u8 color)
{
    if (w <= 0 || h <= 0)
        return;
    vid_hline(x, y, w, color);
    vid_hline(x, y + h - 1, w, color);
    vid_vline(x, y, h, color);
    vid_vline(x + w - 1, y, h, color);
}

void vid_bevel(int x, int y, int w, int h, u8 tl, u8 br)
{
    if (w <= 0 || h <= 0)
        return;
    vid_hline(x, y, w, tl);
    vid_vline(x, y, h, tl);
    vid_hline(x, y + h - 1, w, br);
    vid_vline(x + w - 1, y, h, br);
}

/* One rendered title-gradient scan line, cached by width.  Every window
   repaints its bar on every compose, and building the sweep with TGRAD_N
   vid_fillrect() calls cost h*TGRAD_N tiny _fmemsets per bar (384 at the
   usual 12-pixel bar).  The ramp only depends on the WIDTH, so render one
   row and _fmemcpy it down the bar: 12 row copies instead of 384 fills. */
static u8 far g_tgrad_row[SCREEN_W_MAX];
static int    g_tgrad_w = -1;

static void build_tgrad_row(int w)
{
    int i, x0 = 0, x1;
    if (w == g_tgrad_w)
        return;
    for (i = 0; i < TGRAD_N; ++i) {
        x1 = (i + 1) * w / TGRAD_N;
        if (x1 > x0)
            _fmemset(g_tgrad_row + x0, (u8)(TGRAD_BASE + i),
                     (unsigned)(x1 - x0));
        x0 = x1;
    }
    g_tgrad_w = w;
}

void vid_title_bar(int x, int y, int w, int h, bool_t active)
{
    if (g_mode == MODE_13 && active) {
        int row;
        if (w <= 0 || h <= 0)
            return;
        if (w > SCREEN_W_MAX)
            w = SCREEN_W_MAX;
        build_tgrad_row(w);
        for (row = 0; row < h; ++row)
            vid_copy_row(x, y + row, g_tgrad_row, w);
    } else if (g_mode != MODE_13 && active) {
        /* Mode 12h: dither ACROSS the bar, C_TITLE into C_LTBLUE.
           The desktop gradient could not be done this way because the
           palette has no darker teal to ramp toward - but the title bar
           does have its pair, navy and light blue, and that is the whole
           difference.  Mode 12h looked like the unfinished mode largely
           because this bar was flat beside Mode 13h's 32-step sweep.

           Seventeen VERTICAL BANDS, each one p_pattern call, rather than
           a pixel at a time: per-pixel would be w*h far calls through the
           4-plane read-modify-write - about 8800 for one bar, on every
           compose - which is exactly the cost just taken out of the
           scrollbar trough. */
        static const u8 BAYER[4][4] = {
            {  0,  8,  2, 10 },
            { 12,  4, 14,  6 },
            {  3, 11,  1,  9 },
            { 15,  7, 13,  5 }
        };
        int band;
        if (w <= 0 || h <= 0)
            return;
        vid_fillrect(x, y, w, h, C_TITLE);
        for (band = 1; band <= 16; ++band) {
            int bx0 = x + band * w / 17;
            int bx1 = x + (band + 1) * w / 17;
            u8  pat[4];
            int r, i;
            if (bx1 <= bx0)
                continue;
            for (r = 0; r < 4; ++r) {
                u8 p = 0;
                for (i = 0; i < 8; ++i)
                    if (band > (int)BAYER[r][i & 3])
                        p = (u8)(p | (0x80 >> i));
                pat[r] = p;
            }
            p_pattern(bx0, y, bx1 - bx0, h, C_LTBLUE, pat);
        }
    } else {
        /* An inactive bar: flat, as Windows drew it. */
        vid_fillrect(x, y, w, h, active ? C_TITLE : C_SHADOW);
    }
}

void vid_title_bar_v(int x, int y, int w, int h, bool_t active)
{
    if (g_mode == MODE_13 && active) {
        /* Sweep the same reserved ramp TOP->BOTTOM: navy at the top blooms
           into sky blue at the foot, so the banner glows toward the taskbar.
           One _fmemset per SCAN LINE (the band index is constant across a
           row), not one clipped vid_fillrect per band - the Start menu
           rebuilt this banner on every compose while it was open. */
        int row, band;
        if (w <= 0 || h <= 0)
            return;
        if (x < g_clx0) { w -= g_clx0 - x; x = g_clx0; }
        if (x + w > g_clx1) w = g_clx1 - x;
        if (w <= 0) return;
        for (row = 0; row < h; ++row) {
            int yy = y + row;
            if (yy < g_cly0 || yy >= g_cly1)
                continue;
            band = row * TGRAD_N / h;
            if (band >= TGRAD_N) band = TGRAD_N - 1;
            fmemset32(g_back + (unsigned)yy * 320 + (unsigned)x,
                      (u8)(TGRAD_BASE + band), (unsigned)w);
        }
    } else {
        vid_fillrect(x, y, w, h, active ? C_TITLE : C_SHADOW);
    }
}

void vid_present(void)
{
    if (g_mode == MODE_13) l_present();
    else                   p_present();
}

bool_t vid_cache_ok(void)
{
    return (g_cache_seg != 0) ? TRUE : FALSE;
}

void vid_cache_store(void)
{
    if (g_cache_seg == 0)
        return;
    if (g_mode == MODE_13) {
        copy32(g_cache_seg, 0, g_back_seg, 0, 16000);
    } else {
        int p;
        for (p = 0; p < 4; ++p)
            copy32(g_cache_seg + (unsigned)p * 2400, 0,
                   FP_SEG(g_plane[p]), FP_OFF(g_plane[p]), PLANE_BYTES / 4);
    }
}

/* Restore ONLY a rectangle of the cached scene.  The partial-present path
   used to restore all 64000 bytes and then recompose the entire window
   stack on top of it, just to blit a 40x12 clock. */
void vid_cache_restore_rect(int x, int y, int w, int h)
{
    if (g_cache_seg == 0)
        return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > SCREEN_W) w = SCREEN_W - x;
    if (y + h > SCREEN_H) h = SCREEN_H - y;
    if (w <= 0 || h <= 0)
        return;
    if (g_mode == MODE_13) {
        unsigned o = (unsigned)y * 320 + (unsigned)x;
        while (h-- > 0) {
            unsigned head = (4U - (o & 3U)) & 3U;
            unsigned n = (unsigned)w, nd, tail;
            unsigned so = o;
            if (n < 16) {
                _fmemcpy(MK_FP(g_back_seg, so), MK_FP(g_cache_seg, so), n);
            } else {
                if (head) {
                    _fmemcpy(MK_FP(g_back_seg, so), MK_FP(g_cache_seg, so),
                             head);
                    so += head; n -= head;
                }
                nd = n >> 2; tail = n & 3U;
                if (nd) copy32(g_back_seg, so, g_cache_seg, so, nd);
                if (tail)
                    _fmemcpy(MK_FP(g_back_seg, so + (nd << 2)),
                             MK_FP(g_cache_seg, so + (nd << 2)), tail);
            }
            o += 320;
        }
    } else {
        int p, r;
        int bx0 = x >> 3, bx1 = (x + w - 1) >> 3;
        unsigned bw = (unsigned)(bx1 - bx0 + 1);
        for (p = 0; p < 4; ++p) {
            unsigned o = (unsigned)y * PLANE_STRIDE + (unsigned)bx0;
            unsigned cs = g_cache_seg + (unsigned)p * 2400;
            for (r = 0; r < h; ++r) {
                _fmemcpy(g_plane[p] + o, MK_FP(cs, o), bw);
                o += PLANE_STRIDE;
            }
        }
    }
}

void vid_cache_restore(void)
{
    if (g_cache_seg == 0)
        return;
    if (g_mode == MODE_13) {
        copy32(g_back_seg, 0, g_cache_seg, 0, 16000);
    } else {
        int p;
        for (p = 0; p < 4; ++p)
            copy32(FP_SEG(g_plane[p]), FP_OFF(g_plane[p]),
                   g_cache_seg + (unsigned)p * 2400, 0, PLANE_BYTES / 4);
    }
}

/* Raw linear back buffer for the Light Show effects (Mode 13h only): one
   byte per pixel, pitch 320, 320x200.  NULL in Mode 12h (planar), where the
   256-colour effects are not offered. */
u8 far *vid_backbuffer(void)
{
    return (g_mode == MODE_13) ? g_back : (u8 far *)0;
}

void vid_blit_rect(int x, int y, int w, int h)
{
    if (w <= 0 || h <= 0)
        return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > SCREEN_W) w = SCREEN_W - x;
    if (y + h > SCREEN_H) h = SCREEN_H - y;
    if (w <= 0 || h <= 0)
        return;
    if (g_mode == MODE_13) l_blit(x, y, w, h);
    else                   p_blit(x, y, w, h);
}

void vga_pixel(int x, int y, u8 color)
{
    if (x < 0 || x >= SCREEN_W || y < 0 || y >= SCREEN_H)
        return;
    if (g_mode == MODE_13) l_vga_pixel(x, y, color);
    else                   p_vga_pixel(x, y, color);
}

/* ---- software mouse cursor -------------------------------------------
 * mouse.c used to poke the cursor out one pixel at a time: CURSOR_W *
 * CURSOR_H = 192 vga_pixel() calls per pointer move, each a medium-model
 * FAR call that re-ran four bound tests and the mode dispatch - and in
 * Mode 12h each one also reprogrammed six VGA registers and did a latch
 * read, some 1500 port writes to draw one arrow.  This takes the cursor
 * as two bitmask columns (bit 15 = the leftmost pixel of the cell),
 * clips once, dispatches once, and in Mode 12h programmes set/reset a
 * single time per colour.  It is the most frequent screen operation in
 * the shell, so it is worth the specialisation.
 * -------------------------------------------------------------------- */
void vid_cursor(int x, int y, const u16 far *outline, const u16 far *fill,
                int n, u8 c_out, u8 c_fill)
{
    int r;
    if (g_mode == MODE_13) {
        bool_t inside = (x >= 0 && x + 16 <= SCREEN_W) ? TRUE : FALSE;
        for (r = 0; r < n; ++r) {
            int yy = y + r;
            u16 mo, mf, mm;
            unsigned base;
            int c;
            if (yy < 0 || yy >= SCREEN_H)
                continue;
            mo = outline[r]; mf = fill[r];
            mm = (u16)(mo | mf);
            if (!mm)
                continue;
            base = (unsigned)yy * 320;
            for (c = 0; mm != 0; ++c, mm = (u16)(mm << 1)) {
                int xx;
                u16 bit;
                if (!(mm & 0x8000U))
                    continue;
                xx  = x + c;
                bit = (u16)(0x8000U >> c);
                if (!inside && (xx < 0 || xx >= SCREEN_W))
                    continue;
                g_vga[base + (unsigned)xx] = (mo & bit) ? c_out : c_fill;
            }
        }
        return;
    }

    /* Mode 12h: two passes, one per colour, with set/reset held across
       the whole pass and only the bit mask changing per byte column. */
    {
        int pass;
        map_mask(0x0F);
        outp(0x3CE, 0x01); outp(0x3CF, 0x0F);      /* enable set/reset     */
        for (pass = 0; pass < 2; ++pass) {
            const u16 far *m = pass ? fill : outline;
            outp(0x3CE, 0x00); outp(0x3CF, pass ? c_fill : c_out);
            for (r = 0; r < n; ++r) {
                int yy = y + r, bx, k;
                unsigned long q;
                unsigned idx;
                if (yy < 0 || yy >= SCREEN_H || m[r] == 0)
                    continue;
                bx = x >> 3;
                /* Lay the 16-bit mask into a byte-aligned 24-bit field:
                   bit 15 (pixel x) lands at bit 31 - (x & 7). */
                q   = (unsigned long)m[r] << (16 - (x & 7));
                idx = (unsigned)yy * PLANE_STRIDE + (unsigned)bx;
                for (k = 0; k < 3; ++k) {
                    u8 byte = (u8)((q >> (24 - k * 8)) & 0xFFUL);
                    int cx = bx + k;
                    volatile u8 d;
                    if (byte == 0 || cx < 0 || cx >= PLANE_STRIDE)
                        continue;
                    outp(0x3CE, 0x08); outp(0x3CF, byte);
                    d = g_vga[idx + (unsigned)k];   /* latch load          */
                    g_vga[idx + (unsigned)k] = 0xFF;
                    (void)d;
                }
            }
        }
        outp(0x3CE, 0x01); outp(0x3CF, 0x00);      /* restore defaults     */
        outp(0x3CE, 0x00); outp(0x3CF, 0x00);
        outp(0x3CE, 0x08); outp(0x3CF, 0xFF);
    }
}

/* ---- Drag outline (drawn straight to the visible framebuffer) ---------
 * A dashed 1px rectangle for "rubber band" window dragging: only the
 * outline follows the mouse, and the window itself moves once, on release.
 * That avoids repainting the whole scene every mouse step - the classic
 * (and fast) early-90s way to drag on modest hardware. */
void vid_outline(int x, int y, int w, int h, u8 color)
{
    int i;
    if (w < 2 || h < 2)
        return;
    for (i = 0; i < w; i += 2) {           /* top + bottom edges (dashed) */
        vga_pixel(x + i, y,         color);
        vga_pixel(x + i, y + h - 1, color);
    }
    for (i = 0; i < h; i += 2) {           /* left + right edges          */
        vga_pixel(x,         y + i, color);
        vga_pixel(x + w - 1, y + i, color);
    }
}

/* Restore an outline's 1px footprint from the back buffer (erases it). */
void vid_restore_outline(int x, int y, int w, int h)
{
    if (w < 2 || h < 2)
        return;
    vid_blit_rect(x, y,         w, 1);     /* top    */
    vid_blit_rect(x, y + h - 1, w, 1);     /* bottom */
    vid_blit_rect(x, y,         1, h);     /* left   */
    vid_blit_rect(x + w - 1, y, 1, h);     /* right  */
}
