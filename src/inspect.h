/* ======================================================================
 * inspect.h - System Inspector applet for CASTALIA/386
 * ----------------------------------------------------------------------
 * The machine at a glance, the way a 386/486 owner wants it: the detected
 * processor and coprocessor, DOS version, video mode and mouse on a sunken
 * readout; a live green oscilloscope and an LED time-of-day clock; and
 * animated gauges for conventional, extended/XMS and disk space that charge
 * up when the window opens.  This is the merged System Panel + Inspector -
 * one window, everything about the box.  Read-only; it reprobes on open.
 * ====================================================================== */
#ifndef INSPECT_H
#define INSPECT_H

#include "castalia.h"

/* Reprobe the machine and (re)start the open animation.  Called when the
   System Inspector window is opened. */
void inspect_open(void);

/* Paint the inspector into its client area (animates off the BIOS tick). */
void inspect_draw(const Rect *client);

#endif /* INSPECT_H */
