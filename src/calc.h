/* ======================================================================
 * calc.h - Calculator applet for CASTALIA/386 (v0.5)
 * ----------------------------------------------------------------------
 * A small immediate-execution integer calculator (+ - * /).  Integer
 * maths is used deliberately: it needs no floating-point emulation, so
 * it stays fast on a 386SX-without-387 and keeps the executable lean.
 * Division truncates; a divide-by-zero shows "Error".
 *
 * Like the file manager, the calculator owns its window's client area:
 * window.c hands it the client rectangle and routes clicks and keys.
 * ====================================================================== */
#ifndef CALC_H
#define CALC_H

#include "castalia.h"

void calc_reset(void);
void calc_draw(const Rect *client);

/* Returns TRUE if the click/key changed something (caller should repaint). */
bool_t calc_click(const Rect *client, int mx, int my);
bool_t calc_key(int key);

/* TRUE when a key-press flash has expired and wants lifting. */
bool_t calc_tick(void);

#endif /* CALC_H */
