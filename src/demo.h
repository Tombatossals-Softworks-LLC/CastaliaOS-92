/* ======================================================================
 * demo.h - The Light Show: a full-screen demoscene player for CASTALIA/386
 * ----------------------------------------------------------------------
 * A salute to the Amiga/PC cracktros of 1989: plasma, copper bars, a warp
 * starfield, a fire effect and the Boing ball.  It takes over the screen
 * in Mode 13h (256 colours), paces itself off the BIOS tick so it looks
 * the same on a real 386 and in an emulator, and restores the desktop's
 * palette on the way out.
 *
 *   SPACE / ->  next effect      <-  previous effect      ESC  back to shell
 *
 * In Mode 12h (16 colours, planar) the effects are not offered; demo_run()
 * shows a short note and returns.
 * ====================================================================== */
#ifndef DEMO_H
#define DEMO_H

#include "castalia.h"

/* Run the Light Show until the user presses ESC.  theme is the active
   theme name (from the INI), used to restore the palette on exit. */
void demo_run(const char *theme);

/* The idle screensaver: bare effects, no captions, exits on any input. */
void demo_screensaver(const char *theme, bool_t have_mouse);

#endif /* DEMO_H */
