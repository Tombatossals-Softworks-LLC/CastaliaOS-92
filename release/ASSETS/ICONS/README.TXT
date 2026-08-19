ASSETS/ICONS
============
Bitmap icons for the desktop (v0.4).

A desktop icon can be a 32x32 bitmap loaded from a .ICN file instead of
the built-in procedural shape.  Point at one from CASTALIA.INI:

    [desktop]
    icon1_name=World
    icon1_command=fileman
    icon1_icon=ASSETS\ICONS\GLOBE.ICN

If iconN_icon is absent (or the file fails to load) the matching built-in
procedural icon is drawn instead (see SRC/ui.c).

THE .ICN FORMAT
---------------
Plain text, trivial to author by hand or generate:

    ; an optional comment line (starts with ';')
    CICN 32 32
    <32 rows, each 32 characters>

Each character is a palette index and therefore recolours with the active
theme:

    0 1 2 3 4 5 6 7 8 9 A B C D E F   = colour slots (see SRC/video.h)
    .  or  space                      = transparent (background shows through)

      0 black    4 shadow    8 red       C dkgray
      1 title    5 hilight   9 yellow    D blue
      2 desktop  6 white     A dkyellow  E cyan
      3 face     7 ltblue    B green     F cream

Icons are stored at 32x32 and drawn at 1x in Mode 13h and 2x (64x64) in
Mode 12h, so one file serves both resolutions.

MAKING YOUR OWN
---------------
The Sketch Pad applet (Dominus menu, or command "paint") is a 32x32 pixel
editor: draw with the 16-colour palette and the transparent eraser, then
Save to a .ICN file here and point a desktop icon at it from CASTALIA.INI.

SAMPLES IN THIS DIRECTORY
-------------------------
    GLOBE.ICN ... a world / network globe
    PAINT.ICN ... an artist's palette
    CALC.ICN .... a calculator
    GAME.ICN .... a game controller

See DOCS/SCREENSHOTS/11-bitmap-icons-moncloa.png and 12-* for these in
use, and icons-preview.png for the four side by side.
