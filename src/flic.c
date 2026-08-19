/* ======================================================================
 * flic.c - Autodesk FLI/FLC animation player for CASTALIA/386
 * ----------------------------------------------------------------------
 * See flic.h.  The player streams the file frame by frame (never loading
 * the whole animation), decodes each frame's sub-chunks into a far canvas,
 * drives the 256-entry DAC and blits the canvas to the Mode-13h back
 * buffer.  Frame and sub-chunk boundaries are honoured by SEEKING to each
 * chunk's declared offset+size, so a mis-decoded or unknown chunk can
 * garble one frame but never desynchronise the stream.
 * ====================================================================== */
#include <stdio.h>
#include <dos.h>
#include <string.h>
#include "flic.h"
#include "video.h"
#include "font.h"
#include "keyboard.h"
#include "mouse.h"
#include "system.h"

/* ---- format constants ------------------------------------------------ */
#define FLI_MAGIC   0xAF11
#define FLC_MAGIC   0xAF12
#define FRAME_TYPE  0xF1FA

#define CK_COLOR256 4
#define CK_SS2      7      /* DELTA_FLC (word-oriented delta)             */
#define CK_COLOR64  11
#define CK_LC       12     /* DELTA_FLI (byte-oriented delta)            */
#define CK_BLACK    13
#define CK_BYTERUN  15     /* whole-frame RLE                            */
#define CK_COPY     16     /* whole-frame raw                            */

#define FW 320
#define FH 200

/* ---- the frame canvas (far) and the working palette ------------------ */
static unsigned    g_canseg = 0;
static u8 far     *g_canvas = (u8 far *)0;   /* W*H chunky pixels          */
static int         g_w, g_h;                 /* animation dimensions       */
static long        g_npix;                   /* W*H, the write bound       */
static u8          g_pal[768];               /* live 256-colour palette    */
static bool_t      g_pal_dirty;

/* ---- buffered sequential byte reader --------------------------------- */
static FILE       *g_ff;
static u8          g_rb[1024];
static int         g_rn, g_rp;

static void rd_reset(void) { g_rn = 0; g_rp = 0; }
static unsigned rbyte(void)
{
    if (g_rp >= g_rn) {
        g_rn = (int)fread(g_rb, 1, sizeof(g_rb), g_ff);
        g_rp = 0;
        if (g_rn <= 0) { g_rn = 0; return 0; }
    }
    return g_rb[g_rp++];
}
static unsigned rword(void)  { unsigned a = rbyte(); return a | (rbyte() << 8); }
static unsigned long rdword(void)
{
    unsigned long a = rword();
    return a | ((unsigned long)rword() << 16);
}
/* Seek to an absolute offset and drop the read-ahead buffer. */
static void rseek(long off) { fseek(g_ff, off, SEEK_SET); rd_reset(); }

/* Write one pixel to the canvas, clipped to its bounds (so a corrupt
   delta can never scribble outside the buffer). */
#define PUT(idx, v)  do { long _i = (idx); \
                          if (_i >= 0 && _i < g_npix) g_canvas[_i] = (u8)(v); } while (0)

/* ---- sub-chunk decoders (each consumes exactly its own bytes) -------- */

static void ck_color(int wide)     /* COLOR_256 (wide) or COLOR_64        */
{
    unsigned packets = rword();
    int idx = 0, p;
    for (p = 0; p < (int)packets; ++p) {
        int skip = (int)rbyte();
        int cnt  = (int)rbyte();
        int c;
        idx += skip;
        if (cnt == 0) cnt = 256;
        for (c = 0; c < cnt; ++c) {
            unsigned r, g, b;
            r = rbyte(); g = rbyte(); b = rbyte();      /* in stream order  */
            if (!wide) { r <<= 2; g <<= 2; b <<= 2; }   /* 0..63 -> 0..255 */
            if (idx >= 0 && idx < 256) {
                g_pal[idx * 3]     = (u8)r;
                g_pal[idx * 3 + 1] = (u8)g;
                g_pal[idx * 3 + 2] = (u8)b;
            }
            ++idx;
        }
    }
    g_pal_dirty = TRUE;
}

static void ck_black(void)
{
    if (g_canvas != (u8 far *)0) _fmemset(g_canvas, 0, (unsigned)g_npix);
}

static void ck_copy(void)          /* FLI_COPY: W*H raw bytes             */
{
    long i;
    for (i = 0; i < g_npix; ++i) g_canvas[i] = (u8)rbyte();
}

static void ck_byterun(void)       /* BYTE_RUN: per-row RLE               */
{
    int y;
    for (y = 0; y < g_h; ++y) {
        long base = (long)y * g_w;
        int x = 0;
        rbyte();                                   /* packet count: ignore */
        while (x < g_w) {
            int sc = (signed char)rbyte();
            if (sc == 0) break;                    /* guard: no progress   */
            if (sc > 0) {                          /* run of one byte      */
                unsigned v = rbyte();
                while (sc-- && x < g_w) { PUT(base + x, v); ++x; }
            } else {                               /* literal bytes        */
                int n = -sc;                       /* always consume them  */
                while (n--) {
                    unsigned v = rbyte();
                    if (x < g_w) { PUT(base + x, v); ++x; }
                }
            }
        }
    }
}

static void ck_lc(void)            /* DELTA_FLI (LC): byte delta          */
{
    int start = (int)rword();
    int lines = (int)rword();
    int i, y = start;
    for (i = 0; i < lines; ++i, ++y) {
        long base = (long)y * g_w;
        int packets = (int)rbyte();
        int x = 0, p;
        for (p = 0; p < packets; ++p) {
            int sc;
            x += (int)rbyte();                     /* column skip          */
            sc = (signed char)rbyte();
            if (sc >= 0) {                         /* literal (LC sign!)   */
                while (sc--) { unsigned v = rbyte(); PUT(base + x, v); ++x; }
            } else {                               /* run of one byte      */
                unsigned v = rbyte();
                int n = -sc;
                while (n--) { PUT(base + x, v); ++x; }
            }
        }
    }
}

static void ck_ss2(void)           /* DELTA_FLC (SS2): word delta         */
{
    int todo = (int)rword();
    int y = 0;
    while (todo > 0) {
        unsigned op = rword();
        unsigned hi = op & 0xC000;
        if (hi == 0xC000) {                        /* line skip (down)     */
            y += -(int)(short)op;
        } else if (hi == 0x8000) {                 /* last pixel of line   */
            PUT((long)y * g_w + (g_w - 1), op & 0xFF);
        } else {                                   /* op = packet count    */
            long base = (long)y * g_w;
            int x = 0, p, packets = (int)(op & 0x3FFF);
            for (p = 0; p < packets; ++p) {
                int sc;
                x += (int)rbyte();                 /* column skip          */
                sc = (signed char)rbyte();
                if (sc >= 0) {                     /* sc words, literal    */
                    while (sc--) {
                        unsigned lo, hib;
                        lo = rbyte(); hib = rbyte();
                        PUT(base + x, lo);     ++x;
                        PUT(base + x, hib);    ++x;
                    }
                } else {                           /* one word, -sc times  */
                    unsigned lo, hib;
                    int n = -sc;
                    lo = rbyte(); hib = rbyte();
                    while (n--) {
                        PUT(base + x, lo);     ++x;
                        PUT(base + x, hib);    ++x;
                    }
                }
            }
            ++y; --todo;
        }
    }
}

/* ---- present the canvas to the (Mode 13h) screen --------------------- */
static void flic_blit(void)
{
    int ox = (FW - g_w) / 2, oy = (FH - g_h) / 2, y;
    if (ox != 0 || oy != 0) vid_clear(0);          /* letterbox borders    */
    for (y = 0; y < g_h; ++y)
        vid_copy_row(ox, oy + y, g_canvas + (long)y * g_w, g_w);
    if (g_pal_dirty) { video_set_dac(0, 256, g_pal); g_pal_dirty = FALSE; }
    vid_present();
}

/* A mouse click only stops playback once we have seen the button released
   at least once, so the very click that launched the Cinema does not
   immediately close it. */
static bool_t g_click_armed;

/* Wait `delay` BIOS ticks from `since`, polling for an exit.  Returns
   TRUE if the user asked to stop (any key, or a fresh mouse click). */
static bool_t flic_wait(unsigned long since, unsigned long delay)
{
    for (;;) {
        if (kb_poll() != KEY_NONE) return TRUE;
        mouse_update();                 /* latch the live button state first  */
        if (!(mouse_buttons() & 3)) g_click_armed = TRUE;
        else if (g_click_armed)     return TRUE;
        if (sys_ticks() - since >= delay) return FALSE;
        sys_idle();
    }
}

/* ---- the "needs 256 colours" notice for Mode 12h --------------------- */
static void flic_notice(void)
{
    int cx = SCREEN_W / 2 - 150, cy = SCREEN_H / 2 - 20;
    vid_fillrect(0, 0, SCREEN_W, SCREEN_H, C_DESKTOP);
    vid_fillrect(cx, cy, 300, 40, C_FACE);
    vid_bevel(cx, cy, 300, 40, C_HILIGHT, C_SHADOW);
    font_draw(cx + 14, cy + 15,
              "The Cinema needs 256-colour mode (video=mode13h).", C_BLACK);
    vid_present();
    kb_flush();
    while (kb_poll() == KEY_NONE) sys_idle();
}

void flic_play(const char *path, const char *theme)
{
    u8  hdr[128];
    unsigned magic, frames;
    unsigned long speed_ms, frame1;
    unsigned long delay;
    const char *use;
    bool_t revealed;
    u8  wsav[176 * 3];                 /* DAC slots 16..191 (wallpaper window) */
    int wi;

    if (vid_backbuffer() == (u8 far *)0) { flic_notice(); return; }

    use = (path != NULL && path[0]) ? path : "ASSETS\\MEDIA\\CINEMA.FLC";
    g_ff = fopen(use, "rb");
    if (g_ff == (FILE *)0) { flic_notice(); return; }
    if (fread(hdr, 1, 128, g_ff) != 128) { fclose(g_ff); return; }

    magic  = hdr[4] | (hdr[5] << 8);
    if (magic != FLI_MAGIC && magic != FLC_MAGIC) { fclose(g_ff); return; }
    frames = hdr[6] | (hdr[7] << 8);
    g_w    = hdr[8]  | (hdr[9]  << 8);
    g_h    = hdr[10] | (hdr[11] << 8);
    if (g_w < 1 || g_h < 1 || g_w > FW || g_h > FH) { fclose(g_ff); return; }
    g_npix = (long)g_w * g_h;

    if (magic == FLC_MAGIC) {
        speed_ms = (unsigned long)hdr[16] | ((unsigned long)hdr[17] << 8) |
                   ((unsigned long)hdr[18] << 16) | ((unsigned long)hdr[19] << 24);
        frame1   = (unsigned long)hdr[80] | ((unsigned long)hdr[81] << 8) |
                   ((unsigned long)hdr[82] << 16) | ((unsigned long)hdr[83] << 24);
        if (frame1 < 128) frame1 = 128;
    } else {                                       /* FLI: speed in 1/70s  */
        unsigned jif = hdr[16] | (hdr[17] << 8);
        speed_ms = (unsigned long)jif * 1000UL / 70UL;
        frame1   = 128;
    }
    /* BIOS tick is ~55 ms; one tick minimum, cap the wait so a broken
       header cannot freeze the frame for seconds. */
    delay = (speed_ms + 27UL) / 55UL;
    if (delay < 1)  delay = 1;
    if (delay > 18) delay = 18;

    if (_dos_allocmem((unsigned)((g_npix + 15L) / 16L), &g_canseg) != 0) {
        fclose(g_ff); flic_notice(); return;
    }
    g_canvas = (u8 far *)MK_FP(g_canseg, 0);
    _fmemset(g_canvas, 0, (unsigned)g_npix);
    _fmemset(g_pal, 0, sizeof(g_pal));
    g_pal_dirty = FALSE;

    /* The animation drives all 256 DAC slots, which includes the window
       (16..191) that holds any GIF wallpaper's palette; save it so the
       desktop's colours come back exactly, not just the theme's 0..15. */
    for (wi = 0; wi < 176; ++wi)
        video_slot_rgb(16 + wi, &wsav[wi * 3], &wsav[wi * 3 + 1],
                       &wsav[wi * 3 + 2]);

    video_fade_out();                              /* dissolve the desktop */
    kb_flush();
    g_click_armed = FALSE;
    revealed = FALSE;

    for (;;) {                                     /* loop until a keypress */
        long foff = (long)frame1;
        unsigned f;
        bool_t stop = FALSE;
        for (f = 0; f < frames; ++f) {
            unsigned long fsize, ftype, nsub, s;
            long sub;
            rseek(foff);
            fsize = rdword();
            ftype = rword();
            nsub  = rword();
            if (fsize < 16) {                      /* malformed frame: stop */
                stop = TRUE;                       /* (looping on it would  */
                break;                             /*  hang the machine)    */
            }
            sub = foff + 16;                       /* first sub-chunk       */
            if (ftype == FRAME_TYPE) {
                for (s = 0; s < nsub; ++s) {
                    unsigned long ssize;
                    unsigned stype;
                    rseek(sub);
                    ssize = rdword();
                    stype = rword();
                    switch (stype) {
                        case CK_COLOR256: ck_color(1);  break;
                        case CK_COLOR64:  ck_color(0);  break;
                        case CK_BLACK:    ck_black();    break;
                        case CK_COPY:     ck_copy();     break;
                        case CK_BYTERUN:  ck_byterun();  break;
                        case CK_LC:       ck_lc();       break;
                        case CK_SS2:      ck_ss2();      break;
                        default: break;              /* PSTAMP/unknown: skip */
                    }
                    if (ssize < 6) break;            /* malformed sub-chunk  */
                    sub += ssize;
                }
            }
            foff += fsize;

            flic_blit();
            if (!revealed) { video_fade_in(); revealed = TRUE; }  /* reveal */
            if (flic_wait(sys_ticks(), delay)) { stop = TRUE; break; }
        }
        if (stop) break;
        if (frames == 0) break;                    /* nothing to loop       */
    }

    _dos_freemem(g_canseg);
    g_canseg = 0; g_canvas = (u8 far *)0;
    fclose(g_ff);

    video_fade_out();                              /* fade the last frame   */
    video_set_theme(theme);                        /* UI (0..15) + ramps    */
    video_set_dac(16, 176, wsav);                  /* wallpaper window back  */
    kb_flush();
}

bool_t flic_is_flic(const char *name)
{
    int n = 0;
    while (name[n]) ++n;
    if (n < 4 || name[n - 4] != '.') return FALSE;
    if (name[n - 3] != 'f' && name[n - 3] != 'F') return FALSE;
    if (name[n - 2] != 'l' && name[n - 2] != 'L') return FALSE;
    return (name[n - 1] == 'i' || name[n - 1] == 'I' ||
            name[n - 1] == 'c' || name[n - 1] == 'C') ? TRUE : FALSE;
}
