/* ======================================================================
 * media.h - The Gramophone: a WAV / MIDI player for CASTALIA/386
 * ----------------------------------------------------------------------
 * Two eras of sound, one PC speaker.  A WAV file is played as digitised
 * audio through the speaker by pulse-width modulation (the RealSound
 * trick - no sound card needed), with a live OSCILLOSCOPE of the real
 * samples.  A Standard MIDI File is reduced to its top-voice melody and
 * played note by note on the timer, with a scrolling PIANO ROLL.
 *
 * The MIDI melody plays asynchronously off the BIOS tick (like the Music
 * Box), so the desktop stays live.  WAV playback is a short, bounded,
 * key-interruptible burst (digitised speaker audio must be tightly timed,
 * so it briefly owns the CPU) paced by the 8253 channel-0 counter, which
 * makes the pitch independent of how fast the 386 is.
 * ====================================================================== */
#ifndef MEDIA_H
#define MEDIA_H

#include "castalia.h"

bool_t media_open_file(const char *path);  /* load a .WAV or .MID          */
void   media_draw(const Rect *client);
bool_t media_click(const Rect *client, int mx, int my);
bool_t media_key(int key);

/* Advance the MIDI melody (called once per main-loop pass).  TRUE = the
   visualiser moved and the window should repaint.  fg says whether the
   player is the focused window: the ~18 Hz cosmetic animation (analyzer
   gravity, the LCD marquee crawl) runs only then; the music itself always
   advances, focused or not. */
bool_t media_tick(bool_t fg);
bool_t media_is_playing(void);
void   media_stop(void);

/* TRUE (once) after the Eject button was clicked: the main loop then pops
   the "Play..." dialog so a new file can be loaded from inside the player. */
bool_t media_poll_open(void);

/* ---- playlist --------------------------------------------------------
 * Point the Gramophone at a directory and every .WAV and .MID in it joins
 * a playlist shown in the window; Prev/Next walk it, a click plays a track,
 * and a MIDI auto-advances to the next when it ends.  media_add_folder is
 * called by the main loop after the "Add Folder" button pops its dialog. */
void   media_add_folder(const char *dir);

/* TRUE (once) after the [+ ADD FOLDER] button was clicked: the main loop
   then pops a folder-name dialog and calls media_add_folder(). */
bool_t media_poll_folder(void);

#endif /* MEDIA_H */
