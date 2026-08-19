/* ======================================================================
 * peek.h - Hex Peek, the file inspector for CASTALIA/386
 * ----------------------------------------------------------------------
 * A classic hex viewer: offset, eight bytes, ASCII column.  The Disk
 * Cabinet routes every file it has no better association for straight
 * here, so double-clicking ANYTHING on a disk now shows you something
 * true about it.  Pages are read on demand (one small fseek+fread per
 * scroll), so a 500 MB file costs the same as a 5-byte one.
 * ====================================================================== */
#ifndef PEEK_H
#define PEEK_H

#include "castalia.h"

/* Point the viewer at a file (resets to offset 0). TRUE if it opened. */
bool_t peek_open_file(const char *path);

void   peek_draw(const Rect *client);

/* Scrolling keys (arrows / PgUp / PgDn / Home / End).
   TRUE = view moved, repaint. */
bool_t peek_key(int key);

#endif /* PEEK_H */
