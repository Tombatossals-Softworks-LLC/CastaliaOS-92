/* ======================================================================
 * recent.h - the Documents menu's recently-opened list for Castalia 92
 * ----------------------------------------------------------------------
 * Windows 95 put the last handful of opened documents under Start >
 * Documents, and it is the single most-used shortcut in that shell: the
 * file you want next is nearly always one you had open recently.  This
 * shell routed every document open through one function already
 * (main.c's open_document), so there is exactly one place to notice
 * them.
 *
 * The list is remembered as CfgShortcut records so the menu can carry
 * them beside the pinned [shortcut] entries with no second code path -
 * `command` holds the 8.3 name and `path` the folder it lives in, which
 * is precisely what open_document takes.  It persists to RECENT.TXT
 * beside CASTALIA.EXE, the way AGENDA.TXT and CARDFILE.DAT do, rather
 * than into CASTALIA.INI: the INI writer round-trips a fixed set of
 * known keys, and a churning list does not belong in a file the user
 * hand-edits.
 * ====================================================================== */
#ifndef RECENT_H
#define RECENT_H

#include "castalia.h"
#include "config.h"

#define RECENT_MAX 6

/* Load / save RECENT.TXT.  Both are quiet on failure: a missing or
   unwritable list is a lost convenience, not an error worth a dialog. */
void recent_load(void);
void recent_save(void);

/* Note that (dir, name) was just opened.  Moves it to the top if it is
   already listed, so the order is genuinely most-recent-first. */
void recent_note(const char *dir, const char *name);

/* The list, newest first.  recent_name() is the label the menu draws;
   recent_fill() copies entry i into a caller's CfgShortcut when it is
   actually picked.

   The store itself is COMPACT and FAR: six full CfgShortcut records is
   1.1 KB, and holding them in near memory pushed DGROUP from 89% to 91%
   for a convenience feature.  A recent document needs a name and a
   folder, not the icon and freemem fields a launcher entry carries. */
int         recent_count(void);
const char *recent_name(int i);
void        recent_fill(int i, CfgShortcut *out);

/* Forget everything (the "Clear the list" entry at the foot of the menu). */
void recent_clear(void);

#endif /* RECENT_H */
