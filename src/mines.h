/* ======================================================================
 * mines.h - Minefield (a minesweeper) for CASTALIA/386
 * ----------------------------------------------------------------------
 * A 9x9 field with 10 hidden mines.  The top bar has a DIG/FLAG toggle
 * (mouse only, so no right button is needed) and a New button.  Uncover
 * every safe square to win; uncover a mine and it is over.  The first dig
 * is always safe.
 * ====================================================================== */
#ifndef MINES_H
#define MINES_H

#include "castalia.h"
#include "ui.h"

void mines_open(void);
void mines_draw(const Rect *client);
void mines_click(const Rect *client, int mx, int my);

/* F2 starts a new game; returns TRUE when the board changed. */
bool_t mines_key(int key);

/* TRUE while the game clock is running (repaint the status line). */
bool_t mines_tick(void);

/* Right-click flagging. TRUE = the board changed. */
bool_t mines_rclick(const Rect *client, int mx, int my);

#endif /* MINES_H */
