/* ======================================================================
 * textscan.h - "does this file contain this text" for CASTALIA/386
 * ----------------------------------------------------------------------
 * What Find File uses for its Containing-text filter, and what a DOS box
 * otherwise has no grep to do.  Bytewise and case-insensitive: the point
 * is the CONFIG.SYS that mentions HIMEM or the save file with your name
 * in it, so nothing here assumes the file is text.
 *
 * Its own module because it is the one part of Find File that has no UI
 * in it, and because the chunk-boundary handling is the kind of thing
 * that breaks silently - tests/ compiles this file unmodified.
 * ====================================================================== */
#ifndef TEXTSCAN_H
#define TEXTSCAN_H

#include "castalia.h"

/* Copy `src` into `dst` uppercased, at most cap-1 characters plus a NUL.
   Returns the length written - which is what text_in_file wants as
   `nlen`, so the two are always in step. */
int text_needle(char *dst, int cap, const char *src);

/* TRUE when `path` contains `needle`, which must ALREADY be uppercased
   by text_needle - the comparison folds the file's bytes, not the
   pattern's, because the file is millions of bytes and the pattern is
   one.  An nlen of 0 matches everything (no filter); a file that cannot
   be opened matches nothing. */
bool_t text_in_file(const char *path, const char *needle, int nlen);

#endif /* TEXTSCAN_H */
