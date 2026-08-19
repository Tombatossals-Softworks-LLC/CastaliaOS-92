/* ======================================================================
 * font.h - Bitmap text rendering for CASTALIA/386
 * ----------------------------------------------------------------------
 * A single fixed 8x8 cell font is embedded in font.c.  Glyphs are drawn
 * 5 pixels wide inside the 8-pixel cell and advanced 6 pixels, giving a
 * clean 1-pixel gap between characters.  Embedding the font (rather than
 * borrowing the VGA ROM font) keeps the build self-contained, removes a
 * BIOS dependency, and makes the typeface part of Castalia's own visual
 * identity.
 * ====================================================================== */
#ifndef FONT_H
#define FONT_H

#include "castalia.h"

/* The font is now mode-aware (see font.c):
     normal  - the embedded 8x8 face (advance 6) for Mode 13h
     big     - the VGA ROM 8x16 face (advance 8) for Mode 12h, or, if the
               ROM font cannot be fetched, the 8x8 face drawn at 2x.
   font_h()/font_adv() report the ACTIVE metrics; the layout code derives
   its sizes from them, so the same UI fits either mode. */

/* Fetch the ROM 8x16 font once at start-up. */
void font_init(void);

/* Select the big (Mode 12h) or normal (Mode 13h) face. */
void font_set_big(bool_t big);

/* Active glyph height and per-character advance, in pixels.
   The layout code reads these constantly (often once per list row), and in
   the medium memory model a cross-module call is a FAR call - so the two
   metrics are exported as globals and the names below resolve to a plain
   memory read.  The functions remain for anyone needing their address;
   only font_set_big() ever writes the globals. */
extern int castalia_font_adv;
extern int castalia_font_h;

int  font_h(void);
int  font_adv(void);

#define font_h()   (castalia_font_h)
#define font_adv() (castalia_font_adv)

/* Back-compat names used throughout the layout code (now runtime). */
#define FONT_H_PX  font_h()
#define FONT_ADV   font_adv()

/* Pixel width that a string will occupy when drawn. */
int  font_text_width(const char *s);

/* Draw one character. Characters outside 0x20..0x7F render as a box. */
void font_draw_char(int x, int y, char c, u8 color);

/* Draw a NUL-terminated string at (x,y) in the given colour. */
void font_draw(int x, int y, const char *s, u8 color);

/* Draw at most maxchars characters of s (used for truncating long names).
   Returns the number of characters actually drawn. */
int  font_draw_n(int x, int y, const char *s, int maxchars, u8 color);

/* Raw access to the active face's glyph bitmap (8 columns per row, MSB
   left). *rows receives the height (8 or 16). Used by the boot splash to
   render big gradient-filled logo text. */
const u8 far *font_glyph(int ch, int *rows);

#endif /* FONT_H */
