/* ======================================================================
 * g2048.h - 2048 for CASTALIA/386
 * ----------------------------------------------------------------------
 * The 2014 tile-sliding puzzle, backported to 1989: slide the 4x4 board
 * with the arrows, equal tiles merge and double, and a new 2 (or lucky 4)
 * drops after every move.  Reach 2048 to win; run out of moves and it is
 * over.  Turn-based - no tick, no timer, nothing to burn a 386SX on.
 * ====================================================================== */
#ifndef G2048_H
#define G2048_H

#include "castalia.h"
#include "ui.h"

void   g2048_open(void);
void   g2048_draw(const Rect *client);
void   g2048_click(const Rect *client, int mx, int my);
bool_t g2048_key(int key);

#endif /* G2048_H */
