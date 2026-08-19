/* ======================================================================
 * quadrix.h - Quadrix (a falling-blocks game) for CASTALIA/386
 * ----------------------------------------------------------------------
 * The inevitable one: seven tetrominoes rain into a 10x18 well.  Arrows
 * steer, Up rotates, Down soft-drops, SPACE slams the piece home.  Full
 * rows dissolve for points; every eighth row cleared speeds the rain up.
 * Paced off the BIOS tick like every other applet, PC-speaker effects
 * when sound=true, and the whole well repaints through the fast path.
 * ====================================================================== */
#ifndef QUADRIX_H
#define QUADRIX_H

#include "castalia.h"
#include "ui.h"

void   quadrix_open(void);
void   quadrix_draw(const Rect *client);
void   quadrix_click(const Rect *client, int mx, int my);
bool_t quadrix_key(int key);           /* TRUE = repaint                  */
bool_t quadrix_tick(void);             /* gravity; TRUE = repaint         */

#endif /* QUADRIX_H */
