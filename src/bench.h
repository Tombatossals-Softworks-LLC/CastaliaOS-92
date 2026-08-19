/* ======================================================================
 * bench.h - Benchmark applet for CASTALIA/386
 * ----------------------------------------------------------------------
 * Six quick, honest micro-benchmarks timed off the BIOS tick - two integer
 * kernels, two memory kernels and two video kernels - each indexed against
 * a 386SX/16 and folded into one composite CASTALIA SCORE with a machine-
 * class verdict.  bench_run() does the timing (briefly freezing the
 * screen); bench_draw() shows the animated result bars.  bench_animating()
 * is TRUE while the bars are still charging, so the event loop only spends
 * repaints during the ~0.5 s the animation lasts.
 * ====================================================================== */
#ifndef BENCH_H
#define BENCH_H

#include "castalia.h"

void bench_run(void);                 /* run the tests (call on open)     */
void bench_draw(const Rect *client);
bool_t bench_key(int key);            /* Enter re-runs; returns TRUE then  */
bool_t bench_click(const Rect *cl, int mx, int my);  /* click re-runs      */
bool_t bench_animating(void);         /* TRUE while the bars charge up      */

#endif /* BENCH_H */
