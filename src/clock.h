/* ======================================================================
 * clock.h - Clock & calendar applet for CASTALIA/386 (v0.5)
 * ----------------------------------------------------------------------
 * Shows a live HH:MM:SS readout, the full date, and a month calendar with
 * today highlighted.  It is stateless (it reads the DOS clock each paint);
 * main.c repaints it once a second while a clock window is open.
 * ====================================================================== */
#ifndef CLOCK_H
#define CLOCK_H

#include "castalia.h"

void clock_draw(const Rect *client);

#endif /* CLOCK_H */
