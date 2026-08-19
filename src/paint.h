/* ======================================================================
 * paint.h - Sketch Pad (pixel / icon editor) applet for CASTALIA/386
 * ----------------------------------------------------------------------
 * A zoomed 32x32 pixel editor: pick one of the 16 theme colours (or the
 * transparent "eraser"), draw on the magnified grid, and Save the result
 * as a .ICN file - the same format the desktop loads for icons (v0.4).
 * So the Sketch Pad is also Castalia's icon editor.
 *
 * Like the other applets it owns its window's client area; window.c routes
 * paint, clicks and (for free-hand drawing) button-held drags to it.
 * ====================================================================== */
#ifndef PAINT_H
#define PAINT_H

#include "castalia.h"

void   paint_reset(void);

/* Load a .ICN file by path and make it the working file (association). */
void   paint_open_file(const char *path);

/* Ask about an unsaved drawing before paint_open_file() replaces it. */
bool_t paint_ok_to_replace(void);
void   paint_draw(const Rect *client);

/* Keys: arrows move a cursor, Space/Enter applies the current tool, Tab
   cycles Pen/Fill/Line, [ and ] walk the palette, 0-9 pick a colour, X is
   the eraser, Del clears a cell, Esc drops a pending line anchor.  This
   was the only applet in the shell with no keyboard path at all. */
bool_t paint_key(int key);

/* A press: hits the toolbar, the palette, or paints one canvas pixel. */
bool_t paint_click(const Rect *client, int mx, int my);

/* Button-held motion over the canvas: paints, but only if the press that
   started the stroke was on the canvas.  The cell is drawn straight into the
   back buffer; paint_drag_rect() reports its screen rectangle so the caller
   can blit just that instead of repainting the whole scene. */
bool_t paint_drag(const Rect *client, int mx, int my);
void   paint_drag_rect(Rect *r);

/* Unsaved-canvas state, so the shell can warn before closing. */
bool_t paint_is_dirty(void);
void   paint_flush_state(void);

#endif /* PAINT_H */
