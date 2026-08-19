/* ======================================================================
 * music.h - Music Box applet for CASTALIA/386
 * ----------------------------------------------------------------------
 * Plays short classical tunes on the PC speaker (the 8253 timer + port
 * 61h), so it works on any DOS PC with no sound card.  Playback is
 * asynchronous: music_tick() advances one note at a time off the BIOS
 * clock from the main loop, so the desktop stays live while a tune plays.
 * ====================================================================== */
#ifndef MUSIC_H
#define MUSIC_H

#include "castalia.h"

void   music_open(void);                 /* reset selection, silence       */
void   music_draw(const Rect *client);
void   music_click(const Rect *client, int mx, int my);   /* play/stop     */
/* Arrows pick, Enter or Space plays the picked tune (or stops it), S
   stops.  TRUE = something changed, repaint. */
bool_t music_key(int key);
bool_t music_tick(void);                 /* advance playback; TRUE=repaint  */
bool_t music_is_playing(void);
void   music_stop(void);                 /* silence the speaker            */
void   music_chime(void);                /* short startup arpeggio         */

/* ---- Non-blocking sound effects (used by games) ---------------------
 * music_sfx() starts a brief tone and returns at once; music_sfx_service(),
 * called once per main-loop pass, silences it after its duration.  A sound
 * effect never interrupts a Music Box tune (it is simply suppressed while
 * one plays), and it does nothing unless sound effects are enabled. */
void   music_set_sfx(bool_t on);         /* enable/disable effects (INI)    */
void   music_sfx(unsigned freq, unsigned char dur_ticks);
void   music_sfx_service(void);          /* silence a finished effect       */

#endif /* MUSIC_H */
