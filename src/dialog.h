/* ======================================================================
 * dialog.h - Modal dialogs for CASTALIA/386 (Phase 2)
 * ----------------------------------------------------------------------
 * Three small modal dialogs used by the Disk Cabinet's file operations:
 * a message box (OK), a confirmation (Yes/No) and a single-line text
 * input (OK/Cancel).
 *
 * They are SYNCHRONOUS: each call runs its own little event loop, drawing
 * itself over the frozen scene (which still lives in the video back
 * buffer) until the user answers, then returns the result.  That makes
 * them trivial to use from anywhere -
 *
 *     if (dialog_confirm("Delete", "Delete README.TXT?"))
 *         remove(...);
 *
 * - without threading any state through the main event loop.  The caller
 * should mark the scene dirty afterwards so the dialog is painted over on
 * the next frame.
 * ====================================================================== */
#ifndef DIALOG_H
#define DIALOG_H

#include "castalia.h"

#define DLG_CANCEL 0
#define DLG_OK     1
#define DLG_NO     0
#define DLG_YES    1

/* Information box with a single OK button. Always returns DLG_OK. */
/* TRUE once if any modal dialog ran since the last query (the caller
   must then repaint the whole scene, not just the clicked window). */
bool_t dialog_took_over(void);

/* Declare a takeover from another modal that paints over the whole scene
   (the file picker), so the caller repaints fully rather than partially. */
void   dialog_note_takeover(void);

int dialog_message(const char *title, const char *line1, const char *line2);

/* Yes/No question. Returns DLG_YES or DLG_NO (ESC = No). */
int dialog_confirm(const char *title, const char *line1, const char *line2);

/* Single-line text entry. buf holds the initial text and receives the
   result; bufsize is its capacity. Returns DLG_OK or DLG_CANCEL
   (ESC = Cancel, Enter = OK). */
int dialog_input(const char *title, const char *prompt,
                 char *buf, int bufsize);

#endif /* DIALOG_H */
