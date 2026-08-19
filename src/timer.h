/* ======================================================================
 * timer.h - Stopwatch & kitchen timer for CASTALIA/386
 * ----------------------------------------------------------------------
 * Two timekeepers in one window, both driven by the 18.2 Hz BIOS tick:
 * a stopwatch with big double-height digits, lap memory and tenths, and
 * a countdown timer (1..99 minutes) that beeps and flashes at zero.
 * SPACE starts/stops the watch, L laps, R resets.
 * ====================================================================== */
#ifndef TIMER_H
#define TIMER_H

#include "castalia.h"
#include "ui.h"

void   timer_open(void);
void   timer_draw(const Rect *client);
void   timer_click(const Rect *client, int mx, int my);
bool_t timer_key(int key);             /* TRUE = repaint                  */
bool_t timer_tick(void);               /* TRUE = the readout changed      */

#endif /* TIMER_H */
