/* ======================================================================
 * snake.h - Serpent (a snake game) for CASTALIA/386
 * ----------------------------------------------------------------------
 * Steer with the arrow keys; eat the apples to grow; do not hit a wall or
 * your own tail.  snake_tick() advances the snake off the BIOS tick (so it
 * runs at the same pace on a 386 and an emulator) and returns TRUE when the
 * window should repaint.  Click after a crash for a fresh game.
 * ====================================================================== */
#ifndef SNAKE_H
#define SNAKE_H

#include "castalia.h"
#include "ui.h"

void   snake_open(void);
void   snake_draw(const Rect *client);
void   snake_click(const Rect *client, int mx, int my);
bool_t snake_key(int key);
bool_t snake_tick(void);          /* advance; TRUE = repaint             */
bool_t snake_step_draw(const Rect *client, Rect *dirty); /* incremental   */

#endif /* SNAKE_H */
