/* ======================================================================
 * gif.h - GIF image decoder for CASTALIA/386
 * ----------------------------------------------------------------------
 * Decodes the first frame of a GIF87a/GIF89a into an 8-bit paletted
 * buffer.  Built for desktop wallpaper in Mode 13h: the shell's UI uses
 * DAC slots 0..15 and the two gradient ramps sit at 192..255, so a GIF's
 * palette is loaded into the free window 16..191 and the caller shifts
 * pixel values by +16.  LZW is decoded with the classic prefix/suffix/
 * stack tables in a 16 KB far scratch block, freed as soon as the frame
 * is out - so the resident cost is just the decoded picture.
 * ====================================================================== */
#ifndef GIF_H
#define GIF_H

#include "castalia.h"

/* Decode the first image of `path` into `out` (far, maxw*maxh bytes of
   raw GIF palette indices).  On success fills *w,*h (clamped to max) and
   pal768 (up to 256 RGB triples, 0..255) and *ncol.  Returns FALSE on any
   malformed or unsupported file. */
bool_t gif_decode(const char *path, unsigned char far *out,
                  int maxw, int maxh, int *w, int *h,
                  unsigned char *pal768, int *ncol);

#endif /* GIF_H */
