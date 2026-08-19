/* ======================================================================
 * drawer.h - Program Drawer for CASTALIA/386 (v0.7)
 * ----------------------------------------------------------------------
 * The Program Drawer is the visual companion to the Dominus menu: a
 * window that shows the configured launch entries (the [shortcut] list)
 * as a grid of labelled icons, so programs can be started with a glance
 * and a double-click instead of from a text menu.  Each entry uses its
 * own .ICN bitmap when one is configured, else the procedural icon that
 * matches its command - exactly like the desktop icons.
 *
 * The drawer never spawns anything itself: a double-click (or Enter) only
 * records which entry was chosen and returns TRUE; main.c then launches it
 * through the one shared execute_command() path, so internal verbs and
 * external DOS programs behave identically to every other launch site.
 * ====================================================================== */
#ifndef DRAWER_H
#define DRAWER_H

#include "castalia.h"
#include "config.h"

/* Bind the drawer to the configuration: load any per-entry bitmap icons
   and clear the selection.  Call right before opening the WIN_DRAWER
   window so the grid reflects the current shortcuts. */
void drawer_open(const Config *cfg);

/* INI shortcuts + the entries scanned from [drawer] scan= (call after
   drawer_open) - the grid size main.c should open the window for. */
int  drawer_entry_count(void);

/* Paint the program grid into the window's client rectangle. */
void drawer_draw(const Rect *cl);

/* Handle a click at (mx,my) in the client rectangle.  Selects the entry
   under the pointer; on a double-click it records that entry for launch
   and returns TRUE.  Otherwise returns FALSE. */
bool_t drawer_click(const Rect *cl, int mx, int my, bool_t dbl);

/* Handle a key for the focused drawer (arrows move the selection, Enter
   launches it).  Returns TRUE when a launch was requested. */
bool_t drawer_key(int key);

/* The command / working path of the entry chosen for launch (valid right
   after drawer_click()/drawer_key() returned TRUE). */
const char *drawer_launch_command(void);
const char *drawer_launch_path(void);

/* TRUE if the chosen entry asked for a free-memory (unload) launch. */
bool_t      drawer_launch_freemem(void);

/* One-shot: the cell rectangle that launch came from (zoom-open origin). */
bool_t      drawer_take_launch_rect(Rect *r);

/* Fill (*w,*h) with a good window size (frame included, Mode-13h base
   pixels) to show `count` entries without scrolling. */
void drawer_window_size(int count, int *w, int *h);

#endif /* DRAWER_H */
