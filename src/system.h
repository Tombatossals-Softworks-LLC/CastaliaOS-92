/* ======================================================================
 * system.h - Machine / DOS information for the System Panel
 * ----------------------------------------------------------------------
 * Gathers a handful of facts that are reliably obtainable in real mode
 * without any assembly: DOS version (INT 21h/30h), conventional memory
 * (INT 12h), largest free block (INT 21h/48h), extended memory
 * (INT 15h/88h) and coprocessor presence (INT 11h).
 *
 * Note on the processor line: distinguishing 386 / 486 / Pentium at run
 * time needs 32-bit EFLAGS probing in assembly, which is deliberately
 * out of scope for the MVP (see ROADMAP.TXT).  We therefore report the
 * configured target class rather than guessing.  Honesty over a fake
 * readout.
 * ====================================================================== */
#ifndef SYSTEM_H
#define SYSTEM_H

#include <stdio.h>     /* FILE (the safe-save helpers)                   */
#include "castalia.h"

/* Install a DOS critical-error (INT 24h) handler that fails the call
   instead of letting DOS print "Abort, Retry, Fail" or hang the GUI when
   a drive is not ready (e.g. an empty floppy). Call once at start-up. */
void        crit_error_install(void);

/* Make Ctrl+C / Ctrl+Break harmless (an INT 23h that resumes the call)
   instead of DOS's default of killing the shell in graphics mode.  Call
   once at start-up; DOS restores the vector itself at exit. */
void        ctrl_break_install(void);

/* The home (start-up) directory.  The Disk Cabinet chdir()s as the user
   browses, so data files opened by bare name would land wherever the
   user is standing; capture once in main() from argv[0] (the
   folder the EXE was loaded from), then anchor names with sys_home_path
   (out gets "C:\CASTALIA\NAME"; cap includes the NUL). */
void        sys_capture_home(const char *argv0);
const char *sys_home(void);
void        sys_home_path(char *out, int cap, const char *name);

/* Safe saves: write to sys_temp_name(path) ("NOTES.TXT" -> "NOTES.TM$",
   same folder), then sys_commit_file() flushes, DOS-commits and closes
   it and swaps it in for `path`.  FALSE means the new file did not make
   it; unless the final rename failed, the original is untouched. */
void        sys_temp_name(char *out, int cap, const char *path);
bool_t      sys_commit_file(FILE *f, const char *tmp, const char *path);

/* The BIOS 18.2 Hz tick counter, read straight from the BIOS data area
   (0040:006C) with a torn-read guard.  This replaces the old per-module
   int86(0x1A) helpers: no software-interrupt round trip, no REGS copying,
   and it is called several times per event-loop pass. */
unsigned long sys_ticks(void);

/* Halt the CPU until the next hardware interrupt (timer tick, keystroke,
   mouse motion).  The event loop calls this whenever a pass ends with
   nothing dirty, so an idle desktop uses (almost) no CPU - kind to
   emulators, laptops and the power bill - while still waking instantly
   on input and never missing a BIOS-tick-paced animation.  Defined as an
   in-line intrinsic (sti first, so a pending-interrupt wake is certain);
   the pragma lives here so every caller expands it in place. */
void        sys_idle(void);
#pragma aux sys_idle = \
    "sti"              \
    "hlt";

/* CPU-speed-independent sample pacing off PIT channel 0 (1193180 Hz
   input clock).  sys_pit_start() opens a timeline; sys_pit_until(t) spins
   until t input clocks have passed since then.  Deadlines are absolute,
   so per-sample work between waits does not stretch the period.  Poll
   at least every ~25 ms (the counter wraps). */
void        sys_pit_start(void);
void        sys_pit_until(unsigned long clocks);

/* Total system RAM in KB: conventional (INT 12h) + the extended memory the
   BIOS counted at POST (CMOS 30h/31h - never the FREE XMS figure, which is
   all the HIMEM-era probes return).  On 386-class machines the 384 KB
   above 640 KB is reported as extended, so a 1 MB board totals ~1024 KB.
   Used by the start-up memory gate, the splash and the Inspector.
   (unsigned long: a machine with >64 MB overflows a 16-bit sum.) */
unsigned long system_total_ram_kb(void);

/* The detected CPU as a bare 80x86 name ("80386", "80486", "80586-class",
   "80686-class").  Used by the System Panel and the boot splash. */
const char *system_cpu_name(void);

/* Individual memory / disk probes used by the System Inspector. */
unsigned    system_conventional_kb(void);
unsigned    system_extended_kb(void);      /* FREE XMS (or bare INT 15h) */
unsigned    system_ext_installed_kb(void); /* extended RAM fitted (CMOS)  */
unsigned    system_free_conv_kb(void);
bool_t      system_fpu_present(void);
void        system_disk_kb(unsigned long *freek, unsigned long *totalk);

/* Tell the module whether a mouse was detected (main knows this). */
void        system_set_mouse(bool_t present);

/* Query the mouse-detected flag (used by the System Inspector). */
bool_t      system_has_mouse(void);

/* Build the information lines. Call once after detection is complete. */
void        system_gather(void);

/* Access the gathered lines for rendering. */
int         system_line_count(void);
const char *system_line(int i);

#endif /* SYSTEM_H */
