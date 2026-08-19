/* ======================================================================
 * group.h - Program-group launcher windows for CASTALIA/386
 * ----------------------------------------------------------------------
 * The "Program Manager groups" of Castalia: a WIN_GROUP window is a small
 * windowed icon grid of RELATED built-in applets (a Toolbox of desk
 * utilities, an Arcade of games).  It works exactly like the Program
 * Drawer but its entries are fixed internal verbs, so the top-level
 * launcher stays short while the full toolset keeps growing behind two
 * tidy group icons.  Double-click an entry to open it.
 * ====================================================================== */
#ifndef GROUP_H
#define GROUP_H

#include "castalia.h"
#include "ui.h"          /* Rect */

#define GRP_TOOLS   0
#define GRP_ARCADE  1

/* Select the group to show; call right before opening the WIN_GROUP window. */
void        group_open(int which);

/* Title text and window pixel size (feed the size to open_centered). */
const char *group_title(int which);
void        group_window_size(int which, int *w, int *h);

/* Paint and input for the active group's client area. */
void        group_draw(const Rect *client);
bool_t      group_click(const Rect *client, int mx, int my, bool_t dbl);
bool_t      group_key(int key);

/* The internal verb the group just chose (valid after click/key return TRUE). */
const char *group_launch_command(void);

/* One-shot: the cell rectangle that launch came from (zoom-open origin). */
bool_t      group_take_launch_rect(Rect *r);

#endif /* GROUP_H */
