/* ======================================================================
 * colors.h - Palette viewer utility for CASTALIA/386
 * ----------------------------------------------------------------------
 * A read-only swatch board of the sixteen theme colours (the DAC slots the
 * whole UI is painted from), labelled by role, with the active theme name.
 * Handy for seeing a theme at a glance and for choosing [colors] overrides
 * in CASTALIA.INI.
 * ====================================================================== */
#ifndef COLORS_H
#define COLORS_H

#include "castalia.h"
#include "ui.h"

void colors_open(const char *theme);
void colors_draw(const Rect *client);

#endif /* COLORS_H */
