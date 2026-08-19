/* ======================================================================
 * menu.h - The Dominus pop-up launcher menu for CASTALIA/386
 * ----------------------------------------------------------------------
 * A single modal pop-up list built from the [shortcut] entries in
 * CASTALIA.INI.  It opens anchored to the launcher button, tracks a
 * hovered item, and on click returns the chosen shortcut (or NULL if the
 * click fell outside, which dismisses it).  The application layer turns
 * the returned shortcut's command into an action.
 * ====================================================================== */
#ifndef MENU_H
#define MENU_H

#include "castalia.h"
#include "config.h"

/* Open the menu from a bottom-left anchor (typically the launcher button
   top-left).  The items array must remain valid while the menu is open
   (it points straight at the Config). */
void               menu_open(const CfgShortcut *items, int count,
                             int anchor_x, int anchor_bottom);

void               menu_close(void);
bool_t             menu_is_open(void);
void               menu_draw(void);

/* Screen footprint of the open menu (with its shadow), for a partial blit. */
void               menu_bounds(Rect *r);

/* Update the hovered item from the mouse. Returns TRUE if it changed. */
bool_t             menu_hover(int x, int y);

/* Process a click. Returns the chosen shortcut and closes the menu, or
   NULL (also closing) if the click was outside the menu. */
const CfgShortcut *menu_click(int x, int y);

/* TRUE when the record just returned by menu_click/menu_key is a recent
   DOCUMENT (Start > Documents) rather than a verb or a program: its
   command is a file name and its path the folder, for open_document. */
bool_t             menu_result_is_document(void);

/* Drive the open menu from the keyboard: arrows walk it, Right/Enter
   opens a submenu or launches a leaf, Left/Esc backs out or dismisses.
   Returns TRUE when the menu changed (repaint it); *out receives the
   chosen shortcut when a leaf was launched, else NULL. */
bool_t             menu_key(int key, const CfgShortcut **out);

/* One-shot: the screen rectangle of the item the last menu_click() chose,
   so a window opened from the menu can zoom out of that very spot. */
bool_t             menu_take_click_rect(Rect *r);

#endif /* MENU_H */
