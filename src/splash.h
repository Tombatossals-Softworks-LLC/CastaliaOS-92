/* ======================================================================
 * splash.h - Boot splash for CASTALIA/386
 * ----------------------------------------------------------------------
 * A 1989-flavoured boot screen.  In Mode 13h (256 colours) it paints a
 * smooth gradient sky, copper-style colour bars and a chrome gradient
 * "CASTALIA/386" logo - the kind of thing a sharp Amiga/PC studio shipped
 * at the end of the decade.  In Mode 12h (16 colours) it shows a clean
 * panelled version of the same.
 *
 * Only DAC slots 16..255 are touched, so the theme colours (0..15) stay
 * intact and the desktop paints normally afterwards.
 * ====================================================================== */
#ifndef SPLASH_H
#define SPLASH_H

void splash_show(void);

/* The Windows-95 farewell: a black screen with big amber "It's now safe to
   turn off your computer." text.  Shown at shut down; returns when a key is
   pressed (the caller then fades out and drops back to DOS). */
void splash_shutdown(void);

/* The Windows-95 "blue screen of death" easter egg (Run "bsod" / "crash").
   Fills the screen with the classic blue panel and waits for a key. */
void bsod_show(void);

#endif /* SPLASH_H */
