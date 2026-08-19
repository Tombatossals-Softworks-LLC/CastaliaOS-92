/* ======================================================================
 * oracle.h - The System Oracle for CASTALIA/386
 * ----------------------------------------------------------------------
 * A full hardware / software profiler in the spirit of the great DOS
 * diagnostic suites (Norton SI, CheckIt, MSD) - and of what AIDA would
 * have looked like had it shipped in 1991.  A sidebar of categories, a
 * detail pane of probed facts, and a benchmark page that races THIS
 * machine against the classic reference PCs with big horizontal bars.
 *
 * Everything is probed from the metal: CPU class via the EFLAGS
 * AC/ID-bit tests and CPUID where available, FPU via FNSTSW, memory
 * via INT 12h/15h and the XMS/EMS drivers, VESA via INT 10h 4Fh, disk
 * geometry via INT 13h, ports from the BIOS data area, the BIOS date
 * from F000:FFF5.  Cheap probes run once at open; the CPU speed index
 * is measured on first view of the CPU page; the benchmark runs only
 * when asked (it deliberately burns a second of CPU).
 * ====================================================================== */
#ifndef ORACLE_H
#define ORACLE_H

#include "castalia.h"

void   oracle_open(void);                /* probe + reset to Overview      */
void   oracle_draw(const Rect *client);

/* Click / key.  TRUE = state changed, repaint the window. */
bool_t oracle_click(const Rect *client, int mx, int my);
bool_t oracle_key(int key);

/* TRUE once after a benchmark run: its video sub-tests paint in absolute
   screen coordinates, so the WHOLE scene has to be recomposed, not just
   the Oracle's own window.  Reading it clears it. */
bool_t oracle_poll_damage(void);

#endif /* ORACLE_H */
