/* ======================================================================
 * eyes.h - Eyes (a desk toy) for CASTALIA/386
 * ----------------------------------------------------------------------
 * Two big cartoon eyes that follow the mouse around the desktop, in the
 * grand tradition of every windowing system's first toy.  They blink now
 * and then.  Completely useless, absolutely mandatory.
 * ====================================================================== */
#ifndef EYES_H
#define EYES_H

#include "castalia.h"
#include "ui.h"

void   eyes_open(void);
void   eyes_draw(const Rect *client);

/* Track the mouse: TRUE when a pupil would visibly move (repaint). */
bool_t eyes_mouse(const Rect *client, int mx, int my);

/* Blinking: TRUE when the lids just opened or closed (repaint). */
bool_t eyes_tick(void);

#endif /* EYES_H */
