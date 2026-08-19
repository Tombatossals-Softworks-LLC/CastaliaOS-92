/* ======================================================================
 * desktop.h - Desktop, icon grid and taskbar for CASTALIA/386
 * ----------------------------------------------------------------------
 * Owns the background, the grid of desktop icons (sourced from the
 * config), the bottom taskbar with the "Castalia" launcher button, and
 * the clock.  It paints and hit-tests; the application layer (main.c)
 * turns clicks into actions.
 * ====================================================================== */
#ifndef DESKTOP_H
#define DESKTOP_H

#include "castalia.h"
#include "config.h"
#include "video.h"
#include "font.h"

#define TASKBAR_H (font_h() + 6)         /* 14 at the 8px font            */
#define TASKBAR_Y (SCREEN_H - TASKBAR_H) /* bottom of the screen, any mode */

void        desktop_init(const Config *cfg);

/* Paint the background and all icons (not the taskbar). */
void        desktop_draw(void);

/* Partial-present support: TRUE when the scene cache holds a valid composed
   background, and the on-top-of-the-cache half of desktop_draw(). */
bool_t      desktop_cache_ready(void);
void        desktop_draw_over_cache(void);

/* Paint the taskbar. launcher_pressed shows the button held down while
   the Dominus menu is open. */
void        desktop_draw_taskbar(bool_t launcher_pressed);

/* Hit testing. */
int         desktop_icon_at(int x, int y);     /* icon index or -1       */
bool_t      desktop_launcher_hit(int x, int y);
int         desktop_taskbar_button_at(int x, int y); /* window button or -1 */

/* The point the Dominus menu should anchor to (button top-left). */
void        desktop_launcher_anchor(int *x, int *bottom);

/* Draw the first-run "Click here to begin" balloon over the Start orb. */
void        desktop_draw_start_hint(void);

/* Selection. */
void        desktop_select(int idx);
int         desktop_selected(void);

/* Keyboard navigation over the icon grid (used when no window has the
   focus).  Arrows move the selection; ENTER on a selected icon asks for
   a launch.  Returns TRUE if the key was consumed; *launch is set to the
   icon index to start, or -1 when the key only moved the selection. */
bool_t      desktop_key(int key, int *launch);

/* Invalidate the cached desktop background (icon set / backdrop changed),
   and switch the backdrop pattern / tiled wallpaper at run time (the
   Settings panel).  A NULL or empty wallpaper path turns tiling off. */
void        desktop_set_pattern(const char *p);
void        desktop_set_wallpaper(const char *path);
void        desktop_swap_bitmaps(int a, int b);

/* Small footprints for partial presents: an icon cell (with its focus
   outline and label band) and the taskbar clock. */
void        desktop_cell_rect(int i, Rect *r);
void        desktop_clock_rect(Rect *r);

/* Clickable footprint of the tray speaker (mute toggle), left of the clock. */
void        desktop_tray_speaker_rect(Rect *r);

/* Screen rectangle of taskbar window-button i (of count buttons), used as
   the target/source of the minimize/restore zoom animations. */
void        desktop_bar_button_rect(int i, int count, Rect *r);

/* Paint the desktop backdrop (GIF wallpaper, else themed pattern) into a
   rectangle given in screen coordinates.  Used by the About banner. */
void        desktop_blit_backdrop(int x, int y, int w, int h);

/* Icon accessors. */
int         desktop_icon_count(void);
const char *desktop_icon_command(int idx);
const char *desktop_icon_name(int idx);

#endif /* DESKTOP_H */
