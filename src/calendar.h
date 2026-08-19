/* ======================================================================
 * calendar.h - A month-view calendar for CASTALIA/386
 * ----------------------------------------------------------------------
 * Opens on the DOS clock's current month with today ringed in red.  The
 * arrows (or Left/Right) turn the month, PgUp/PgDn turn the year, and
 * Home snaps back to today.  Pure drawing - no state files.
 * ====================================================================== */
#ifndef CALENDAR_H
#define CALENDAR_H

#include "castalia.h"
#include "ui.h"

void   calendar_open(void);
void   calendar_draw(const Rect *client);
bool_t calendar_click(const Rect *client, int mx, int my);
bool_t calendar_key(int key);

#endif /* CALENDAR_H */
