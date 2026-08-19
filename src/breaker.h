/* ======================================================================
 * breaker.h - Breaker (a block-breaker game) for CASTALIA/386
 * ----------------------------------------------------------------------
 * Bounce the ball off the paddle to smash the wall of bricks.  Steer the
 * paddle with the mouse or the Left/Right arrow keys.  Miss the ball and
 * you lose one of three balls; clear the wall to win.  The ball advances
 * off the BIOS tick, so it runs at the same pace on a 386 and an emulator.
 * Click (or press Space) after a game ends for a fresh wall.
 * ====================================================================== */
#ifndef BREAKER_H
#define BREAKER_H

#include "castalia.h"
#include "ui.h"

void   breaker_open(void);
void   breaker_draw(const Rect *client);
void   breaker_click(const Rect *client, int mx, int my);
bool_t breaker_key(int key);                 /* TRUE = repaint now (restart) */
bool_t breaker_tick(const Rect *client);     /* advance; TRUE = repaint      */
bool_t breaker_mouse(const Rect *client, int mx);  /* paddle follows mouse   */
bool_t breaker_step_draw(const Rect *client, Rect *dirty);  /* incremental   */

#endif /* BREAKER_H */
