/* ======================================================================
 * charmap.h - Character Map utility for CASTALIA/386
 * ----------------------------------------------------------------------
 * A reference grid of the printable font characters (0x20..0x7F).  Click a
 * cell to see that character's decimal and hex code.
 * ====================================================================== */
#ifndef CHARMAP_H
#define CHARMAP_H

#include "castalia.h"

void charmap_open(void);
void charmap_draw(const Rect *client);
void charmap_click(const Rect *client, int mx, int my);

/* Arrows walk the grid; typing a character selects it. */
bool_t charmap_key(int key);

#endif /* CHARMAP_H */
