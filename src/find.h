/* ======================================================================
 * find.h - Find File (a Win95-style file search) for Castalia 92
 * ----------------------------------------------------------------------
 * Walks every directory of the current drive matching an 8.3 wildcard
 * pattern (*.TXT, CONFIG.*, ...) and lists what it finds: name, size and
 * the folder it lives in.  The walk is breadth-first with a single
 * find_t active at any moment, so it never fights DOS over the DTA.
 * ====================================================================== */
#ifndef FIND_H
#define FIND_H

#include "castalia.h"

void   find_open(void);
void   find_draw(const Rect *cl);
bool_t find_click(const Rect *cl, int mx, int my);
bool_t find_key(int key);

/* TRUE once after the user asked for a new search (button / ENTER); the
   main loop pops the modal pattern dialog and calls find_run.  The same
   poll pattern the Gramophone uses for its Eject dialog. */
bool_t find_poll_prompt(void);

/* The same, for the "Text..." button and F2: the caller prompts for the
   text a matching file must CONTAIN and re-runs with the pattern
   unchanged.  find_text() is what to seed that dialog with, so an empty
   answer clears the filter and Enter on the old one keeps it. */
bool_t      find_poll_tprompt(void);
const char *find_text(void);
const char *find_pattern(void);

/* TRUE once when the user pressed Enter (or clicked twice) on a result:
   *dir and *name are its folder and 8.3 name, for the caller to open
   through the same association route the Disk Cabinet uses. */
bool_t find_poll_open(char *dir, int dcap, char *name, int ncap);

/* Sweep the drive.  `text` is optional: NULL or "" lists every file whose
   NAME matches, anything else additionally opens each of those files and
   keeps only the ones containing that text, case-insensitively. */
void   find_run(const char *pattern, const char *text);

/* F4 on a result: the caller should show that FOLDER in the Disk
   Cabinet.  TRUE once per press, with the folder in `dir`. */
bool_t find_poll_goto(char *dir, int dcap);

#endif /* FIND_H */
