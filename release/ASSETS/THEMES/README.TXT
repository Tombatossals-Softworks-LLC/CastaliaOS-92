ASSETS/THEMES
=============
In v0.1 themes are built-in 16-colour palettes defined in SRC/video.c
(the pal_* tables) and selected by name in CASTALIA.INI:

    [system]
    theme=classic        ; classic | penumbra | bureau | winsteel

Available themes:

    classic ... the default - teal desktop, navy title bars, light-gray
                3D controls, white client areas.  The mid-1990s look.
    penumbra .. a darker, dusk palette (slate desktop, cool grays).
    bureau .... "Bureau Beige": a warm office beige scheme.
    winsteel .. a cool blue-gray "steel" scheme.
    moncloa ... "Moncloa 92": a warm institutional scheme - sandy desktop,
                maroon title bars, warm grays.  An early-90s Iberian look.
    workbench . a homage to the 1.x Amiga desktop: blue backdrop, gray
                gadgets, white/orange accents.  The look this project salutes.
    ocean ..... a cool deep-sea scheme: teal-blue desktop, cool grays.
    rose ...... a warm, elegant mauve/rose scheme.
    midnight .. a deep indigo/violet night: near-black desktop, royal violet
                title bars, electric-periwinkle accents.
    amber ..... a warm sunset/amber-phosphor desktop: espresso backdrop,
                burnt-amber title bars, ember accents.  A CRT at dusk.
    matrix .... a green-phosphor terminal made desktop: near-black green
                backdrop, vivid phosphor accents.

PER-ELEMENT OVERRIDES (v0.4)
----------------------------
Any individual colour slot can be overridden from CASTALIA.INI on top of
the chosen theme, without recompiling:

    [colors]
    desktop=0,128,128      ; just the background
    title=64,0,96          ; just the title bars

Slot names: black title desktop face shadow hilight white ltblue red
yellow dkyellow green dkgray blue cyan cream.

Each palette assigns RGB values to the 16 logical colour slots that the
UI uses (C_FACE, C_TITLE, C_DESKTOP, ...); video_set_theme() loads them
into the VGA DAC at start-up and again after returning from a launched
program (a video-mode switch resets the DAC).

This directory is reserved for EXTERNAL theme files (plain-text palette
definitions loadable without recompiling) in a future version - see
ROADMAP.TXT, v0.4.  The names "Moncloa 92" and additional schemes are
planned there.
