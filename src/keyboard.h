/* ======================================================================
 * keyboard.h - Keyboard input for CASTALIA/386
 * ----------------------------------------------------------------------
 * Thin, non-blocking wrapper over the DOS/BIOS keyboard.  The event loop
 * polls kb_poll() once per iteration; it never blocks waiting for a key.
 *
 * Extended keys (arrows, function keys) come back from the BIOS as a
 * leading 0 byte followed by a scan code.  We fold those into a single
 * value 0x100 | scancode so callers can switch on one integer.
 * ====================================================================== */
#ifndef KEYBOARD_H
#define KEYBOARD_H

#define KEY_NONE   (-1)
#define KEY_ESC    27
#define KEY_ENTER  13
#define KEY_BACK   8
#define KEY_TAB    9
#define KEY_SPACE  32

/* Extended (non-ASCII) keys: 0x100 | BIOS scan code. */
#define KEY_UP     (0x100 | 72)
#define KEY_DOWN   (0x100 | 80)
#define KEY_LEFT   (0x100 | 75)
#define KEY_RIGHT  (0x100 | 77)
#define KEY_HOME   (0x100 | 71)
#define KEY_END    (0x100 | 79)
#define KEY_PGUP   (0x100 | 73)
#define KEY_PGDN   (0x100 | 81)
#define KEY_DEL    (0x100 | 83)
#define KEY_F1     (0x100 | 59)
#define KEY_F5     (0x100 | 63)    /* cascade the windows                   */
#define KEY_F6     (0x100 | 64)    /* tile the windows                      */
/* Scan code 0x0F with no ASCII is SHIFT+Tab (back-tab); DOS's BIOS
   reports Alt+Tab as 0xA5.  The window cycler answers to both, but the
   reliable one under DOSBox and on real hardware is Shift+Tab - the
   docs used to name only Alt+Tab, which is the one that often never
   arrives. */
#define KEY_BACKTAB (0x100 | 0x0F)   /* Shift+Tab                          */
#define KEY_ALTTAB  (0x100 | 0xA5)   /* Alt+Tab, where the BIOS reports it */
#define KEY_F2     (0x100 | 60)    /* F2: New Game / rename                 */
#define KEY_F3     (0x100 | 61)    /* F3: search again (Find File)          */
#define KEY_F4     (0x100 | 62)    /* F4: Find File -> show me the folder   */
#define KEY_F7     (0x100 | 65)    /* F7: new folder / new card             */
#define KEY_F8     (0x100 | 66)    /* F8: delete a RECORD, where Del already
                                      deletes a character                   */
#define KEY_F10    (0x100 | 68)    /* F10: open the Start menu (DOS habit)  */
#define KEY_CTRLESC (0x100 | 0x2E) /* Ctrl+Esc: same, where the BIOS reports it */
/* Ctrl+Home / Ctrl+End: top and bottom of a document.  The BIOS reports
   these as their own extended scan codes, distinct from plain Home/End,
   which is what lets an editor give the line ends and the document ends
   different keys. */
#define KEY_CTRL_HOME (0x100 | 0x77)
#define KEY_CTRL_END  (0x100 | 0x75)

/* Returns a key code, or KEY_NONE if no key is waiting. */
int kb_poll(void);

/* Discard any pending keystrokes (used after returning from a shell). */
void kb_flush(void);

#endif /* KEYBOARD_H */
