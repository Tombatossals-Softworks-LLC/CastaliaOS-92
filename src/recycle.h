/* ======================================================================
 * recycle.h - the Recycle Bin for CASTALIA/386
 * ----------------------------------------------------------------------
 * Delete in the Disk Cabinet no longer means gone.  A file on a fixed
 * local disk is moved into that drive's hidden X:\RECYCLED folder (the
 * folder Windows 95 used) under a DCn.EXT name, and one line in
 * X:\RECYCLED\INFO.TXT remembers where it came from.  The Recycle Bin
 * window lists every drive's bin and can restore an entry, delete it for
 * good, or empty the lot.  Floppies and network drives have no bin -
 * there, as in Windows 95, Delete is final - and Shift+Del skips it.
 * ====================================================================== */
#ifndef RECYCLE_H
#define RECYCLE_H

#include "castalia.h"

bool_t recycle_available(const char *name); /* name's drive keeps a bin */
bool_t recycle_file(const char *name);      /* into the bin; FALSE = left */
bool_t recycle_shift_held(void);            /* Shift down: delete for good */

void   recycle_open(void);                  /* (re)read every drive's bin */
void   recycle_draw(const Rect *client);
bool_t recycle_key(int key);
bool_t recycle_click(const Rect *client, int mx, int my);
void   recycle_release(void);               /* window closed: free the list */

#endif /* RECYCLE_H */
