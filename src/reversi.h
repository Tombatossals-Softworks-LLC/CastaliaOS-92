/* ======================================================================
 * reversi.h - Reversi (Othello) minigame for CASTALIA/386
 * ----------------------------------------------------------------------
 * You are the dark discs, the machine is the light discs.  Click a legal
 * square to place a disc and flip the flanked line; the machine replies
 * with a greedy, corner-hungry move.  Click after the board fills for a
 * fresh game.  Mouse only.
 * ====================================================================== */
#ifndef REVERSI_H
#define REVERSI_H

#include "castalia.h"
#include "ui.h"

void reversi_open(void);
void reversi_draw(const Rect *client);
void reversi_click(const Rect *client, int mx, int my);

/* F2 starts a new game; returns TRUE when the board changed. */
bool_t reversi_key(int key);

#endif /* REVERSI_H */
