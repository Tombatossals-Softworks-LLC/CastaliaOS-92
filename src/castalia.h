/* ======================================================================
 * castalia.h - Shared types, constants and identity for CASTALIA/386
 * ----------------------------------------------------------------------
 * CASTALIA/386 - A graphical operating environment for DOS-compatible
 *                386 systems.
 *                (C) Tombatossals Softworks.
 *
 * This header is intentionally tiny.  It only holds the things that
 * EVERY module needs: the boolean type that C89 lacks, a couple of
 * fixed-width typedefs, the Rect helper, and the product identity
 * strings used by the splash screen and the About box.
 *
 * Design rule for the whole project: C89/C90 only.  No C++ line
 * comments; declarations at the top of their block; no variable
 * length arrays, no compound literals.  The code targets Open Watcom
 * C 16-bit, medium memory model, real mode, 386 instruction set.
 * ====================================================================== */
#ifndef CASTALIA_H
#define CASTALIA_H

/* ---- Product identity (no Microsoft trademarks are used anywhere) ---- */
#define CAST_NAME     "Castalia 92"
#define CAST_VERSION  "0.56"
#define CAST_TAGLINE  "Graphical Environment for DOS"
#define CAST_COMPANY  "Tombatossals Softworks"

/* Minimum system RAM (conventional + extended), in KB.  1 MB is the
   baseline the project targets - the same ceiling an Amiga 500 reached
   with the classic 512 KB "slow" expansion. */
#define CAST_MIN_RAM_KB 1024

/* ---- Fixed width helper types ---------------------------------------- */
typedef unsigned char  u8;    /* 8 bit                                    */
typedef unsigned short u16;   /* 16 bit                                   */
typedef short          i16;   /* signed 16 bit                            */
typedef unsigned long  u32;   /* 32 bit                                   */

/* ---- Boolean (C89 has no <stdbool.h>) -------------------------------- */
#ifndef TRUE
#define TRUE  1
#endif
#ifndef FALSE
#define FALSE 0
#endif
typedef int bool_t;

#ifndef NULL
#define NULL ((void *)0)
#endif

/* ---- A simple axis aligned rectangle (x,y = top left corner) --------- */
typedef struct {
    int x;
    int y;
    int w;
    int h;
} Rect;

/* Implemented in ui.c (kept with the other geometry helpers). */
void     rect_set(Rect *r, int x, int y, int w, int h);
bool_t   rect_contains(const Rect *r, int px, int py);

/* Clamp helper used in several modules. */
#define CAST_MIN(a,b) ((a) < (b) ? (a) : (b))
#define CAST_MAX(a,b) ((a) > (b) ? (a) : (b))

#endif /* CASTALIA_H */
