/* ======================================================================
 * files.h - The Disk Cabinet (file manager) for CASTALIA/386
 * ----------------------------------------------------------------------
 * Lists the current directory using the DOS find-first/find-next calls,
 * lets the user select an entry, walk into sub-directories, walk back up
 * via "..", and launch executable files (.EXE/.COM/.BAT).
 *
 * Directory navigation is handled internally (chdir + rescan).  Running
 * an executable is NOT done here - files_click()/files_key() return
 * FILES_LAUNCH and the application layer performs the actual spawn, so
 * all program-launching policy stays in one place (launcher.c).
 * ====================================================================== */
#ifndef FILES_H
#define FILES_H

#include "castalia.h"

/* Return codes from files_click()/files_key(). */
#define FILES_NONE    0   /* nothing happened                            */
#define FILES_REDRAW  1   /* selection or directory changed -> repaint   */
#define FILES_LAUNCH  2   /* user activated an executable                */

/* Point the Disk Cabinet at a directory and scan it. */
void        files_open(const char *path);

/* Open the Windows-95 "My Computer" root: the drives shown as big icons.
   Double-clicking a drive drops into the folder view (files_open). */
void        files_open_computer(void);

/* Rescan the current directory (e.g. after returning from a program). */
void        files_rescan(void);

/* Paint the listing inside the given client rectangle. */
void        files_draw(const Rect *client);

/* Handle a click in the client area. dbl = TRUE for a double-click. */
int         files_click(const Rect *client, int mx, int my, bool_t dbl);

/* Handle a key while the Disk Cabinet has focus (arrows/Enter/Backspace). */
int         files_key(int key);

/* Scroll-thumb dragging: a press on the thumb begins it (files_click),
   the main loop feeds the pointer's y while the button stays down, and
   release ends it.  files_thumb_drag returns TRUE when the list moved. */
bool_t      files_thumb_active(void);
bool_t      files_thumb_drag(int my);
void        files_thumb_end(void);

/* The directory currently shown (e.g. "C:\DOS"). */
const char *files_cwd(void);

/* After FILES_LAUNCH: the bare command to run (e.g. "EDIT.COM"). */
const char *files_launch_command(void);

#endif /* FILES_H */
