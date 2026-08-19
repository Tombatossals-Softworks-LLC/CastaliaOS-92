/* ======================================================================
 * opl.h - OPL2 (Yamaha YM3812 / AdLib) FM music synthesis for CASTALIA/386
 * ----------------------------------------------------------------------
 * The FM chip on every AdLib and Sound Blaster.  It gives the Gramophone
 * real, polyphonic, instrument-timbred music for a MIDI file instead of a
 * one-voice square wave on the PC speaker.  Nine voices; everything is
 * register pokes to ports 388h/389h, no interrupts and no DMA, so it is
 * cheap and 386SX-friendly.
 * ====================================================================== */
#ifndef OPL_H
#define OPL_H

#include "castalia.h"

#define OPL_VOICES  9      /* channels the chip has                         */
#define OPL_MELODIC 6      /* melodic voices while rhythm mode owns 6..8     */

/* Percussion bits for opl_drums(): OR them together. */
#define OPL_BD 0x10        /* bass drum   */
#define OPL_SD 0x08        /* snare       */
#define OPL_TT 0x04        /* tom         */
#define OPL_TC 0x02        /* cymbal      */
#define OPL_HH 0x01        /* hi-hat      */

/* Detect the chip (the AdLib timer handshake).  Caches the result. */
bool_t opl_present(void);

/* Silence and program the chip with the built-in instrument, ready to
   play.  Safe to call repeatedly. */
void   opl_init(void);

/* Point a voice at the FM patch for General-MIDI program `gmprog` (0..127).
   Only rewrites the chip registers when the patch actually changes, so it
   is cheap to call before every note. */
void   opl_program(int voice, int gmprog);

/* Sound / release MIDI note `midinote` (0..127) on voice 0..OPL_MELODIC-1. */
void   opl_note_on(int voice, int midinote);
void   opl_note_off(int voice);

/* Strike percussion: `bits` is any OR of OPL_BD/SD/TT/TC/HH.  Rhythm mode
   turns channels 6..8 into these five voices, so only 0..5 are melodic. */
void   opl_drums(int bits);

/* Key-off every melodic voice and clear the drums. */
void   opl_silence(void);

#endif /* OPL_H */
