/* ======================================================================
 * scrap.h - Scrap Box text editor applet for CASTALIA/386 (v0.5)
 * ----------------------------------------------------------------------
 * A small word-wrapping plain-text editor with a fixed buffer.  Type to
 * insert, Backspace/Del to remove, arrows to move, Enter for a new line.
 * The New / Load / Save toolbar buttons use the modal dialogs for the
 * file name.  Like the file manager it owns its window's client area.
 * ====================================================================== */
#ifndef SCRAP_H
#define SCRAP_H

#include "castalia.h"

void   scrap_new(void);
void   scrap_open(const char *path);

/* Ask about unsaved text before scrap_open() replaces the buffer. */
bool_t scrap_ok_to_replace(void);
void   scrap_draw(const Rect *client);
bool_t scrap_click(const Rect *client, int mx, int my);
bool_t scrap_key(int key);

/* Unsaved-edit state, so the shell can warn before closing the window. */
bool_t scrap_is_dirty(void);
void   scrap_flush_state(void);

#endif /* SCRAP_H */
