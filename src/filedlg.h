/* ======================================================================
 * filedlg.h - the modal file picker for Castalia 92
 * ----------------------------------------------------------------------
 * Every path prompt in the shell was a bare text field: Load and Save in
 * the Scrap Box and the Sketch Pad, the Hex Peek, the Gramophone, Copy
 * and Move in the Disk Cabinet.  You had to know the path already and
 * type it correctly, with no way to look - in a shell whose whole point
 * is that you can see your disk.  This is the control the era used and
 * the one thing most conspicuously missing from it.
 *
 * It draws over the frozen scene and runs its own event loop, exactly
 * like dialog.c - same software-cursor discipline, same drain of accrued
 * INT 33h presses on the way out so the click that dismissed it never
 * replays on the desktop.
 * ====================================================================== */
#ifndef FILEDLG_H
#define FILEDLG_H

#include "castalia.h"

/* Pick a file.  `pattern` is an 8.3 wildcard filter for the listing
   ("*.TXT", "*.IC?"; NULL or "" means everything).  `path` carries the
   suggested name in and the chosen one out, as a full path the caller
   can hand straight to fopen().  `saving` allows a name that does not
   exist yet and titles the button "Save" instead of "Open".

   Going up from a drive root lists the DRIVES, the level My Computer
   occupies elsewhere in the shell; picking one switches to it.  Without
   that the picker could only reach what was under the drive it opened
   on - the name field holds 8.3 and no more, so a path to another drive
   cannot be typed either - and saving to a floppy was not possible.

   TRUE when the user chose something; FALSE on Cancel or Esc.  The
   current directory is left exactly as it was found, DRIVE included -
   the picker browses without disturbing wherever the Disk Cabinet
   happens to be. */
bool_t filedlg(const char *title, const char *pattern,
               char *path, int cap, bool_t saving);

/* Pick a FOLDER rather than a file: same dialog, listing directories
   only, with the browsed path coming back in `path`.  Multi-file Copy
   and Move need it - a set of files has no single name to return - and
   it is what "add a folder of tunes" should have been asking with all
   along instead of a text field. */
bool_t filedlg_folder(const char *title, char *path, int cap);

#endif /* FILEDLG_H */
