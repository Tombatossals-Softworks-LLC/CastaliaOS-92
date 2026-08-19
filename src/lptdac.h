/* ======================================================================
 * lptdac.h - Parallel-port DACs for CASTALIA/386: Covox & Sound Source
 * ----------------------------------------------------------------------
 * Two beloved printer-port audio devices of the early 90s:
 *
 *   COVOX SPEECH THING - a resistor-ladder DAC hanging off the LPT data
 *   lines.  No handshake, no FIFO, no way to detect it: software simply
 *   writes one unsigned 8-bit sample after another to the data port at
 *   the playback rate.  Clones were legion (and a soldering iron and
 *   eighteen resistors made you one in an afternoon).
 *
 *   DISNEY SOUND SOURCE - the little grey box with the 16-byte FIFO that
 *   drains at ~7 kHz.  Samples are clocked in by pulsing control bit 3,
 *   and the FIFO-full flag (status bit 6) paces the stream, exactly the
 *   protocol the id Software classics drove it with.
 *
 * Neither device can be safely auto-probed (poking bytes at a printer
 * prints garbage), so - like the BLASTER variable of the era - the user
 * declares the device in CASTALIA.INI:  [system] lptdac= / lptport=.
 * ====================================================================== */
#ifndef LPTDAC_H
#define LPTDAC_H

#include "castalia.h"

/* Bind the driver from the INI: mode "covox" or "dss" (anything else =
   off), lpt 1..3.  The port base comes from the BIOS data area, so a
   card remapped by the BIOS is found where it really lives. */
void        lptdac_config(const char *mode, int lpt);

bool_t      lptdac_present(void);
unsigned    lptdac_max_rate(void);   /* 0 = uncapped (Covox); 7000 = DSS  */
const char *lptdac_name(void);       /* "Covox LPT1" / "Snd Source LPT1"  */

/* Play n unsigned 8-bit mono samples at rate Hz.  A bounded, blocking,
   key-interruptible burst like the PC-speaker path (digitised audio owns
   the CPU briefly; the PIT paces Covox, the FIFO paces the DSS).
   Returns FALSE when no LPT DAC is configured. */
bool_t      lptdac_play(const u8 far *s, int n, unsigned rate);

#endif /* LPTDAC_H */
