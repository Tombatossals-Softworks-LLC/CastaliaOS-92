/* ======================================================================
 * window.h - Window manager for CASTALIA/386
 * ----------------------------------------------------------------------
 * A tiny stacking window manager backed by a fixed pool (no malloc).
 * Each window has a kind that selects how its client area is drawn and
 * which input it accepts.  Z-order is an array of indices, bottom first;
 * focusing moves a window to the top.
 *
 * The manager never spawns programs itself.  When the file-manager
 * window asks to launch an executable, the relevant entry point returns
 * WM_LAUNCH and the application layer (main.c) performs the spawn.  This
 * keeps all "leave the GUI / run DOS / come back" policy in one place.
 * ====================================================================== */
#ifndef WINDOW_H
#define WINDOW_H

#include "castalia.h"
#include "font.h"

/* Window kinds. */
#define WIN_GENERIC  0
#define WIN_ABOUT    1
#define WIN_SYSINFO  2
#define WIN_FILEMAN  3
#define WIN_CALC     4
#define WIN_SCRAP    5
#define WIN_CLOCK    6
#define WIN_PAINT    7
#define WIN_DRAWER   8
#define WIN_INSPECT  9
#define WIN_BENCH    10
#define WIN_MUSIC    11
#define WIN_PUZZLE   12
#define WIN_TTT      13
#define WIN_CHARMAP  14
#define WIN_GROUP    15
#define WIN_MINES    16
#define WIN_REVERSI  17
#define WIN_SNAKE    18
#define WIN_COLORS   19
#define WIN_CARD     20
#define WIN_BREAKER  21
#define WIN_ECHO     22
#define WIN_FRACT    23
#define WIN_QUADRIX  24
#define WIN_DEPOT    25
#define WIN_TIMER    26
#define WIN_EYES     27
#define WIN_HELP     28
#define WIN_PATIENCE 29
#define WIN_LIGHTS   30
#define WIN_SETTINGS 31
#define WIN_ORACLE   32
#define WIN_PEEK     33
#define WIN_AGENDA   34
#define WIN_MEDIA    35
#define WIN_PONG     36
#define WIN_CAL      37
#define WIN_G2048    38
#define WIN_FIND     39
#define WIN_CORRAL   40
#define WIN_TYPIST   41
#define WIN_KIND_COUNT 42           /* size of per-kind tables            */

#define WM_MAX       6              /* maximum simultaneous windows       */
#define TITLE_H      (font_h() + 4) /* title bar height (12 at 8px font)  */

/* Result codes from the input entry points. */
#define WM_MISS    (-1)     /* event was not over any window              */
#define WM_NONE      0      /* handled, no special action                 */
#define WM_REDRAW    1      /* handled, caller should repaint             */
#define WM_LAUNCH    2      /* file manager wants to launch a program     */
#define WM_LAUNCH_PROG 3    /* Program Drawer wants to launch an entry    */
#define WM_LAUNCH_GROUP 4   /* a group window wants to launch an applet    */
#define WM_MINIMIZE  5      /* a window was minimized to the taskbar      */
#define WM_STRUCT    6      /* window geometry/stack changed: full redraw  */
#define WM_CLOSED    7      /* a window was closed                         */
#define WM_RAISED    8      /* a background window came forward (only) -
                               present the two windows + taskbar, not all  */

void   wm_init(void);

/* Open a window; returns its id, or -1 if the pool is full. */
int    wm_open(int kind, const char *title, int x, int y, int w, int h);

void   wm_close_id(int id);
void   wm_close_top(void);
bool_t wm_any_open(void);

/* TRUE if any open window is of the given kind (used for the live clock). */
bool_t wm_has_kind(int kind);

/* Topmost window's kind (-1 if none) and outer footprint (incl. shadow).
   Used by the partial-present fast path for animating windows. */
int    wm_top_kind(void);
bool_t wm_top_rect(Rect *r);
bool_t wm_top_client_rect(Rect *r);

/* Paint every window, bottom to top. The topmost is drawn "active". */
void   wm_draw_all(void);

/* Repaint only the topmost window (used by the partial-present fast path). */
void   wm_draw_top(void);

/* Repaint ONLY the topmost window's client area, leaving the frame, the
   title bar and the caption boxes alone; *client gets the repainted rect.
   FALSE when there is no focused window (or it is shaded) - the caller
   then falls back to wm_draw_top(). */
bool_t wm_draw_top_client(Rect *client);

/* ---- Taskbar window buttons -----------------------------------------
 * The taskbar shows one button per open window in a STABLE slot order (so
 * buttons never jump when focus changes).  desktop.c draws them and routes
 * clicks; the manager owns the state.
 * -------------------------------------------------------------------- */
int         wm_bar_count(void);              /* number of open windows      */
const char *wm_bar_title(int i);             /* title of the i-th button    */
bool_t      wm_bar_active(int i);            /* is it the focused window?   */
bool_t      wm_bar_min(int i);               /* is it minimized?            */
void        wm_bar_click(int i);             /* restore / minimize / raise  */
void        wm_cycle(void);                  /* Alt+Tab: next window         */
void        wm_minimize_id(int id);
void        wm_set_min(int id, bool_t m);
void        wm_minimize_all(void);           /* "show desktop"               */
void        wm_cascade(void);                /* F5: staggered arrangement    */
void        wm_tile(void);                   /* F6: grid arrangement         */
bool_t      wm_window_rect(int id, Rect *r);  /* outer footprint of a window */

/* Take a pending window animation so the caller can play a zoom over the
   still-current scene.  kind: 0=close, 1=minimize, 2=restore-from-taskbar.
   For kinds 1 and 2, *bar_i / *bar_n give the window's taskbar button
   index and the button count, so the zoom can target the actual button
   (desktop_bar_button_rect).  FALSE if no animation is pending. */
bool_t      wm_take_anim(Rect *r, int *kind, int *bar_i, int *bar_n);

/* Mouse button press at (x,y); dbl = TRUE on a double-click.
   Returns WM_MISS if no window was under the point. */
int    wm_press(int x, int y, bool_t dbl);

/* Drain a pending maximize/restore zoom (old and new frame rects); TRUE
   once per toggle.  main.c plays the outline zoom before repainting. */
bool_t wm_take_maxzoom(Rect *from, Rect *to);

/* TRUE if (x,y) lies over any visible window (for right-click routing). */
bool_t wm_over_window(int x, int y);

/* Right button. WM_REDRAW = a window handled it, WM_MISS = no window was
   under the pointer, WM_NONE = a window was but had nothing to do. */
int    wm_rpress(int x, int y);

/* Point the Help window at whatever was focused when F1 was pressed, so
   it opens on that window's own keys instead of one static page. */
void   help_set_context(int kind);

/* TRUE if (x,y) lies over the FOCUSED (topmost visible) window - a click
   there that only changes window content can repaint just that window. */
bool_t wm_point_on_top(int x, int y);

/* Outer footprint of the first window of `kind`: 1 = *r filled, 0 = the
   window exists but is minimized (nothing to repaint), -1 = none open.
   Lets a background window's animation tick blit only its own rectangle. */
int    wm_kind_rect(int kind, Rect *r);

/* Title-bar drag OR corner-grip resize: wm_drag updates the rubber-band
   outline (the window itself changes only on release); wm_drag_rect
   reports that outline rectangle; wm_release commits the move/resize and
   returns TRUE if the window actually changed (so the caller repaints
   once).  Windows never shrink below the size they opened at, so every
   applet's layout stays valid - they only grow roomier. */
void   wm_drag(int x, int y);
void   wm_drag_rect(Rect *r);
bool_t wm_drag_bounds(Rect *oldr, Rect *newr);  /* old+new footprints, pre-release */
bool_t wm_release(void);
bool_t wm_dragging(void);

/* Button-held motion routed to the focused window's content (used by the
   Sketch Pad for free-hand drawing).  Returns TRUE if it painted, and fills
   *dirty with the small screen rectangle that changed so the caller can blit
   just that instead of repainting the whole scene. */
bool_t wm_content_drag(int x, int y, Rect *dirty);

/* Tick every applet that animates itself, calling present(kind) for each
   one that wants repainting.  Applets flagged AF_TICKTOP run only while
   focused (Serpent, Quadrix and the rest pause politely in the back). */
void   wm_tick_all(void (*present)(int kind));

/* Send a key to the focused window. Returns WM_NONE/WM_REDRAW/WM_LAUNCH. */
int    wm_key(int key);

/* Rename an open window of this kind - a document name in the title
   bar, for an applet with nowhere else to show one.  No-op if no such
   window is open. */
void   wm_set_title(int kind, const char *title);

/* TRUE once after a title actually changed: the frame needs repainting,
   and a content-only redraw will not do it. */
bool_t wm_poll_retitled(void);

#endif /* WINDOW_H */
