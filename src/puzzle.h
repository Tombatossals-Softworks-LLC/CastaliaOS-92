/* ======================================================================
 * puzzle.h - Sliding 15-puzzle minigame for CASTALIA/386
 * ----------------------------------------------------------------------
 * The classic 4x4 sliding-tile puzzle: click a tile next to the gap to
 * slide it, and put 1..15 back in order.  Shuffled (always solvable) when
 * opened.
 * ====================================================================== */
#ifndef PUZZLE_H
#define PUZZLE_H

#include "castalia.h"

void puzzle_open(void);                  /* shuffle a fresh board          */
void puzzle_draw(const Rect *client);
void puzzle_click(const Rect *client, int mx, int my);

/* F2 starts a new game; returns TRUE when the board changed. */
bool_t puzzle_key(int key);

#endif /* PUZZLE_H */
