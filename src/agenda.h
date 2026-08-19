/* ======================================================================
 * agenda.h - The Agenda (to-do list) for CASTALIA/386
 * ----------------------------------------------------------------------
 * A small persistent task list: check things off with a click or the
 * space bar, add entries through the text dialog, delete with Del.
 * Everything lives in AGENDA.TXT next to CASTALIA.EXE as plain
 * "[ ] buy diskettes" lines, so any editor (or the Scrap Box) can read
 * and write the same list.  Saved after every change - the file is a
 * few hundred bytes, the write is invisible even on a slow disk.
 * ====================================================================== */
#ifndef AGENDA_H
#define AGENDA_H

#include "castalia.h"

void   agenda_open(void);              /* load AGENDA.TXT (once)           */
void   agenda_draw(const Rect *client);

/* TRUE when an edit was made and the save it asked for did not happen -
   because AGENDA.TXT held more than the window can carry and rewriting
   it would delete the rest, or because the disk refused the write.  The
   Agenda has no dirty flag (it writes on every change), so this is its
   equivalent: work that exists only on the screen.  The quit path asks
   on it.  NOT "is the file clipped": a long file the user only LOOKED
   at has lost nothing and must not interrogate them on the way out. */
bool_t agenda_unsaved(void);

/* TRUE = list changed or selection moved, repaint the window. */
bool_t agenda_click(const Rect *client, int mx, int my);
bool_t agenda_key(int key);

#endif /* AGENDA_H */
