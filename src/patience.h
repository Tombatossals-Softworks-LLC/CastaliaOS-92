/* ======================================================================
 * patience.h - Patience (klondike solitaire) for CASTALIA/386
 * ----------------------------------------------------------------------
 * The desk game every graphical environment is measured by.  Classic
 * klondike: seven tableau columns, draw-one stock with endless redeals,
 * four foundations.  Click a card (or run) to pick it up, click where it
 * should go; double-click sends a card to its foundation; N deals again.
 * Winning earns the traditional cascade of bouncing cards.
 * ====================================================================== */
#ifndef PATIENCE_H
#define PATIENCE_H

#include "castalia.h"
#include "ui.h"

void   patience_open(void);
void   patience_draw(const Rect *client);
void   patience_click(const Rect *client, int mx, int my, bool_t dbl);
bool_t patience_key(int key);          /* N = new deal; TRUE = repaint    */
bool_t patience_tick(void);            /* the win cascade; TRUE = repaint */

#endif /* PATIENCE_H */
