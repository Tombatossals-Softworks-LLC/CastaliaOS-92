/* ======================================================================
 * typist.h - Typing Tutor for CASTALIA/386
 * ----------------------------------------------------------------------
 * Strict-entry typing drills with live words-per-minute, an accuracy
 * count and a persistent best (CASTALIA.HI key "typist").  One drill
 * line at a time; Enter restarts the line, Tab skips to the next.
 * ====================================================================== */
#ifndef TYPIST_H
#define TYPIST_H

#include "castalia.h"

void   typist_open(void);

/* Redraw the live WPM meter about twice a second while typing. */
bool_t typist_tick(void);

void   typist_draw(const Rect *cl);

/* Feed a key; TRUE when the display changed. */
bool_t typist_key(int key);

#endif /* TYPIST_H */
