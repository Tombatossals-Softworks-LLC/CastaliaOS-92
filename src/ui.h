/* ======================================================================
 * ui.h - Reusable widget drawing for CASTALIA/386
 * ----------------------------------------------------------------------
 * Small "immediate mode" helpers: every function just draws into the
 * video back buffer right now.  There is no widget tree and no retained
 * state here - higher layers (window.c, menu.c, desktop.c) own state and
 * call these to paint.  That keeps the UI tiny and predictable, which is
 * what a 386SX wants.
 *
 * Icons are drawn procedurally (with rectangles and lines) rather than
 * loaded from bitmap files, so the MVP has zero external art assets.
 * The ASSETS/ICONS directory is reserved for bitmap icons in a later
 * version (see ROADMAP.TXT).
 * ====================================================================== */
#ifndef UI_H
#define UI_H

#include "castalia.h"

/* Procedural icon identifiers (32x32). */
#define ICON_FOLDER    0   /* Disk Cabinet / a directory                 */
#define ICON_TERMINAL  1   /* Command Room                               */
#define ICON_SYSTEM    2   /* System Panel                               */
#define ICON_DRAWER    3   /* Program Drawer                             */
#define ICON_INFO      4   /* About Castalia                             */
#define ICON_FILE      5   /* a generic program / file                   */
#define ICON_DISK      6   /* a disk / drive                             */
#define ICON_NOTE      7   /* Scrap Box (ruled notepad)                  */
#define ICON_CALC      8   /* Calculator                                 */
#define ICON_CLOCK     9   /* Clock                                      */
#define ICON_PAINT    10   /* Sketch Pad (paint box)                     */
#define ICON_CHARS    11   /* Character Map                              */
#define ICON_MUSIC    12   /* Music Box (beamed notes)                   */
#define ICON_GAME     13   /* Fifteen Puzzle / Tic-Tac-Toe (die)         */
#define ICON_GAUGE    14   /* System Inspector (resource meter)          */
#define ICON_WATCH    15   /* Benchmark (stopwatch)                      */
#define ICON_EXIT     16   /* Exit to DOS (door + arrow)                 */
#define ICON_TOOLS    17   /* Toolbox group (tool case)                  */
#define ICON_ARCADE   18   /* Arcade group (joystick)                    */
#define ICON_DEMO     19   /* Light Show (starburst)                     */
#define ICON_COLORS   20   /* Colors / palette viewer (swatch quad)      */
#define ICON_EYES     21   /* Eyes desk toy (two googly eyes)            */
#define ICON_FIFTEEN  22   /* Fifteen Puzzle (tile grid with a gap)      */
#define ICON_TTT      23   /* Tic Tac Toe (X and O on the grid)          */
#define ICON_MINE     24   /* Minefield (spiked mine)                    */
#define ICON_REVERSI  25   /* Reversi (board with discs)                 */
#define ICON_SNAKE    26   /* Serpent (coiled snake + apple)             */
#define ICON_BREAKER  27   /* Breaker (bricks, ball, paddle)             */
#define ICON_ECHO     28   /* Echo (the four Simon quadrants)            */
#define ICON_QUADRIX  29   /* Quadrix (falling tetromino)                */
#define ICON_DEPOT    30   /* Depot (crate on its bay)                   */
#define ICON_PATIENCE 31   /* Patience (ace of spades over a card)       */
#define ICON_LIGHTS   32   /* Lights Out (3x3 lamp grid)                 */
#define ICON_FRACTAL  33   /* Fractal (the Mandelbrot silhouette)        */
#define ICON_CARDFILE 34   /* Cardfile (tabbed index cards)              */
#define ICON_BENCH    35   /* Benchmark (rising bar chart)               */
#define ICON_ORACLE   36   /* System Oracle (the all-seeing eye)         */
#define ICON_SETTINGS 37   /* Settings (slider panel)                    */
#define ICON_AGENDA   38   /* Agenda (clipboard with check marks)        */
#define ICON_PEEK     39   /* Hex Peek (magnifier over bytes)            */
#define ICON_MEDIA    40   /* Gramophone (a note over a sound wave)       */
#define ICON_CINEMA   41   /* Cinema (a film projector / reel of film)    */
#define ICON_COMPUTER 42   /* My Computer (a beige CRT + tower + keyboard) */
#define ICON_FIND     43   /* Find File (a magnifier over a folder)        */
#define ICON_PICTURE  44   /* Picture Show (a framed landscape)            */
#define ICON_CORRAL   45   /* Corral (a fence bisecting two balls)         */
#define ICON_TYPIST   46   /* Typing Tutor (keyboard with home-row keys)   */
#define ICON_PONG     47   /* Pong (two paddles and a ball)                */
#define ICON_2048     48   /* 2048 (four merging number tiles)             */
/* ICON_DISK is a 3.5" diskette, so My Computer captioned every drive
   "Local Disk" under a picture of a floppy - and A: looked exactly like
   C:.  These two are what a fixed and a network drive get instead. */
#define ICON_HDD      49   /* a fixed disk (drive body, face plate, LED)   */
#define ICON_NETDRV   50   /* a network drive (a fixed disk on a pipe)     */
#define ICON_CALENDAR 51   /* the Calendar - it had been sharing ICON_AGENDA */

/* Set / query the icon scale (1 = 32x32 for Mode 13h, 2 = 64x64 for 12h). */
void ui_set_scale(int s);
int  ui_scale(void);
int  ui_icon_size(void);
#define ICON_SIZE ui_icon_size()

/* ---- Geometry helpers (declared in castalia.h, defined here) --------- */
/* rect_set / rect_contains live in ui.c.                                 */

/* ---- 3D surfaces ----------------------------------------------------- */
/* Fill a rectangle with the control-face colour. */
void ui_fill_face(int x, int y, int w, int h);

/* Two-pixel classic raised bevel (white top/left, gray+dark bottom/right). */
void ui_raise(int x, int y, int w, int h);

/* Two-pixel classic sunken bevel (used for client areas and list boxes). */
void ui_sink(int x, int y, int w, int h);

/* A soft, 50%-dithered drop shadow down the right and bottom of a rect
   (offset down-right).  Draw it before the element's own frame so the
   shadow falls onto whatever is behind it. */
void ui_shadow(int x, int y, int w, int h);

/* ---- Widgets --------------------------------------------------------- */
/* A push button: face + raised/sunken bevel + centred label. When
   pressed the bevel inverts and the label nudges down-right by 1px. */
void ui_button(const Rect *r, const char *label, bool_t pressed);

/* Checkbox, radio button and etched group frame - the rest of a 1995
   control panel's vocabulary.  A pressed push button reads as "held
   down", not as "this is the setting".  ui_check_size() is the box side
   (tied to the font, so it scales with the video mode); pass NULL for
   the label to draw the box alone. */
/* Property-sheet tabs: the active one is taller and open at the bottom so
   it merges into the page, the way Windows drew them.  ui_tab_page_top()
   runs the page's top edge between the tabs, broken under the active one. */
void ui_tab(const Rect *r, const char *label, bool_t active);
void ui_tab_page_top(int x, int y, int w, const Rect *active);

int  ui_check_size(void);
void ui_checkbox(int x, int y, bool_t on, const char *label);
void ui_radio(int x, int y, bool_t on, const char *label);
void ui_groupbox(int x, int y, int w, int h, const char *title);

/* Draw a label centred horizontally within [x, x+w). */
void ui_text_center(int x, int y, int w, const char *s, u8 color);

/* Split an icon caption into two lines that each fit `w` pixels; a and b
   are caller buffers of `cap` bytes.  b comes back empty when one line was
   enough.  Shared by the desktop, the groups and the Program Drawer - they
   had three copies of this and all three orphaned the tail of a long word. */
/* w = the width to WRAP at (cell minus a gutter, so two captions never
   crowd); wmax = the width a single unbreakable word may use before it is
   split (the whole cell).  Splitting everything at w mangled one-word
   names; wrapping everything at wmax fused neighbouring ones. */
void ui_wrap2(const char *s, int w, int wmax, char *a, char *b, int cap);

/* The Windows dotted keyboard-focus rectangle. */
void ui_focus_rect(int x, int y, int w, int h);

/* A solid arrow head filling the button (not the letters "^" and "v"). */
void ui_arrow(const Rect *r, bool_t up);

/* The whole vertical scrollbar: arrows, checkered trough, and a thumb
   whose length is visible/total and whose position is top/(total-visible). */
void ui_vscroll(const Rect *up, const Rect *dn, const Rect *track,
                int top, int visible, int total);

/* ---- Icons ----------------------------------------------------------- */
/* Draw a 32x32 procedural icon with its top-left corner at (x,y). */
void ui_icon(int kind, int x, int y);

/* Map an icon kind from a config/command string ("fileman","sysinfo"...). */
int  ui_icon_for_command(const char *command);

/* The Castalia castle logo (branding), drawn at (x,y) with unit size u.
   Larger u makes the big animated logo in the boot splash; flag_up waves
   the pennant. */
void ui_castle(int x, int y, int u, bool_t flag_up);

/* The Start-button castle: a crisp, black-outlined pixel-art keep (13x10
   at s=1) drawn from a fixed grid, scaled by s.  Used for the taskbar's
   "Inicio" (Start) button and as every window's title-bar crest - well
   defined at any size. */
void ui_start_castle(int x, int y, int s);

#endif /* UI_H */
