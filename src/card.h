/* ======================================================================
 * card.h - Cardfile (an index-card notepad) for CASTALIA/386
 * ----------------------------------------------------------------------
 * A little stack of index cards, each a few lines of free text.  Flip with
 * the < and > buttons, start a fresh card with New, drop the current one
 * with Del; the first line is the card's index.  Cards live in memory for
 * the session.  Type to edit the current card.
 * ====================================================================== */
#ifndef CARD_H
#define CARD_H

#include "castalia.h"
#include "ui.h"

void   card_open(void);
void   card_draw(const Rect *client);
bool_t card_click(const Rect *client, int mx, int my);
bool_t card_key(int key);

/* Commit any pending edits to CARDFILE.DAT.  Called when the user
   leaves a card and when the Cardfile window closes. */
void card_flush(void);

/* TRUE when the deck in RAM is newer than CARDFILE.DAT.  A flush can
   decline for three reasons - the file held more cards than the table
   (writing back would delete the overflow), the file could not be
   opened, or the write failed part way - and all three leave this set.
   The quit path asks on it, so "the deck did not save" cannot be the
   thing the user finds out tomorrow. */
bool_t card_is_dirty(void);

#endif /* CARD_H */
