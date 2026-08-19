/* ======================================================================
 * hiscore.h - Persistent high scores for CASTALIA/386
 * ----------------------------------------------------------------------
 * A tiny key/value store in CASTALIA.HI (one "game score" line each), so
 * a best score survives across sessions.  Loaded lazily on first use and
 * rewritten whenever a record is beaten; a missing or unwritable file is
 * simply treated as "no records yet" - the games never fail on it.
 * ====================================================================== */
#ifndef HISCORE_H
#define HISCORE_H

#include "castalia.h"

/* The stored best for `game` (a short lowercase key), or 0 if none. */
long   hiscore_best(const char *game);

/* Record `score` for `game` if it beats the stored best.  Returns TRUE
   when a new record was set (and written to disk). */
bool_t hiscore_submit(const char *game, long score);

#endif /* HISCORE_H */
