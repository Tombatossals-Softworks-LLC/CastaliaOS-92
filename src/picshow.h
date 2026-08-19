/* ======================================================================
 * picshow.h - Picture Show (a full-screen GIF slideshow) for Castalia 92
 * ----------------------------------------------------------------------
 * Shows every .GIF in ASSETS\ICONS full-screen, one at a time: LEFT and
 * RIGHT (or SPACE/ENTER) walk the gallery, ESC returns to the desktop.
 * Each picture's palette loads into the free DAC window 16..191 like the
 * wallpaper does, so the 16-colour UI palette survives for the caption.
 * The caller restores the theme and the wallpaper afterwards.
 * ====================================================================== */
#ifndef PICSHOW_H
#define PICSHOW_H

#include "castalia.h"

void picshow_run(void);

#endif /* PICSHOW_H */
