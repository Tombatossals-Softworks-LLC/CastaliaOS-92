/* ======================================================================
 * pong.h - Pong for CASTALIA/386
 * ----------------------------------------------------------------------
 * The 1972 classic against a beatable house AI.  The left paddle is
 * yours: it follows the mouse, or nudges with the arrow keys.  First to
 * seven wins.  pong_tick() advances the ball off the BIOS tick (so the
 * pace is identical on a 386 and an emulator) and returns TRUE when the
 * window should repaint.
 * ====================================================================== */
#ifndef PONG_H
#define PONG_H

#include "castalia.h"
#include "ui.h"

void   pong_open(void);
void   pong_draw(const Rect *client);
void   pong_click(const Rect *client, int mx, int my);
bool_t pong_key(int key);
bool_t pong_tick(const Rect *client);            /* TRUE = repaint        */
bool_t pong_mouse(const Rect *client, int my);   /* paddle follows mouse  */

#endif /* PONG_H */
