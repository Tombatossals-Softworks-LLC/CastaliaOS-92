/* ======================================================================
 * flic.h - Autodesk FLI/FLC animation player for CASTALIA/386
 * ----------------------------------------------------------------------
 * A from-scratch decoder for the .FLI (0xAF11) and .FLC (0xAF12) formats
 * - the full-motion "flic" animations of the DOS era (Autodesk Animator /
 * Animator Pro).  It plays them full-screen in Mode 13h: the linear
 * 320x200x256 back buffer IS a flic frame, so there is no format
 * impedance at all.  Everything is integer, so it runs on a bare 386SX.
 *
 * Supported chunk types (the ones real flics use): COLOR_256, COLOR_64,
 * DELTA_FLC (SS2), DELTA_FLI (LC), BLACK, BYTE_RUN (RLE) and FLI_COPY.
 * PSTAMP (postage-stamp preview) and any unknown chunk are skipped by
 * their length, so an unfamiliar sub-chunk never desynchronises the
 * stream.
 * ====================================================================== */
#ifndef FLIC_H
#define FLIC_H

#include "castalia.h"

/* Play an FLI/FLC animation from `path`, full-screen, looping, until the
   user presses a key or clicks.  It is a blocking call (like the Light
   Show): it takes over the screen, then restores the `theme` palette on
   exit - the caller repaints the desktop (damage_all()).  Mode 13h only;
   in Mode 12h it shows a short "needs 256-colour mode" notice.  A NULL or
   empty path plays the bundled ASSETS\MEDIA\CINEMA.FLC. */
void flic_play(const char *path, const char *theme);

/* TRUE if `name` ends in .FLI or .FLC (case-insensitive) - the file
   browser uses this to route a double-clicked animation here. */
bool_t flic_is_flic(const char *name);

#endif /* FLIC_H */
