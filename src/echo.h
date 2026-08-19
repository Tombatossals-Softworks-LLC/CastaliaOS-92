/* ======================================================================
 * echo.h - Echo (a sound-memory game) for CASTALIA/386
 * ----------------------------------------------------------------------
 * The machine flashes a growing sequence across four glowing panels, each
 * with its own tone; repeat it back by clicking the panels in order.  Every
 * round that you clear adds one more step.  A wrong panel ends the game;
 * click (or press Space) for a fresh sequence.  Named for the nymph Echo -
 * a fitting muse for a game of call-and-response near the Castalian spring.
 *
 * The sequence playback advances off the BIOS tick (so it keeps the same
 * rhythm on a 386 and an emulator) and the tones use the non-blocking
 * PC-speaker effects in music.c, so the desktop never freezes.
 * ====================================================================== */
#ifndef ECHO_H
#define ECHO_H

#include "castalia.h"
#include "ui.h"

void   echo_open(void);
void   echo_draw(const Rect *client);
void   echo_click(const Rect *client, int mx, int my);
bool_t echo_key(int key);            /* TRUE = repaint now (restart)        */
bool_t echo_tick(void);              /* advance playback; TRUE = repaint     */

#endif /* ECHO_H */
