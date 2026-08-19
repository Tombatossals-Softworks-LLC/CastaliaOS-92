/* ======================================================================
 * tests/host.h - shim that lets the DOS parsers build on a host compiler
 * ----------------------------------------------------------------------
 * The file-format parsers (GIF, ICO/ICN, INI) take fully hostile input:
 * a GIF or .ICO named by CASTALIA.INI or picked in the Sketch Pad's Load
 * box, an INI written by hand.  Three memory-safety bugs have been found
 * in exactly these three modules, so they are worth running under a
 * sanitizer - which means building them somewhere that has one.
 *
 * Everything DOS-specific is either erased (`far`, the near/far pointer
 * distinction) or replaced with a malloc-backed stand-in (_dos_allocmem).
 * The parser sources themselves are compiled UNMODIFIED.
 * ==================================================================== */
#ifndef CAST_TEST_HOST_H
#define CAST_TEST_HOST_H

#include <stdlib.h>
#include <string.h>

/* 16-bit real mode has no flat pointers; the host does. */
#define far
#define _fmemcpy  memcpy
#define _fmemset  memset
#define _fmemcmp  memcmp

/* One live block is all any parser holds at a time, but keep a small
   table so a leak or a double free shows up as a test failure. */
#define HOST_BLOCKS 8
extern void *host_block[HOST_BLOCKS];

int  host_allocmem(unsigned paras, unsigned *seg);
int  host_freemem(unsigned seg);
void host_reset_blocks(void);
int  host_live_blocks(void);

#define _dos_allocmem(p, s) host_allocmem((p), (s))
#define _dos_freemem(s)     host_freemem((s))
#define MK_FP(seg, off)     ((void *)((char *)host_block[(seg)] + (off)))
#define FP_SEG(p)           (0)
#define FP_OFF(p)           (0)

#endif /* CAST_TEST_HOST_H */
