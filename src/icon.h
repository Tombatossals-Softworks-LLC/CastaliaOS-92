/* ======================================================================
 * icon.h - External bitmap icons for CASTALIA/386 (v0.4)
 * ----------------------------------------------------------------------
 * A tiny, human-readable 32x32 icon format (.ICN) so desktop icons can be
 * custom art instead of only the procedural shapes in ui.c.  The format
 * is deliberately trivial to author by hand and to parse on DOS:
 *
 *     ; an optional comment line
 *     CICN 32 32
 *     <32 rows, each 32 characters>
 *
 * where each character is a palette index 0-9/A-F, or '.' (or space) for
 * a transparent pixel.  The 16 indices are the theme slots (C_* in
 * video.h), so an icon recolours automatically with the active theme.
 *
 * Icons are stored at 32x32 and drawn at scale 1 (Mode 13h) or 2 (Mode
 * 12h), exactly like the procedural icons, so a single asset serves both
 * resolutions.
 * ====================================================================== */
#ifndef ICON_H
#define ICON_H

#include "castalia.h"

#define ICON_BM_SIZE     32
#define ICON_TRANSPARENT 0xFF

typedef struct {
    bool_t loaded;
    u8     px[ICON_BM_SIZE * ICON_BM_SIZE];   /* 0..15, or ICON_TRANSPARENT */
} IconBitmap;

/* The bitmap tables are held FAR by their owners (desktop.c, drawer.c):
   at ~1 KB per icon they were 24 KB of near data crowding the nearly-full
   DGROUP, so these entry points take far pointers. */

/* Load a .ICN file. Returns TRUE on success (ic->loaded is also set). */
bool_t icon_load(const char *path, IconBitmap far *ic);

/* Draw a loaded icon with its top-left at (x,y); scale is 1 or 2.
   Transparent pixels are skipped, so the background shows through. */
void   icon_draw(const IconBitmap far *ic, int x, int y, int scale);

/* Draw a loaded icon into a `size` x `size` box at (x,y), nearest-neighbour
   sampled (down- or up-scaled from the native 32x32).  Used for the tiny
   taskbar Start-button badge.  Transparent pixels are skipped. */
void   icon_draw_box(const IconBitmap far *ic, int x, int y, int size);

/* Drop the cached 2x expansion (see icon.c).  icon_load() calls this
   itself, so a reload is always covered; a caller only needs it if it
   changes an IconBitmap's PIXELS behind icon.c's back - as desktop.c
   does when it swaps two slots' contents to rearrange the icons. */
void icon_cache_flush(void);

#endif /* ICON_H */
