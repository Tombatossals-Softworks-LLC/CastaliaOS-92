/* ======================================================================
 * fractal.h - Mandelbrot explorer applet for CASTALIA/386
 * ----------------------------------------------------------------------
 * Renders the Mandelbrot set in a window using 32-bit fixed-point integer
 * maths - no coprocessor, so it runs on a bare 386SX.  The image builds
 * progressively a few rows per BIOS tick (you watch it draw), into a far
 * off-screen buffer, and a reserved 128-entry DAC ramp gives it a smooth
 * spectrum.  Left-click any point to zoom in on it; reopen to reset.
 * ====================================================================== */
#ifndef FRACTAL_H
#define FRACTAL_H

#include "castalia.h"
#include "ui.h"

void   fractal_open(void);
void   fractal_draw(const Rect *client);
void   fractal_click(const Rect *client, int mx, int my);
bool_t fractal_key(int key);               /* J/M/R/O/I: mode, reset, zoom   */
bool_t fractal_tick(void);                 /* compute a few more rows        */

/* Blit only the rows the last tick computed (plus the progress line) into
   the back buffer; fills *dirty for a tiny partial present.  FALSE when a
   full fractal_draw() is required instead. */
bool_t fractal_step_draw(const Rect *client, Rect *dirty);

#endif /* FRACTAL_H */
