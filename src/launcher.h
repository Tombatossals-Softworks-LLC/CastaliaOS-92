/* ======================================================================
 * launcher.h - External program launching for CASTALIA/386
 * ----------------------------------------------------------------------
 * Launching a DOS program from a graphical shell means leaving graphics
 * mode entirely.  There is no multitasking here: we restore text mode,
 * run the program to completion via COMMAND.COM, wait for a keypress so
 * the user can read any output, then re-enter Mode 13h.  The caller is
 * responsible for repainting the desktop afterwards.
 *
 * Trade-offs (documented in ARCHITECTURE.TXT):
 *   - We stay resident (~100 KB) while the child runs, so the child has
 *     less conventional memory.  Fine for editors, file tools and a
 *     nested shell; a memory-hungry game can instead use the free-memory
 *     path (launcher_write_runfile) to unload Castalia entirely.
 *   - The current drive/directory are saved and restored, so launching
 *     from a shortcut path never disturbs the Disk Cabinet's location.
 * ====================================================================== */
#ifndef LAUNCHER_H
#define LAUNCHER_H

#include "castalia.h"   /* bool_t */

/* Run command (a DOS command line) with the given working directory.
   theme is re-applied after returning to graphics mode.  Returns the
   value from system(), or -1 if the program could not be started. */
int launcher_run(const char *path, const char *command, const char *theme);

/* Free-memory launch: write CASTRUN.BAT (cd to path, run command, return
   to Castalia's home directory) so the CASTSHEL.BAT wrapper can run the
   program with ALL of conventional memory and then relaunch the shell.
   The caller writes the file and then exits Castalia. */
bool_t launcher_write_runfile(const char *path, const char *command);

#endif /* LAUNCHER_H */
