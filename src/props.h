/* ======================================================================
 * props.h - Windows-95 property sheets and pop-ups for CASTALIA/386
 * ----------------------------------------------------------------------
 * The parts of the 1995 shell that are a modal box rather than a window:
 *
 *   popup_menu        the right-click context menu
 *   props_file        File Properties: type, location, size, dates, and
 *                     the four DOS attributes as live checkboxes
 *   props_drive       Drive Properties: label, type, used/free/capacity
 *                     and the tilted pie chart
 *   props_datetime    Date/Time Properties: a month calendar and a
 *                     ticking clock that set the DOS date and time
 *   shutdown_dialog   "Shut Down Castalia": shut down, restart, or back
 *                     to the MS-DOS prompt
 *
 * All are synchronous, like dialog.c: each draws over the frozen scene
 * and runs its own small event loop, so the caller must repaint the whole
 * scene afterwards (dialog_took_over() reports it).
 * ====================================================================== */
#ifndef PROPS_H
#define PROPS_H

#include "castalia.h"

/* Pop up a menu of n items with its top-left near (x,y), kept on screen.
   An item that is exactly "-" is drawn as a separator.  Returns the chosen
   index, or -1 when dismissed (Esc, or a click outside). */
int  popup_menu(int x, int y, const char * const *items, int n);

/* Properties of `name` in the current directory (a file or a folder).
   Attribute changes are applied on OK; TRUE when something changed. */
bool_t props_file(const char *name);

/* Properties of drive `letter` ('A'..'Z'). */
void props_drive(char letter);

/* Date/Time Properties; TRUE when the DOS date or time was set. */
bool_t props_datetime(void);

/* The Shut Down dialog. */
#define SD_CANCEL  0
#define SD_OFF     1                   /* the "safe to turn off" farewell */
#define SD_RESTART 2                   /* reboot the machine              */
#define SD_DOS     3                   /* straight back to the prompt     */
int  shutdown_dialog(void);

#endif /* PROPS_H */
