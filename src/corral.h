/* ======================================================================
 * corral.h - Corral (a wall-building ball-capture game) for CASTALIA/386
 * ----------------------------------------------------------------------
 * Balls carom around a walled pen; a click grows a two-armed fence that
 * hardens when it lands, and any pen without a ball is claimed.  Corral
 * 75% of the floor to advance a level.  Space flips the fence's axis.
 * ====================================================================== */
#ifndef CORRAL_H
#define CORRAL_H

#include "castalia.h"

void   corral_open(void);

/* Advance the balls and the growing fence (top window only, like Pong);
   TRUE when the board moved and the window should repaint. */
bool_t corral_tick(const Rect *cl);

void   corral_draw(const Rect *cl);
void   corral_click(const Rect *cl, int mx, int my);

/* Track the pointer for the aiming guide; TRUE when the guide moved. */
bool_t corral_mouse(const Rect *cl, int mx, int my);

/* Space/Tab flips the fence axis; Enter advances the end screens. */
bool_t corral_key(int key);

#endif /* CORRAL_H */
