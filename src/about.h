/* ======================================================================
 * about.h - The About box for CASTALIA/386
 * ----------------------------------------------------------------------
 * Not a static splash: a tabbed, animated "About" with the castle logo,
 * a rolling credits scroller, live system facts, the build ledger (lines
 * of code, modules, zero third-party libraries) and the licence - plus a
 * couple of easter eggs for the curious.
 * ====================================================================== */
#ifndef ABOUT_H
#define ABOUT_H

#include "castalia.h"

void   about_open(void);
void   about_draw(const Rect *client);
bool_t about_click(const Rect *client, int mx, int my);
bool_t about_key(int key);
bool_t about_tick(void);            /* animate; TRUE = repaint            */

#endif /* ABOUT_H */
