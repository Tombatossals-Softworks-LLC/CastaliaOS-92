/* ======================================================================
 * settings.h - The Settings panel for CASTALIA/386
 * ----------------------------------------------------------------------
 * Live configuration: click a theme and the whole desktop cross-fades
 * into it, switch the backdrop pattern, toggle animations and sound,
 * pick the screensaver delay - all applied instantly.  [Save to INI]
 * writes the choices back into CASTALIA.INI (preserving the user's
 * comments and layout), so they survive a reboot.
 * ====================================================================== */
#ifndef SETTINGS_H
#define SETTINGS_H

#include "castalia.h"
#include "ui.h"
#include "config.h"

void   settings_open(Config *cfg);     /* bind to the live configuration  */
void   settings_draw(const Rect *client);

/* Handle a click.  Returns TRUE when something GLOBAL changed (theme or
   backdrop) and the whole scene must repaint, FALSE for panel-only. */
bool_t settings_click(const Rect *client, int mx, int my);

/* Keyboard: Tab / Left / Right change page, Up / Down walk the page's
   controls, Enter or Space activates, S saves.  Without this the whole
   preferences panel was unreachable on a machine with no mouse driver. */
bool_t settings_key(int key);

/* Unsaved-preferences state.  Settings applies everything live, so the
   screen looks right while nothing has reached the INI - the window
   manager asks before closing on these, as it does for the Scrap Box
   and the Sketch Pad. */
bool_t settings_is_dirty(void);
void   settings_flush_state(void);

/* Pointer motion (screen coords): the Theme tab's live preview strip
   follows the swatch under the mouse.  TRUE = repaint wanted. */
bool_t settings_mouse(int mx, int my);

#endif /* SETTINGS_H */
