/* ======================================================================
 * video.h - VGA graphics subsystem for CASTALIA/386
 * ----------------------------------------------------------------------
 * Two video modes are supported behind ONE set of primitives, selected
 * at start-up from CASTALIA.INI (video=mode13h | mode12h):
 *
 *     Mode 13h  320x200x256  linear  - the default (fast, low memory)
 *     Mode 12h  640x480x16   planar  - roomier, more Windows-like
 *
 * WHY MODE 13h IS THE DEFAULT (and the contrast with Mode 12h):
 *
 *   Mode 13h gives a *linear* framebuffer at A000:0000 - exactly one
 *   byte per pixel, offset = y*320 + x.  No bit planes, no latches,
 *   no Sequencer/Graphics-Controller register juggling.  That keeps
 *   every drawing primitive a handful of instructions, which matters
 *   a great deal on a 16 MHz 386SX with a 16-bit bus.
 *
 *   The whole frame is 320*200 = 64000 bytes, which is < 64 KB, so a
 *   complete off-screen back buffer fits in a SINGLE real-mode segment.
 *   That makes flicker-free, double-buffered drawing trivial.  In Mode
 *   12h the planar 640x480 buffer is 150 KB and cannot be addressed as
 *   one flat segment without bank/plane tricks.
 *
 *   We also get a fully programmable 256-entry DAC, so we can define
 *   the exact 1990s gray/teal/navy palette we want.
 *
 * Mode 12h IS NOW IMPLEMENTED (see video.c).  Every module draws ONLY
 * through the vid_* primitives below; no module pokes A000:0000 directly
 * except video.c.  The planar back-end (plane masks via port 3C4h, the
 * Set/Reset cursor path via 3CEh) lives entirely inside video.c behind
 * these same prototypes, so NO UI code changed to gain the higher
 * resolution.  That abstraction is the entire point of this header.
 * ====================================================================== */
#ifndef VIDEO_H
#define VIDEO_H

#include "castalia.h"

/* Logical screen dimensions.  These are now runtime values so the same
   UI code serves both Mode 13h (320x200) and Mode 12h (640x480); they
   are set by video_init() and never change afterwards.  The SCREEN_W /
   SCREEN_H names are kept for source compatibility. */
extern int castalia_screen_w;
extern int castalia_screen_h;
#define SCREEN_W castalia_screen_w
#define SCREEN_H castalia_screen_h

/* Maximum dimensions (Mode 12h) - handy for any fixed-size reasoning. */
#define SCREEN_W_MAX 640
#define SCREEN_H_MAX 480

/* ----------------------------------------------------------------------
 * Theme colour indices.
 *
 * These are palette SLOTS, not RGB values.  video_set_theme() loads the
 * actual RGB into the DAC for slots 0..15, so the same index means
 * "window face gray" regardless of the selected theme.  All UI code
 * refers to colours by these names only.
 * -------------------------------------------------------------------- */
#define C_BLACK    0   /* text, outlines                                 */
#define C_TITLE    1   /* active title bar (navy)                        */
#define C_DESKTOP  2   /* desktop background                             */
#define C_FACE     3   /* 3D control face (light gray)                   */
#define C_SHADOW   4   /* 3D shadow edge (mid gray)                      */
#define C_HILIGHT  5   /* 3D highlight edge                              */
#define C_WHITE    6   /* window client background / title text          */
#define C_LTBLUE   7   /* accents / inactive title                       */
#define C_RED      8   /* close glyph, alerts                            */
#define C_YELLOW   9   /* folder body                                    */
#define C_DKYELLOW 10  /* folder edge                                    */
#define C_GREEN    11  /* terminal text, OK indicators                   */
#define C_DKGRAY   12  /* deep shadow / icon outline                     */
#define C_BLUE     13  /* links / selection                              */
#define C_CYAN     14  /* secondary accent                               */
#define C_CREAM    15  /* note paper / lists                             */

/* ---- Lifecycle ------------------------------------------------------- */
/* Sets the requested graphics mode, allocates the back buffer and loads
   the default palette.  mode is a string from CASTALIA.INI: "mode12h" /
   "12" selects 640x480x16 (planar); anything else (incl. NULL) selects
   320x200x256 (Mode 13h).  Returns TRUE on success, FALSE if the back
   buffer could not be allocated (text mode is restored before return). */
bool_t video_init(const char *mode);

/* Restores the original text mode and frees the back buffer. */
void   video_shutdown(void);

/* Drop to 80x25 text mode (mode 3) - used before shelling to DOS. */
void   video_text_mode(void);

/* Re-enter Mode 13h after returning from a shelled program. The back
   buffer is preserved, so the caller only needs to redraw and present. */
void   video_graphics_mode(void);

/* Load one of the named themes into the DAC. Unknown name -> "classic".
   Names: classic | penumbra | bureau | winsteel | moncloa | workbench |
   ocean | rose | midnight | amber | matrix | redmond | sunset | forest |
   hotdog | slate | sakura | dos. */
void   video_set_theme(const char *name);

/* Per-slot RGB overrides (from the INI [colors] section). set[i] != 0
   means slot i is overridden by rgb[i*3..]. Applied on the next
   video_set_theme(). */
void   video_set_overrides(const u8 *set, const u8 *rgb);

/* TRUE when the high-resolution (640x480) mode is active. */
bool_t video_is_big(void);

/* ---- Palette fades ----------------------------------------------------
 * video.c keeps a shadow copy of the whole 256-entry DAC.  While "dark"
 * every palette write lands in the shadow only, so a scene can be composed
 * and presented invisibly and then revealed with video_fade_in().  All of
 * these are no-ops when fades are disabled (animations=false in the INI),
 * so callers never need to check the config themselves. */
void   video_enable_fades(bool_t on);
bool_t video_is_dark(void);
void   video_blackout(void);   /* hardware DAC to black in one step        */
void   video_fade_out(void);   /* smooth fade to black (vsync paced)       */
void   video_fade_in(void);    /* smooth fade back to the shadow palette   */

/* Program a run of DAC registers from an RGB-0..255 table (count*3 bytes).
   Used by the boot splash for smooth 256-colour gradients; only meaningful
   in Mode 13h (Mode 12h shows colours 0..15 only). */
void   video_set_dac(int start, int count, const u8 *rgb255);

/* Read a theme slot's current RGB (0..255 each) from the DAC shadow.  The
   ICO loader uses this to map an icon's palette to the nearest theme slot,
   so real .ICO art recolours with the active theme. */
void   video_slot_rgb(int slot, u8 *r, u8 *g, u8 *b);

/* Read a NAMED theme's slot RGB (0..255 each) without applying it - the
   Settings panel previews every theme's colours side by side with this. */
void   video_theme_rgb(const char *name, int slot, u8 *r, u8 *g, u8 *b);

/* ---- Off-screen drawing primitives (all draw to the BACK buffer) -----
 * Every primitive clips to the screen rectangle, so callers may pass
 * partially or fully off-screen coordinates safely.
 * -------------------------------------------------------------------- */
void   vid_clear(u8 color);
void   vid_pixel(int x, int y, u8 color);
void   vid_hline(int x, int y, int w, u8 color);
void   vid_vline(int x, int y, int h, u8 color);
void   vid_rect(int x, int y, int w, int h, u8 color);      /* outline  */
void   vid_fillrect(int x, int y, int w, int h, u8 color);  /* solid    */

/* 50%-dithered (checkerboard) fill - the translucent-shadow primitive.
   Row-wise byte/mask writes in both modes, far faster than per-pixel. */
void   vid_dither_rect(int x, int y, int w, int h, u8 color);

/* Draw up to 8 pixels from a bitmask in one call (bit 7 = (x,y), bit 6 =
   (x+1,y), ...).  This is the glyph-row fast path used by font.c: one call
   per scan line instead of one clipped vid_pixel per lit pixel. */
void   vid_bits8(int x, int y, u8 bits, u8 color);

/* Draw a whole glyph in one call: n rows of 8-pixel bitmasks from (x,y).
   Clips and dispatches once for the character instead of once per scan
   line - the text fast path (see font.c). */
void   vid_glyph(int x, int y, const u8 far *rows, int n, u8 color);

/* Copy a row of chunky pixels (one byte each, values 0..255) to (x,y).
   Mode 13h: a single _fmemcpy into the linear back buffer.  Mode 12h: a
   chunky-to-planar conversion in whole bytes.  Used by the Fractal applet
   to move its off-screen image into the frame at full speed. */
void   vid_copy_row(int x, int y, const u8 far *src, int w);

/* Wait for the start of the next vertical blanking interval (~60-70 Hz).
   Used to pace fades and the zoom/unfurl animations smoothly. */
void   vid_vsync(void);

/* Fill a region with the desktop backdrop: a subtle vertical gradient of
   the theme's desktop colour in Mode 13h, a flat C_DESKTOP in Mode 12h.
   Used by the pattern=gradient desktop. */
void   vid_desktop_fill(int x, int y, int w, int h);

/* One-pixel 3D bevel: top & left use tl, bottom & right use br. */
void   vid_bevel(int x, int y, int w, int h, u8 tl, u8 br);

/* Fill a window title bar.  An ACTIVE bar in Mode 13h is drawn as a
   left-to-right dark->light gradient (a reserved DAC ramp tied to the
   theme's title colour); an inactive bar, or any bar in Mode 12h, is a
   flat fill (C_TITLE active / C_SHADOW inactive). */
void   vid_title_bar(int x, int y, int w, int h, bool_t active);

/* Vertical sibling of vid_title_bar: a top-to-bottom gradient of the theme's
   title colour (navy at the top blooming into sky blue at the foot) in Mode
   13h, a flat fill in Mode 12h.  Used for the Start menu's brand banner. */
void   vid_title_bar_v(int x, int y, int w, int h, bool_t active);

/* ---- Full-frame scene cache -------------------------------------------
 * A second off-screen buffer (allocated from DOS at start-up; everything
 * degrades gracefully if it does not fit).  desktop.c composes the static
 * desktop background - backdrop, icons, labels - ONCE, stores it here, and
 * every later scene rebuild starts from a single fast 386 dword copy
 * instead of re-deriving gradients, icons and text from scratch.  This is
 * the difference between "compose the world" and "memcpy the world". */
bool_t vid_cache_ok(void);
void   vid_cache_store(void);      /* back buffer -> cache                */
void   vid_cache_restore(void);    /* cache -> back buffer                */
void   vid_cache_restore_rect(int x, int y, int w, int h);

/* ---- clip rectangle --------------------------------------------------
 * Every scene primitive clips to this box as well as to the screen.  It
 * fences an applet inside its client area (a miscomputed width truncates
 * instead of painting over the window frame) and it lets a partial present
 * recompose only the region it is about to blit.  Default: whole screen.
 * -------------------------------------------------------------------- */
void   vid_set_clip(int x, int y, int w, int h);
void   vid_clear_clip(void);
/* Software mouse cursor: two bitmask columns (bit 15 = leftmost pixel of
   the cell), drawn straight to the visible framebuffer.  Clips once and,
   in Mode 12h, programmes the VGA set/reset registers once per colour. */
void   vid_cursor(int x, int y, const u16 far *outline, const u16 far *fill,
                  int n, u8 c_out, u8 c_fill);

void   vid_set_clip_isect(const Rect *a, const Rect *b);
void   vid_get_clip(Rect *r);
bool_t vid_clip_hits(int x, int y, int w, int h);

/* ---- Presentation ---------------------------------------------------- */
/* Copy the whole back buffer to the visible VGA framebuffer. */
void   vid_present(void);

/* Raw linear back buffer (Mode 13h only): one byte per pixel, pitch 320,
   320x200.  The Light Show effects write whole frames here at full speed
   and then call vid_present().  NULL in Mode 12h (planar). */
u8 far *vid_backbuffer(void);

/* Copy just the given rectangle from the back buffer to VGA. Used to
   "erase" the software mouse cursor by repainting the scene under it. */
void   vid_blit_rect(int x, int y, int w, int h);

/* ---- Direct framebuffer write (used only by the software cursor) ------
 * Writes straight to the visible VGA memory, bypassing the back buffer,
 * so the cursor can float above the scene without disturbing it. */
void   vga_pixel(int x, int y, u8 color);

/* ---- Drag outline (rubber-band window dragging) -----------------------
 * vid_outline draws a dashed 1px rectangle straight to the visible frame;
 * vid_restore_outline repaints that footprint from the back buffer. */
void   vid_outline(int x, int y, int w, int h, u8 color);
void   vid_restore_outline(int x, int y, int w, int h);

#endif /* VIDEO_H */
