/* ======================================================================
 * depot.h - Depot (a box-pushing warehouse puzzle) for CASTALIA/386
 * ----------------------------------------------------------------------
 * Ten hand-made warehouse floors (every one machine-verified solvable):
 * push each crate onto a marked bay.  Crates only push - never pull - so
 * one careless shove can strand a crate in a corner forever... which is
 * why U takes a move back and R restarts the floor.  N/P browse floors.
 * ====================================================================== */
#ifndef DEPOT_H
#define DEPOT_H

#include "castalia.h"
#include "ui.h"

void   depot_open(void);
void   depot_draw(const Rect *client);
void   depot_click(const Rect *client, int mx, int my);
bool_t depot_key(int key);             /* TRUE = repaint                  */

#endif /* DEPOT_H */
