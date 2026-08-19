/* ======================================================================
 * mouse.h - Mouse input and software cursor for CASTALIA/386
 * ----------------------------------------------------------------------
 * Uses the INT 33h mouse driver, but ONLY for relative motion counters
 * (function 0Bh) and button state (function 03h).  See mouse.c for the
 * detailed reasoning, but in short:
 *
 *   In Mode 13h the absolute position reported by INT 33h fn 03h is
 *   ambiguous - some drivers use a 640-wide virtual screen, some 320.
 *   Reading the relative mickey counters instead and integrating them
 *   ourselves removes that ambiguity entirely, so the cursor can always
 *   reach every pixel on both DOSBox and real hardware.  The price is
 *   that WE own the sensitivity (MOUSE_DIV in mouse.c), which is a fair
 *   trade for correctness on unknown hardware.
 *
 * The cursor is a software sprite drawn straight to VGA memory.  The
 * scene already lives in the video back buffer, so "erasing" the cursor
 * is just repainting the scene rectangle under it - no separate
 * save-under buffer is needed.
 * ====================================================================== */
#ifndef MOUSE_H
#define MOUSE_H

#include "castalia.h"

/* Button bit masks (matches INT 33h fn 03h BX layout). */
#define MB_LEFT   0x01
#define MB_RIGHT  0x02
#define MB_MIDDLE 0x04

/* Cursor sprite dimensions (used by the event loop for erase rects). */
#define CURSOR_W  12
#define CURSOR_H  16

/* Initialise the driver. Returns TRUE if a mouse is present. */
bool_t mouse_init(void);

/* Poll the driver: integrate motion into the cursor position and latch
   the current button state. Call once per event-loop iteration. */
void   mouse_update(void);

/* Current cursor position / buttons. */
int    mouse_x(void);
int    mouse_y(void);
int    mouse_buttons(void);

/* Number of left-button presses since the last call (from the driver's
   hardware counter, so presses are never lost to a slow repaint).  main()
   drives clicks and double-clicks off this instead of polled button edges. */
int    mouse_take_lpresses(void);
int    mouse_take_rpresses(void);
bool_t mouse_left_now(void);   /* fresh driver query (drag-arming race) */

/* Fill *x,*y,*buttons in one call (spec-required convenience form). */
void   mouse_get_state(int *x, int *y, int *buttons);

/* Constrain the cursor to a rectangle (inclusive min, exclusive max). */
void   mouse_set_bounds(int x0, int y0, int x1, int y1);

/* Show / hide the software cursor. While hidden, mouse_draw() is a
   no-op and the cursor footprint is left clean. */
void   mouse_show(void);
void   mouse_hide(void);
bool_t mouse_visible(void);

/* Choose the cursor shape: the hourglass while a slow operation runs, the
   arrow otherwise.  The caller is responsible for redrawing the cursor. */
void   mouse_set_busy(bool_t on);

/* Draw the cursor at the current position directly to VGA. Records the
   footprint so mouse_erase() can repaint it from the back buffer. */
void   mouse_draw(void);

/* Repaint the scene under the last drawn cursor footprint. */
void   mouse_erase(void);

#endif /* MOUSE_H */
