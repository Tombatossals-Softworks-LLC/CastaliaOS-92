/* ======================================================================
 * sblaster.h - Sound Blaster digital audio for CASTALIA/386
 * ----------------------------------------------------------------------
 * Real DAC playback of 8-bit PCM through a Sound Blaster (or SB Pro / SB16
 * / any DSP-compatible card), so the Gramophone can play a WAV as genuine
 * digital sound instead of pulse-width modulation on the PC speaker.
 *
 * It is deliberately small and IRQ-free: a sample plays as one single-cycle
 * 8-bit DMA transfer (blocks up to 64 KB), the card's interrupt line is
 * masked at the PIC for the duration, and completion is timed/polled - so
 * there is no interrupt handler to install and nothing to leak on exit.
 * ====================================================================== */
#ifndef SBLASTER_H
#define SBLASTER_H

#include "castalia.h"

/* Probe for a Sound Blaster: read the BLASTER environment variable, else
   try the usual base ports, resetting the DSP and reading its version.
   Caches the result; call it freely.  Returns TRUE if a card answered. */
bool_t      sb_present(void);

/* Human-readable card name ("Sound Blaster 16", ...) or "" if none. */
const char *sb_name(void);

/* The detected resources (valid only when sb_present() is TRUE). */
int  sb_base(void);       /* base I/O port, e.g. 0x220                     */
int  sb_irq(void);        /* interrupt line                                */
int  sb_dma(void);        /* 8-bit DMA channel                             */

/* Play `nsamp` unsigned-8-bit mono samples (128 = silence) at `rate` Hz
   from the far buffer `samples`.  Blocking, but polls the keyboard so any
   key stops it early (the key is consumed).  Returns FALSE if there is no
   card or the transfer could not be set up (caller then falls back to the
   PC speaker). */
bool_t      sb_play_8bit(const unsigned char far *samples,
                         unsigned nsamp, unsigned rate);

/* Free the DMA buffer (called when the Gramophone closes). */
void        sb_release(void);

#endif /* SBLASTER_H */
