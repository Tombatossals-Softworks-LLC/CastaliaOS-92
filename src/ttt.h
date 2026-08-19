/* ======================================================================
 * ttt.h - Tic-Tac-Toe minigame for CASTALIA/386
 * ----------------------------------------------------------------------
 * You are X, the machine is O.  Click an empty square to play; the machine
 * replies (it takes a win, blocks yours, else prefers the centre/corners).
 * When a game ends, a click starts a new one.
 * ====================================================================== */
#ifndef TTT_H
#define TTT_H

#include "castalia.h"

void ttt_open(void);                     /* fresh board                    */
void ttt_draw(const Rect *client);
void ttt_click(const Rect *client, int mx, int my);

/* F2 starts a new game; returns TRUE when the board changed. */
bool_t ttt_key(int key);

#endif /* TTT_H */
