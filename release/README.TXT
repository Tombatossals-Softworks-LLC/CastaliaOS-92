======================================================================
 CASTALIA 92
 A graphical operating environment for DOS-compatible 386 systems.
 (C) Tombatossals Softworks.  MIT License.  Version 0.56
======================================================================

This file opens with the reference you need to get running, and closes
with the release history.  If you are installing for the first time,
read INSTALLING and HOW TO RUN; everything after RELEASE HISTORY is a
record of how the system got here and can be skipped.

  WHAT CASTALIA/386 IS ....... what this program does, and does not do
  HARDWARE REQUIREMENTS ...... what it needs, and what it checks for
  INSTALLING ................. copying the release onto a DOS machine
  HOW TO RUN ................. starting it, and the switches it takes
  THE DESKTOP IN A NUTSHELL .. the mouse and keyboard in one page
  CONFIGURATION .............. CASTALIA.INI
  BUILDING FROM SOURCE ....... Open Watcom, then wmake
  RELEASE HISTORY ............ what changed in every version, newest
                               first

WHAT CASTALIA/386 IS
--------------------
CASTALIA/386 is a small, fast, Windows-3.x/95-era-styled GRAPHICAL SHELL
that runs ON TOP OF MS-DOS 6.22 or FreeDOS.  It draws a desktop on a
plain VGA card, gives you a mouse pointer, a launcher menu, draggable
windows, a file manager and a system panel, and it launches ordinary DOS
programs for you.

It is designed to be genuinely usable on a real Intel 386SX with 4 MB of
RAM - not just inside an emulator - so it is deliberately lean.

Castalia has been built clean with Open Watcom C and verified booting
under DOSBox 0.74 in BOTH video modes - 320x200x256 (Mode 13h) and
640x480x16 (Mode 12h, planar) - with the same desktop code.  Captured
screenshots are in DOCS/SCREENSHOTS/:
    01-desktop.png ............... the 320x200 desktop, icons and clock
    02-dominus-menu.png .......... the Dominus launcher menu (from the INI)
    03-disk-cabinet.png .......... the file manager listing a real C:\
    04-clean-exit.png ............ a clean return to the DOS prompt
    05-disk-cabinet-tools.png .... the Disk Cabinet toolbar + scroll bar
    06-new-folder-dialog.png ..... the New Folder text-input dialog
    07-delete-confirm.png ........ the delete confirmation dialog
    08-mode12h-640x480-desktop.png the 640x480 (Mode 12h) desktop
    09-mode12h-disk-cabinet.png .. the file manager at 640x480
    10-mode12h-dominus-menu.png .. the Dominus menu at 640x480
    11-bitmap-icons-moncloa.png .. custom .ICN icons + the Moncloa theme
    12-bitmap-icons-640x480.png .. the same at 640x480 (icons drawn 2x)
    13-applet-calculator.png ..... the Calculator applet
    14-applet-scrapbox.png ....... the Scrap Box text editor
    15-applet-clock.png .......... the Clock & calendar applet
    16-applet-sketchpad.png ...... the Sketch Pad pixel / icon editor
    17-boot-splash-13h.png ....... the 256-colour boot splash (Mode 13h)
    18-boot-splash-12h.png ....... the 16-colour boot splash (Mode 12h)
    19-memory-gate.png ........... the 1 MB minimum-memory check at start-up
    20-gradient-title-bars.png ... gradient active title bars + dithered
                                   drop shadows (windows float over the
                                   desktop and each other)
    21-program-drawer-13h.png .... the Program Drawer (windowed launcher)
    22-program-drawer-12h.png .... the Program Drawer at 640x480
    23-theme-workbench.png ....... the "workbench" theme (Amiga homage)
    24-system-inspector.png ...... the System Inspector: CPU, memory and
                                   disk read live off the machine
    25-benchmark.png ............. the Benchmark, with its three result bars
    26-music-box.png ............. the Music Box and its four tunes
    27-minigames.png ............. Tic-Tac-Toe and Fifteen, side by side
    28-open-zoom-trail.png ....... a window zooming open (motion trail)
    29-restore-zoom-trail.png .... a window flying out of its taskbar button
    30-window-shading.png ........ a window shaded to its title bar
    31-sine-scroller.png ......... the SINE SCROLLER Light Show effect
    32-twister.png ............... the TWISTER Light Show effect
    33-quadrix.png ............... Quadrix, the falling-blocks game
    34-depot.png ................. Depot, floor 1 cleared
    35-stopwatch.png ............. the Stopwatch (running) and timer
    36-eyes.png .................. the Eyes, mid-glance
    37-fireworks.png ............. the FIREWORKS Light Show effect
    38-context-menu.png .......... the right-click Dominus menu (2 columns)
    39-patience.png .............. Patience (klondike) freshly dealt
    40-lights-out.png ............ Lights Out
    41-settings.png .............. the Settings panel
    42-matrix-dots-desktop.png ... matrix theme + dots backdrop, set live
    43-wallpaper-desktop.png ..... GLOBE.ICN tiled as desktop wallpaper
    44-settings-saved.png ........ Settings with the saver row, just saved
    45-icon-drag.png ............. a desktop icon mid-drag (outline)
    46-icon-dropped.png .......... ...and dropped: the two slots swapped
    47-keyboard-nav.png .......... arrow keys walking the icon grid
    48-cascade.png ............... F5: the open windows cascaded
    49-tile.png .................. F6: the same windows tiled
    50-oracle-overview.png ....... the System Oracle's overview page
    51-oracle-benchmark.png ...... the Oracle benchmark: THIS PC vs the
                                   PC/XT, the AT 286 and the 386SX/16
    52-oracle-memory.png ......... XMS/EMS/extended memory, probed live
    53-maximized.png ............. a window maximized to the work area
    54-clouds-wallpaper.png ...... the CLOUDS wallpaper, tiled live
    55-toolbox-icons.png ......... the Toolbox: every tool, its own icon
    56-arcade-icons.png .......... the Arcade: every game, its own icon
    57-menu-banner.png ........... the Dominus menu's CASTALIA stripe
    58-wallpaper-picker.png ...... Settings' Paper row, saved to the INI
    59-agenda.png ................ the Agenda to-do list, one item done
    60-hex-peek.png .............. Hex Peek on CASTALIA.EXE (the MZ header)
    61-toolbox-v020.png .......... the Toolbox with Agenda and Hex Peek
    62-gramophone-midi.png ....... a MIDI file loaded, notes on the roll
    63-gramophone-playing.png .... the melody playing, current note lit
    64-gramophone-wav.png ........ a WAV's real waveform on the scope
    65-ico-desktop.png ........... the desktop wearing real .ICO icons
    66-my-computer.png ........... the file browser, now "My Computer"
    67-gif-wallpaper.png ......... a full GIF picture as the wallpaper
    68-gif-window.png ............ a window over it - UI colours intact
    69-sunset-source.png ......... the SUNSET.GIF wallpaper full-size
    70-about-logo.png ............ the About box: castle on a night sky
    71-about-credits.png ......... the rolling credits scroller
    72-about-system.png .......... the live System tab
    73-about-easteregg.png ....... ...a hidden PARTY MODE
    74-start-button.png .......... the castle badge on the Start button
    75-castle-wallpaper.png ...... the CASTLE.GIF wallpaper (v0.25 default)
    76-start-crest.png ........... the real .ICO castle crest on the Start
                                   button (v0.25)
    77-cinema-plasma.png ......... the Cinema playing CINEMA.FLC (v0.26)
    78-cinema-icon.png ........... the Cinema film-strip desktop icon (v0.26)
    79-gramophone-sb.png ......... the Gramophone playing a WAV through the
                                   Sound Blaster (v0.27)
    80-oracle-sound.png .......... the Oracle's Sound line naming the card
                                   (v0.27)
    81-gramophone-fm.png ......... the piano roll stacking chord bars while
                                   MIDI plays on the OPL FM synth (v0.28)
    82-oracle-fm.png ............. the Oracle reporting "+OPL FM" (v0.28)
    83-win95-desktop.png ......... the pixel-accurate Windows-95 desktop
                                   and taskbar (v0.31)
    84-win95-window.png .......... a window with the navy->blue title
                                   gradient and crisp bevels (v0.31)
    85-start-cascade.png ......... the Start menu's Programs cascade flying
                                   out beside the top column (v0.32)
    86-win95-dialog.png .......... a Windows-95 message box: default ring,
                                   close box, sunken field (v0.33)
    87-my-computer-icons.png ..... My Computer showing the drives as big
                                   icons in a grid (v0.34)

You can replace the desktop icons with your own art - either the simple
text .ICN format or genuine Windows .ICO files (ASSETS/ICONS) - pick a
theme (classic / penumbra / bureau /
winsteel / moncloa / workbench / ocean / rose / midnight / amber /
matrix), override individual colours, choose a desktop backdrop
(solid / dots / weave / gradient - a subtle vertical sweep of the
theme colour), and tile a .ICN as wallpaper on top of it - all from
CASTALIA.INI, no recompiling.  See ASSETS/ICONS and ASSETS/THEMES for
the formats.

Choose the resolution with  video=mode13h  or  video=mode12h  in
CASTALIA.INI.  Mode 13h is the default; Mode 12h gives a roomier,
more Windows-like desktop on any VGA card (it needs ~150 KB free).  At
640x480 Castalia automatically uses the crisp VGA 8x16 font and 64x64
icons, and every control scales to match, while Mode 13h is unchanged.

WHAT CASTALIA/386 IS NOT
------------------------
  * It is NOT an operating system.  DOS still owns the disk, the file
    system and memory.  Castalia is just a program you run from the
    DOS prompt.
  * It is NOT a kernel or a bootloader.
  * It is NOT Windows, and it is NOT Windows-compatible.  It cannot run
    Windows .EXE programs.  It runs DOS programs.
  * It does NOT multitask.  When you launch a DOS program, Castalia
    steps aside, the program runs to completion, and the desktop comes
    back.
  * It uses NO Microsoft (or other third-party) names, icons or artwork.
    The look is an original tribute to the era.

A BOOT SPLASH WORTHY OF 1989
----------------------------
Before the desktop, Castalia paints a boot splash.  In Mode 13h it is the
full 256-colour show: a gradient sunset sky with a scattered starfield, a
chrome "CASTALIA/386" logo on a copper raster band, your machine's detected
CPU and RAM, and a progress bar - the demoscene look of the era it salutes.
In Mode 12h it is a clean 16-colour title panel.  The splash is paced off
the BIOS 18.2 Hz tick so it runs at the same speed on a real 386 and inside
an emulator.

HARDWARE REQUIREMENTS
---------------------
  Minimum (ENFORCED at start-up - see below):
    * Intel 386SX (or better) CPU
    * 1 MB RAM.  Castalia checks this before it touches the screen and
      refuses to start on less, with a clear message at the DOS prompt.
      "1 MB" means conventional + extended/XMS >= 1024 KB - the same
      ceiling an Amiga 500 reached with the classic 512 KB "slow"
      expansion, and the bar this project deliberately sets for itself.
      (The check asks the XMS manager directly, so a machine running
      HIMEM.SYS is measured correctly, not reported as bare 640 KB.)
    * VGA display adapter
    * MS-DOS 6.22 or FreeDOS
    * Hard disk with FAT16
  Recommended:
    * 4-8 MB RAM
    * A Microsoft-compatible mouse with its INT 33h driver loaded
      (e.g. MOUSE.COM, CTMOUSE.EXE)

INSTALLING
----------
A compiled, ready-to-run build is committed in the  release/  directory
(see release/RELEASE.TXT) - copy that whole folder to your DOS machine and
you are ready to go; no compiler needed.

From the distribution directory (the one holding CASTALIA.EXE), run:

         INSTALL                 (installs to C:\CASTALIA)
    or   INSTALL D:\APPS\CAST    (installs to a path you choose)

There are two installers in the folder, and DOS picks the graphical one:
COMMAND.COM looks for .EXE before .BAT, so plain INSTALL now starts
INSTALL.EXE, a Castalia-styled setup with an editable target path, a
progress bar and an optional AUTOEXEC.BAT PATH line.  It was built by
every source build for fourteen versions and shipped in none of them;
now it travels with the bundle.

Type  INSTALL.BAT  in full for the batch installer instead.  It is the
zero-tools path - plain MS-DOS 6.22 / FreeDOS, no COPY /Y, no CHOICE -
and it does the same job: creates the target directory, copies
CASTALIA.EXE, a starter CASTALIA.INI (never overwriting one you already
have), README.TXT and the ASSETS\ICONS bitmaps, then prints how to start
Castalia and the optional PATH line for AUTOEXEC.BAT.  It writes only
inside the target directory and does not edit AUTOEXEC.BAT or CONFIG.SYS.
(You can also just copy the files by hand - nothing requires either
installer.)

HOW TO RUN
----------
  1. Install as above, or copy CASTALIA.EXE and (optionally) CASTALIA.INI
     into a directory, for example  C:\CASTALIA .
  2. Make sure a mouse driver is loaded if you want to use the mouse.
  3. From the DOS prompt:

         CD \CASTALIA
         CASTALIA

  4. The desktop appears.  Use it like this:
       * Move the mouse, click desktop icons (DOUBLE-click to open).
       * Click the "Castalia" button at the bottom-left for the
         Dominus launcher menu.
       * Drag windows by their title bar; minimize them with the [_] box
         and close them with the [X] box.
       * The taskbar shows a button for every open window: click it to
         focus a window, minimize the focused one, or restore a minimized
         one.  Shift+Tab cycles through the open windows (Alt+Tab too,
         where the BIOS delivers it - see keyboard.h).
       * "Run..." (in the Dominus menu) opens a box to type any command or
         program to launch.
       * DOUBLE-click a title bar to MAXIMIZE the window (and again to
         restore it), exactly like Windows 95.  The window's title-bar
         icon is the shade control now: click it once to roll the window
         up to its bare title bar (and once more to unroll), double-click
         it to close the window - the Windows-95 system-icon gesture.
       * Leave the desktop idle and the Light Show takes over as a
         screensaver; any key or mouse movement brings it back.  Windows
         zoom out of whatever you launched them from, minimize into their
         own taskbar button, and everything fades rather than snaps.
         All tunable in CASTALIA.INI (screensaver=, animations=).
       * Press ESC to close the top window, or - on a clear desktop -
         to exit back to DOS.  "Exit to DOS" is also in the Dominus menu.

THE DESKTOP IN A NUTSHELL
-------------------------
  Disk Cabinet ... the file manager: browse, enter folders, run programs,
                   switch drives, and create / rename / copy / move /
                   delete files via the toolbar or keys (F7 new, F2
                   rename, F5 copy, Del delete, Backspace = up)
  Command Room ... drops you to a DOS prompt; type EXIT to come back
  System Panel ... DOS version, memory and hardware information
  System Inspector live CPU / RAM / disk gauges with bar meters
  Benchmark ...... times CPU, memory and graphics and scores the machine
  Toolbox ........ a group window of desk utilities: Scrap Box (text
                   editor), Cardfile (an index-card notepad), Calc
                   (four-function calculator), Clock (an analog face and a
                   digital readout over a month calendar), Sketch Pad (a
                   pixel editor that Saves .ICN files), Character Map (every
                   printable glyph), Colors (a swatch board of the active
                   theme's palette) and Fractal (a Mandelbrot / Julia set
                   explorer in fixed-point integer maths - click to zoom in;
                   J cycles Julia sets, M returns to the Mandelbrot, O/I zoom
                   out/in, R reframes), Stopwatch (lap timer with tenths and
                   a beeping 1-99 minute countdown), Eyes (they follow
                   the pointer; they blink; they judge), Settings (live
                   theme / backdrop / animation / sound switching), the
                   System Oracle (a full hardware/software profiler with
                   the classic benchmark bars), the Agenda (a persistent
                   to-do list saved to AGENDA.TXT), Hex Peek (a hex
                   file viewer with on-demand paging) and the Gramophone
                   (a WAV / MIDI player - Sound Blaster, Covox, Disney
                   Sound Source or bare PC speaker for WAV, OPL2 FM or
                   speaker for MIDI - with a live spectrum analyzer,
                   scope and folder playlist)
  Arcade ......... a group window of games: the Fifteen Puzzle, Tic Tac
                   Toe, Minefield (a minesweeper), Reversi (Othello, vs the
                   machine), Serpent (a snake), Breaker (a block-breaker;
                   steer the paddle with the mouse or the arrow keys),
                   Echo (a sound-memory game - repeat the growing tune of
                   flashing panels), Quadrix (the falling-blocks game),
                   Depot (a warehouse box-pushing puzzle, ten verified
                   floors), Patience (klondike solitaire with the winning
                   cascade) and Lights Out (the lamp puzzle).  Every game
                   has PC-speaker sound effects when sound=true.
  Screensaver .... not an app but always on watch: after screensaver=N
                   idle seconds the desktop dissolves into a full-screen
                   demoscene reel - plasma, copper bars, a warp starfield,
                   fire, the Boing ball, a rotozoomer, a tunnel, matrix
                   rain, tumbling vector balls, a giant sine scroller, a
                   twister column, a fireworks night, a tumbling wire-frame
                   cube, a voxel-terrain flyover, a textured raycast
                   corridor and a mirrored kaleidoscope (sixteen effects,
                   cross-fading one into the next); any key or the mouse
                   brings the desktop back
  Program Drawer . a windowed grid of your own [shortcut] launch entries
  About .......... the About Castalia/386 box
  Dominus menu ... the launcher (the "Castalia" taskbar button)

The Toolbox and Arcade are "program groups" - windowed icon grids that keep
the full toolset one click away without crowding the menu.  Everything is
reachable from the Dominus menu, or from CASTALIA.INI as internal commands:
the group openers  tools | arcade | demos  and the applets themselves
scrap | calc | clock | paint | charmap | music | puzzle | ttt | mines |
reversi | snake | breaker | echo | fractal | quadrix | depot |
patience | lights | stopwatch | eyes | settings  (alongside fileman,
sysinfo, inspect, bench, about, help, drawer and exit).  Sketch Pad
doubles as the icon editor: draw a 32x32 image, Save it as
ASSETS\ICONS\MINE.ICN, then point a desktop or [shortcut] icon at it.

RUNNING BIG GAMES (FREE-MEMORY LAUNCH)
--------------------------------------
Normally Castalia stays in memory (~120 KB) while a launched program runs.
For a memory-hungry DOS game that is too much.  Mark its [shortcut] with

        freemem=true

and start Castalia through the wrapper:

        CASTSHEL          (instead of  CASTALIA )

Now launching that entry UNLOADS Castalia completely, runs the game with
all of conventional memory, and brings the desktop back when the game
exits.  Started as plain CASTALIA (no wrapper) the same entry just launches
normally, so nothing breaks either way.

CONFIGURATION
-------------
Edit CASTALIA.INI (plain text) to change the theme, the starting
directory, the desktop icons and the Dominus menu.  See the comments
inside CASTALIA.INI.  If the file is missing, sensible defaults are used.

BUILDING FROM SOURCE
--------------------
See BUILD.TXT.  In short: Open Watcom C, then "wmake" (or BUILD.BAT).

MORE DOCUMENTATION
------------------
  BUILD.TXT ............ how to compile, test in DOSBox, deploy
  tests/README.md ...... the sanitizer tests for the file-format parsers
  DOCS/GUIDE.TXT ....... using the desktop, applet by applet
  DOCS/FORMATS.TXT ..... CASTALIA.INI, .ICN, the .ICO subset, GIF rules
  DOCS/ARCHITECTURE.TXT  how the program is put together
  DOCS/HARDWARE.TXT .... the acceptance lap on real 386/486 machines
  DOCS/ROADMAP.TXT ..... what is planned for v0.2 and beyond


======================================================================
 RELEASE HISTORY
======================================================================

Newest first.  Nothing below this line is needed to install or run
Castalia; it is kept in the shipped README because the release bundle
travels as a single directory and this is the only history that goes
with it.

NEW IN 0.56 - WHAT THE SHELL KNEW AND DID NOT SAY
---------------------------------------------------------------------
  * FIND TEXT IN THE SCRAP BOX.  F5 finds, F6 finds the next,
    case-insensitively and wrapping at the end.  The document grew to
    16 KB this release, which is more than anyone can read by scrolling.
  * THE SCRAP BOX HAS A SCROLL BAR.  It was the only scrolling view in
    the shell without one, which mattered little at 4 KB and a great
    deal at 16: arrows step a line, the trough pages, and the thumb
    finally says how much document there is and where you are in it.
  * THE SKETCH PAD SAYS WHICH DRAWING IS OPEN, in its title bar.  Save
    pre-fills with the name it is holding, so a name you cannot see is
    a file you can overwrite by accident.
  * THE SCRAP BOX HOLDS 16 KB, not 4.  Its buffer was the largest thing
    left in the 64 KB near-data segment the whole shell shares, and it
    bought a notepad that could not hold a README - open one and the
    Scrap Box refused to save it back rather than destroy the tail.  The
    buffer lives in far memory now: four times the document, and the
    near segment drops from 93% full to 86%, which is the headroom
    everything else has to grow into.
  * THE SCRAP BOX'S LOAD BUTTON WORKS.  It did nothing at all - no
    error, no file, no change - because the path handed to the loader
    was the same buffer the loader clears before reading it.
  * THE MUSIC BOX PLAYS AGAIN.  It lost its verb in 0.44 and kept
    everything else - four tunes on the PC speaker, playback that
    carries on while the window is minimised, an oscilloscope that
    tracks the sounding note - so the shell has spent eleven versions
    with a finished applet nobody could open, while the manual went on
    listing "music" as a thing you could type.  Now you can, and it
    answers to the keyboard: arrows pick a tune, Enter plays it or
    stops it, S stops.  It is in the Toolbox too.
  * SHUT DOWN ASKS ABOUT ALL OF YOUR WORK, NOT SOME OF IT.  Closing a
    window over unsaved changes had always asked; leaving the shell
    asked about the Scrap Box and the Sketch Pad and nothing else.
    SETTINGS applies every change live, so the desktop already wore
    the new theme while CASTALIA.INI still held the old one - the one
    unsaved state that looks saved - and Shut Down discarded it in
    silence, though merely closing the window would have stopped and
    asked.  The CARDFILE and the AGENDA refuse to write a file holding
    more records than their table, because rewriting it from the
    truncated table would delete the rest for good; both said so in a
    red banner that lives and dies with the window, so an edit made
    and buried was gone without a word.  All four are asked about now.
  * FIND FILE SEARCHES WHAT IS INSIDE A FILE.  A Text field beside
    Pattern, or F2: case-insensitive, applied only to the files the
    name pattern already matched, and shown on the panel at all times
    so a filter can never quietly drop files from a later search.
    This is the question DOS has no grep to answer - which file
    mentions HIMEM, which save has your name in it.
  * MY COMPUTER STOPS CALLING EVERY DRIVE A FLOPPY.  Every drive was
    drawn as a 3.5" diskette and captioned "Local Disk" underneath, so
    the icon contradicted the label and a real floppy in A: looked
    exactly like the hard disk at C:.  DOS is asked instead (INT 21h
    AX=4408h/4409h): a fixed disk, a network drive and a diskette now
    have their own icons and their own captions.
  * THE DRIVE ROW SHOWS THE DRIVES YOU HAVE.  It was a fixed A: B: C:
    D: - half of them dead on a machine with no second floppy, and no
    button at all for a CD or a network drive at E: or beyond, which
    could only be reached by going out to My Computer and back in.
  * THE AGENDA RESPONDS TO ITS OWN KEYS.  It opened with no row
    selected, so Space to tick and Del to remove - the only two keys
    that do anything - were inert until an arrow key was pressed.
  * THE MANUAL LISTS THE APPLETS YOU CAN LAUNCH.  Corral, the Typing
    Tutor and Help were missing from FORMATS.TXT's verb list, which is
    also what a desktop icon and a [shortcut] take, so putting the
    Typing Tutor on the desktop meant guessing its name.  Both
    directions are gated in CI now.

NEW IN 0.55 - THE FILE PICKER RELEASE
---------------------------------------------------------------------
  * A FILE PICKER, AND EVERY PATH PROMPT USES IT.  Load and Save in
    the Scrap Box and the Sketch Pad, the Hex Peek, the Gramophone,
    and Copy and Move in the Disk Cabinet were all bare text fields:
    you had to know the path already and type it correctly, in a shell
    whose whole point is that you can see your disk.  Folders first
    with a wildcard filter on the files, arrows/PgUp/PgDn/Home/End,
    Enter to descend or to open, Backspace to go up, and the browsed
    folder along the foot.  GOING UP FROM A DRIVE ROOT LISTS THE
    DRIVES, so a document can be saved to a floppy - which was simply
    not possible before, there being no drive control and no room in
    an 8.3 name field to type a path to one.  It leaves the caller's
    directory exactly as it found it, drive included.
  * THE SHELL WORKS WITHOUT A MOUSE.  A sweep for controls the mouse
    could reach and the keyboard could not found five, three of which
    cost you work rather than convenience.  The SCRAP BOX and SKETCH
    PAD had New/Load/Save on the toolbar only, so you could compose a
    document or draw an icon and have no way to keep it - F7 new, F3
    load, F2 save in both now.  The CARDFILE's whole toolbar was
    click-only, leaving fifteen of its sixteen cards unreachable
    (PgUp/PgDn flip, F7 adds, F8 removes).  The CHARACTER MAP had no
    keys at all - type a character to select it.  And the GRAMOPHONE
    refused every key while nothing was loaded, with EJECT, the thing
    that loads, behind that guard: E opens, F adds a folder, S stops,
    PgUp/PgDn step the playlist.
  * TAG FILES WITH SPACE.  The Disk Cabinet marks a set - names in
    red, the count in the path row - and Delete, Copy and Move all
    act on the whole set with ONE question instead of one per file.
    Copy and Move ask for a destination FOLDER, which the file picker
    now knows how to be; the Gramophone's + FOLDER uses it too,
    having asked you to type a path from memory until now.
  * YOUR FILES STOP DISAPPEARING.  New in either editor left the OLD
    filename behind, so New-then-Save offered the name of the
    document you had just been reading and wrote a blank page over
    it.  A CARDFILE.DAT holding more than sixteen cards was rewritten
    short the moment you flipped a card.  And a copy whose
    destination was the file itself emptied it - fopen("wb")
    truncates before the read begins.  All three are closed, and a
    copy that would overwrite something now asks first whether it is
    one file or twenty.
  * A DIRECTORY TOO BIG TO LIST SAYS SO.  Both the Disk Cabinet and
    the picker walked a directory until their table was full and then
    dropped the rest without a word, so the file you came for was
    missing from a list that still looked complete.  Both mark a
    truncated listing now, and the picker's table went from 96 entries
    - which a real DOS directory passes easily - to the Cabinet's 200.
  * THE DISK CABINET'S LAST KEYBOARD GAPS: Move had no key while New,
    Rename, Copy and Delete all had one (F6 now), Home and End, and
    TYPE-AHEAD - a letter jumps to the next entry starting with it,
    which on a FAT directory of any size is the difference between
    finding a file and scrolling for it.
  * A TEXT EDITOR YOU CAN CROSS.  The Scrap Box moved by one line and
    no more; PgUp/PgDn move by the visible page and Ctrl+Home/End go
    to the ends of the document.  The Program Drawer, given a
    scrollbar last version, pages and jumps too.
  * MODE 12H LOOKS FINISHED.  The title bar is a real gradient,
    dithered navy into light blue.  Icons are SCALED rather than
    doubled - 32x32 art shown at 64x64 turned every diagonal into a
    staircase of 2x2 blocks, which a monitor bezel survives and a
    round icon does not.
  * THE BOOT LOGOTYPE STOPPED SLICING ITS OWN LOWERCASE IN HALF.  The
    chrome ramp went nearly black at 45% of its length, which is the
    middle of the x-height of every letter whose ink starts below the
    cap line.
  * THE VOXEL LANDSCAPE IS A FLYOVER.  It had been three flat slabs:
    the camera flew between the ridges rather than over them, the
    projection put the nearest visible slice four map cells wide, and
    there was no distance cue at all.  Now with atmospheric haze.
    VECTOR BALLS has actual shaded spheres, and Patience has real
    card faces - clipped corners, a mirrored index, a centre pip - so
    a fanned pile reads as separate cards.
  * SETTINGS ASKS BEFORE CLOSING ON UNSAVED PREFERENCES.  It applies
    every change live, so the desktop already looks right while
    nothing has reached the INI - the one case where closing silently
    is most likely to be mistaken for having saved.
  * RELEASE/ STOPPED SHIPPING STALE DOCUMENTATION.  Its README had
    spent three versions telling users that Alt+Tab cycles windows,
    long after that moved to Shift+Tab, because nothing compared the
    shipped copy to the original.  Something does now.  README.TXT
    itself opened with 1069 lines of changelog, putting INSTALLING at
    line 1247 of 1396; the history moved to the end.
  * BUGS FIXED: the file picker dropped clicks outright; Enter could
    not reach a subfolder when the name field was pre-filled;
    browsing to a destination discarded the name you were about to
    save under; a path typed with a drive on it was joined onto the
    browsed folder and named nothing; and cancelling out of a browsed
    drive left the whole shell standing on it.

NEW IN 0.54 - THE SPEED, SAFETY AND KEYBOARD RELEASE
---------------------------------------------------------------------
  * FASTER WHERE IT COUNTS.  A clip rectangle now runs through every
    drawing primitive, and a partial repaint composes ONLY the region
    it is about to send to the screen.  A taskbar clock tick used to
    restore all 64000 bytes of the cached desktop and rebuild every
    window on top of it; measured under DOSBox, that same tick now
    changes exactly 72 pixels and touches nothing else.  Solid fills
    move through "rep stosd" and the mouse pointer is compiled to two
    bitmask columns instead of 192 separate plotted pixels (about 1500
    VGA port writes per arrow in 640x480).  video, font, window and ui
    - the four modules that ARE the inner loop - are now built for
    speed while the other 58 stay built for size.
  * THE ARCADE TAKES THE KEYBOARD.  Fifteen slides with the arrows,
    Tic Tac Toe and Reversi walk with the arrows and play with Enter,
    and Minefield digs with Enter and flags with Space.  Reversi marks
    its legal squares with a dot, the way Othello always has.  2048,
    which was the opposite problem, now plays with the mouse as well.
  * THE RIGHT MOUSE BUTTON WORKS INSIDE WINDOWS.  It used to reach the
    bare desktop only.  Minefield flags with it.
  * MINEFIELD HAS A CLOCK, and a mine counter that no longer counts
    below zero once you plant more than ten flags.
  * F1 ANSWERS "WHAT CAN I DO HERE" FIRST, listing the focused
    window's own keys above the general reference, which is itself
    rewritten into three paged sections.
  * SETTINGS TAKES THE KEYBOARD: Tab and the arrows walk the three
    tabs and their controls, Enter activates, S saves.  Without a
    mouse driver the whole preferences panel used to be unreachable.
  * YOUR WORK STOPS DISAPPEARING.  Quitting asks about an unsaved
    Scrap Box or Sketch Pad (three separate exit paths did not).  A
    save that FAILS no longer marks the document saved - a write-
    protected floppy used to show an error and then throw the work
    away at the close prompt.  Opening the Sketch Pad or Calculator a
    second time no longer wipes what is already in it.  The Cardfile
    writes before it clears its dirty flag, and asks before deleting a
    card.  Copying over an existing file asks first.  Settings now
    reads the INI from Castalia's own directory, so preferences
    stopped coming back forgotten when it was launched from the PATH.
  * THE PROGRAM DRAWER SCROLLS, so entries past the first screenful
    are reachable at all.
  * FIRST AUTOMATED TESTS.  The three file-format parsers (GIF, ICO,
    INI) compile unmodified against a host shim and run a corpus of
    deliberately hostile files under AddressSanitizer - 29 cases, in
    CI.  tests/README.md is honest about the two classes it does not
    cover.
  * BUGS FIXED: a 16-bit overflow that corrupted the upper half of the
    Light Show's master rainbow; 24-bit WAV playing at the wrong
    speed and decaying into noise; a MIDI parser reading past its
    track; the benchmark bar showing a 1-pixel green sliver where it
    should show clipped red; floppies reporting "0 KB"; a 110 ms
    desktop freeze the first time the Oracle's CPU page painted;
    Breaker unable to congratulate a record because the flag was
    never set; desktop labels overprinting the next row's icon; and
    the About icon shipping as a black square instead of a round
    bubble.
  * The near-data segment went from 83% full to 74%.

NEW IN 0.53 - CORRAL, THE TYPING TUTOR, RIBBONS - AND THE GREAT AUDIT
---------------------------------------------------------------------
One for the Arcade, one for the Toolbox, one for the Light Show - and
underneath them, the largest bug-fixing sweep in the project's life.

  * CORRAL.  The beloved early-90s "build a fence, trap the balls"
    arcade puzzle joins the Arcade.  Balls carom around a walled pen;
    a click plants a fence post that grows BOTH ways (red arm up/left,
    cyan arm down/right - Space flips the axis, and a yellow guide
    under the pointer shows which way it will grow).  When an arm
    reaches solid ground it hardens; when both land, any pen left
    without a ball inside is instantly bricked over.  Corral 75% of
    the floor to clear the level; a ball striking a still-growing arm
    knocks the whole fence down and costs a life.  More balls every
    level, spare lives pay a bonus, and the best run is kept in
    CASTALIA.HI.  Integer maths in Q4 cell units, board in far memory,
    physics off the BIOS tick - and like Breaker, it politely pauses
    whenever its window is not on top.

  * THE TYPING TUTOR.  A proper shareware-era typing drill for the
    Toolbox: one practice line at a time, strict entry (a wrong key
    beeps and counts an error but never advances), LIVE words-per-
    minute off the BIOS tick chain (1092 ticks a minute, five keys a
    word), an accuracy count, and a persistent best WPM in CASTALIA.HI
    (only lines typed at 85%+ accuracy may set a record).  The drill
    book runs from home-row exercises through pangrams to honest DOS
    folklore ("Abort, Retry, Ignore, Fail?") - 24 lines, all living in
    far memory.  Enter restarts a line, Tab skips it.

  * RIBBONS.  A seventeenth demoscene effect for the Light Show and
    screensaver: the office-classic bouncing polylines - two four-
    cornered ribbons caroming off the screen edges, each towing a
    dozen rainbow afterimages through a 240-entry DAC rainbow.  A
    frame is one clear plus 96 short Bresenham lines, cheaper than a
    plasma fill, so it glides on a bare 386SX.

  * ICONS OF THEIR OWN.  Pong and 2048 no longer share the generic
    die: Pong gets its court, paddles mid-rally and ball; 2048 its
    tile board mid-merge.  Corral and the Typing Tutor arrive with
    hand-drawn icons of their own (fence-and-balls; a keyboard with
    one key lit).

  * GROUP WINDOWS LEARNED TO SCROLL.  The Toolbox had quietly outgrown
    the 320x200 work area: its bottom row hid under the taskbar and -
    worse - the oversized window swallowed the taskbar's clicks, Start
    button included.  Group windows now clamp to the work area and
    scroll by rows (arrow buttons, PgUp/PgDn, or just walking the
    selection); the Program Drawer clamps the same way.

  * THE GREAT AUDIT.  A systematic sweep of every module - games,
    applets, media, core - fixed 38 real bugs.  The highlights:
      - Every GIF narrower than the screen (wallpaper or gallery)
        rendered as diagonally sheared garbage: the decoder wrote
        rows at screen stride while every reader assumed packed rows.
      - A truncated .FLC could hang the machine in an unbreakable
        loop; malformed frames now stop the Cinema cleanly.
      - Phantom clicks: presses accumulated inside modal dialogs (and
        while a launched DOS program ran) replayed as desktop clicks
        the moment control returned - closing windows and launching
        icons nobody asked for.  All drained now.
      - AGENDA.TXT, CASTALIA.HI, CASTALIA.INI, CASTRUN.BAT and the
        Picture Show gallery were opened RELATIVE to whatever folder
        the Disk Cabinet last browsed - scattering data files around
        the disk.  Everything is now anchored to the install home.
      - Midnight: the BIOS tick reset made the Stopwatch jump to
        garbage, kept countdowns from ever ringing, and could fire
        the screensaver mid-keystroke.  All three ride it out now.
      - Pong's ball could tunnel straight through a correctly-placed
        paddle at full rally speed; the hit test now covers the whole
        substep of travel.
      - Four games overflowed their status-line buffers (Serpent on
        EVERY frame); the Sound Blaster time constant wrapped on long
        WAVs and played them ~80x too fast; the WAV chunk walker
        missed the RIFF pad byte; a crafted MIDI could loop the
        parser forever; the Mode 12h scroll thumb wrapped 16-bit
        maths; the Calculator applied stale operands on 5 + - 3;
        Patience's win cascade painted over neighbouring windows;
        the calendar jumped eleven months at its range edges; Echo's
        first game was always the same colour...  and more of the
        same species, each with the regression scenario written down.

NEW IN 0.52 - THE MEMORY DIET (YOU CANNOT SEE IT; THAT IS THE POINT)
--------------------------------------------------------------------
  * THE DGROUP DIET.  A real-mode program gets ONE 64 KB near-data
    segment for all its near statics, its stack AND the near heap that
    stdio buffers live in - and ours had crept to 91% full.  The new CI
    memory gauge (it reads the linker map on every build and fails past
    a 56 KB watermark) caught it on its first run, so v0.52 is the diet
    the 1.0 roadmap promised: the 8x8 font face, all eighteen theme
    palettes, the editor's line-start table and the screensaver's text
    pools moved to far memory, and the file-copy buffer slimmed - about
    3.5 KB of near data freed for the heap.  Nothing looks different;
    everything breathes easier.  Also swept two old compiler warnings
    (a missing string.h prototype and an unreferenced parameter) while
    the hood was open.

NEW IN 0.51 - TWO WISHES OFF THE DREAM LIST
-------------------------------------------
The roadmap's "after 1.0" wish list said more speakers of the era and
localised Start buttons.  Both landed early:

  * COVOX AND DISNEY SOUND SOURCE.  The Gramophone's WAV playback now
    drives the two beloved printer-port DACs of the early 90s:
      - COVOX SPEECH THING (lptdac=covox): the resistor-ladder DAC on
        the LPT data lines.  No FIFO, no handshake - Castalia paces one
        sample after another off the PIT, at near the WAV's native rate.
        Eighteen resistors and a soldering iron made you one in 1991;
        every clone works the same.
      - DISNEY SOUND SOURCE (lptdac=dss): the little grey box with the
        16-byte FIFO that drains at ~7 kHz - fed with the same status/
        control handshake the id Software classics used.  (A guard keeps
        an unplugged box from ever hanging the player.)
    Neither device can be probed safely - poking bytes at a printer
    prints garbage - so, like the BLASTER variable of the era, you
    declare it: lptdac= and lptport= (1..3) in CASTALIA.INI; the port
    address comes from the BIOS.  Priority is Sound Blaster, then the
    LPT DAC, then the PC-speaker PWM floor that always works.  The
    player's device line shows what is actually sounding ("Covox LPT1").
    A 386 laptop with no ISA slots just became a music machine.

  * A START BUTTON IN ANY TONGUE.  startlabel= in [system] renames the
    Start button - Start, Demarrer, Avvio, Iniciar, anything up to
    eleven characters - and the button sizes itself to fit, in either
    video mode.  The default remains, of course, "Inicio".

NEW IN 0.50 - THE GRAMOPHONE'S ANALYZER LEARNS GRAVITY
------------------------------------------------------
  * SMOOTH SPECTRUM BARS.  The Gramophone's analyzer used to move only
    when a chord changed - between notes it froze mid-leap.  Now gravity
    runs at the full ~18 Hz BIOS tick: every bar falls fast the moment
    its note stops sounding, the white peak caps drift down slowly above
    them, drum hits still thump the low bins - the true mid-90s player
    look, finally in motion.  Press Stop and the whole skyline collapses
    gracefully to rest instead of vanishing.
  * THE LCD TITLE CRAWLS.  A track name too long for the LCD strip now
    scrolls smoothly across it while the music plays (a character every
    few ticks), instead of lurching one notch per chord.
  * STILL 386-FRUGAL.  The animation runs only while the Gramophone is
    the focused window (the same politeness rule the Music Box's scope
    follows); the music itself always advances, focused, buried or
    minimized.  And once every bar and cap is at rest, repaints stop
    entirely - an idle player costs nothing again.

NEW IN 0.49 - THE THEME GRID PREVIEWS ITSELF: POINT AND SEE
-----------------------------------------------------------
  * A LIVE PREVIEW STRIP.  The Settings Theme tab gains a preview well
    under the grid of eighteen swatches: point the mouse at any theme and
    the strip instantly paints a whole miniature screen in that theme's
    colours - desktop, a desktop icon, an open window with title bar and
    paper, and the taskbar with its Start button - beside the theme's
    name and a one-line character note ("green phosphor glow", "loud,
    period, beloved"...).  Move the pointer away and the strip settles on
    the theme now in force.  The swatch under the pointer also gets a
    subtle focus ring, so eye and mouse stay together.
  * It costs nothing until you move: hover tracking rides the existing
    idle loop the way the Eyes follow the pointer, and each change
    repaints just the panel through the fast partial-present path -
    no full-desktop redraws, still happy on a 386SX.

NEW IN 0.48 - KALEIDOSCOPE AND THREE MORE THEMES: SLATE, SAKURA, DOS
-------------------------------------------------------------------
  * KALEIDOSCOPE.  A sixteenth demoscene effect joins the screensaver
    reel: the plasma field folded into eight-fold dihedral symmetry, then
    DAC-cycled through the rainbow.  Each screen point is reflected into a
    single 1/8th wedge (fold X against Y, then reflect on the diagonal) so
    the whole display becomes a living mandala of mirrored plasma - the
    same cheap integer plasma maths, painted once, then animated for free
    by cycling the palette.  On a bare 386 it costs nothing per frame but a
    DAC write, so it spins forever without breaking a sweat.
  * THREE MORE THEMES, EIGHTEEN IN ALL.  The Theme grid gains:
      - SLATE    a cool blue-gray IDE look: steel desktop, slate title
                 bars, crisp near-white chrome and sky-blue accents.
      - SAKURA   a soft cherry-blossom scheme: dusty-rose desktop, warm
                 blossom title bars, pale-pink chrome and plum accents.
      - DOS      a deep-blue text-mode homage: an EGA-blue desktop and
                 title bars over classic light-gray chrome with the
                 phosphor-cream highlight of an old CRT.
    Each keeps the crisp black text, working bevels and readable accents
    every theme needs; set one in Settings, from CASTALIA.INI (theme=dos)
    or the Colors board.

NEW IN 0.47 - THREE NEW THEMES: SUNSET, FOREST AND HOT DOG
----------------------------------------------------------
  * THREE THEMES JOIN THE WORKBENCH.  Fifteen colour schemes now, up from
    twelve - and they show up as sprites in the Settings > Theme grid the
    moment you open it:
      - SUNSET   a dusk scheme: a dusky-plum desktop, burnt-coral title
                 bars, warm pink-gray chrome and violet accents.
      - FOREST   a woodland scheme: a deep-moss desktop, forest-green
                 title bars, sage-gray chrome and leaf-green accents.
      - HOT DOG  the garish Windows-3.1 "Hot Dog Stand" homage - a
                 bright-red desktop and title bars over a bright-yellow
                 face.  Loud, period, and beloved.
    Each keeps the crisp black text, working bevels and readable accents
    every theme needs, so folders, icons and dialogs render correctly; set
    one in Settings, from CASTALIA.INI (theme=sunset) or the Colors board.

NEW IN 0.46 - CORRIDOR: A RAYCAST MAZE FOR THE SCREENSAVER
---------------------------------------------------------
  * CORRIDOR.  A fifteenth demoscene effect joins the reel the idle
    screensaver walks through - and it is the indoor twin of the voxel
    landscape: a Wolfenstein-style raycast maze with TEXTURED BRICK WALLS.
    A 16x16 grid (one row packed per int, so the map costs 32 bytes) is
    ray-marched per screen column to the first wall; the column is then
    drawn as a vertical span sampled from a 16x16 running-bond brick
    texture and shaded by distance - near walls bright, far walls fading
    into the ceiling colour as fog - between a sky-blue ceiling gradient
    and a warm floor.  An auto-pilot strolls the corridors, pivoting when
    a wall comes up close.  Like every effect it is pure integer maths off
    the shared sine table with a Q8 fixed-point camera, one march and one
    texture divide per column, so it flies on a bare 386SX.  With the
    voxel flyover it makes the pair of legendary early-90s 3D techniques -
    outdoor heightmap and indoor textured raycaster - both running in the
    same 256-colour screensaver.

NEW IN 0.45 - SETTINGS, REBUILT WITH TABS AND THEME SWATCHES
------------------------------------------------------------
  * TABBED SETTINGS.  The Settings panel is now organised into three tabs
    - Theme, Desktop and System - so each page breathes instead of one
    crowded column.  Click a tab to switch; the Save button lives at the
    foot of every page.
  * A SWATCH FOR EVERY THEME.  The Theme tab shows all twelve themes as a
    grid of buttons, each carrying a tiny window-mockup sprite painted in
    THAT theme's own colours (its desktop, a face window and a title
    strip), so you can see a theme before you pick it.  The colours are
    mapped to the nearest live palette slot, so it works in either video
    mode and never disturbs the DAC.
  * MORE ON THE SYSTEM TAB.  Sound and the screensaver delay are joined by
    a new taskbar-clock toggle (Clock: ON/off, applied on the spot), and a
    read-out of the active theme and video mode.
  * DESKTOP TAB.  The backdrop pattern, the wallpaper < name > cycler and
    the window-animation toggle gather on their own page.
  * The apply logic is unchanged - a theme still cross-fades in, the
    backdrop and wallpaper still repaint the desktop live, and Save still
    writes CASTALIA.INI.

NEW IN 0.44 - A LEANER APP SET (LIGHT SHOW & MUSIC BOX RETIRED)
--------------------------------------------------------------
  * THE LIGHT SHOW AND MUSIC BOX APPS ARE RETIRED.  Neither is launchable
    any more - the "Light Show" entry is gone from the Media menu, the two
    Dominus [shortcut] pins are gone, and the "demos"/"lightshow" and
    "music" verbs no longer resolve.  The app set stays focused on the
    tools people actually reach for.
  * THE SCREENSAVER LIVES ON.  Only the interactive apps went; the idle
    screensaver still runs the full demoscene reel (plasma, fire, the
    Boing ball, the wire-frame cube, the voxel landscape and the rest)
    after screensaver=N idle seconds, and the PC-speaker sound effects and
    startup chime are untouched - both were always separate from the two
    retired windows.
  * (Audio now lives in one place: the revamped Gramophone with its folder
    playlist is the music app; the classical-tune Music Box is gone.)

NEW IN 0.43 - THE GRAMOPHONE GROWS A PLAYLIST
---------------------------------------------
  * A REAL PLAYLIST.  The Gramophone now carries a scrolling playlist of a
    whole folder's tracks, the playing one lit blue.  Click a track to
    play it; the Prev / Next transport buttons walk the list; and when a
    MIDI ends the player auto-advances to the next track (a WAV, being a
    blocking burst, is loaded and waits for Play so the desktop never
    freezes on an auto-advance).
  * "+ FOLDER" - ADD A WHOLE DIRECTORY.  A new button in the playlist
    header pops a folder prompt (blank = the current directory) and scans
    it with DOS find-first/next for every .WAV and .MID, up to 48 tracks,
    filling the list in one go.  The 8.3 names live in far memory, so the
    near heap the player already juggles is untouched.
  * A ROOMIER, TIDIER PLAYER.  The window grew to fit the list; the
    oscilloscope / spectrum visualiser is now a compact strip above it,
    and the LCD title bar, device line, seven-button transport and seek
    slider are all unchanged - the skinned-player look, now with a
    playlist the way a certain 1997 MP3 player had one.

NEW IN 0.42 - THE BENCHMARK, REBUILT: SIX TESTS AND A COMPOSITE SCORE
--------------------------------------------------------------------
  * SIX MICRO-BENCHMARKS.  The old three-bar Benchmark grows into a full
    six-test suite, each timed in the same four-tick window off the BIOS
    clock: two integer kernels (an LCG mix and a heavier multiply/xor
    hash), two memory kernels (a far dword copy and a far dword read) and
    two video kernels (solid fills and horizontal spans).
  * A COMPOSITE SCORE.  Every raw count is turned into an index against a
    386SX/16 (which scores 1.00x) shown right on its bar, and the six
    fold into one big CASTALIA SCORE with a machine-class verdict - from
    "XT / slow-286 class" through "486-class throughput" to "modern
    silicon (emulated?)".
  * ANIMATED, COLOUR-CODED BARS.  Each test gets its own colour and a
    bright leading edge, and the whole chart CHARGES UP from empty when a
    run finishes - the score sweeps up with it.  The animation only
    spends repaints for the half-second it lasts (bench_animating()), so
    a settled window is free.
  * STILL HONEST, STILL INTERACTIVE.  The raw counts are the real
    measurement; Enter or a click runs the whole suite again.  All
    integer maths, no coprocessor, no assembly.

NEW IN 0.41 - THE SYSTEM INSPECTOR, REBUILT (PANEL + INSPECTOR MERGED)
---------------------------------------------------------------------
  * ONE SYSTEM APP.  The old text-only System Panel and the gauge-based
    System Inspector are now a single window - the System Inspector -
    that tells a 386/486 owner everything about the box at a glance.
    (The "sysinfo" and "system" verbs now open it too, so any old
    shortcut still works.)
  * A LIVE DASHBOARD.  A sunken readout names the processor and
    coprocessor, DOS version, video mode and mouse, each hardware fact
    with its own green/dark status LED; beside it a live green
    oscilloscope sweeps over an LED time-of-day clock.
  * ANIMATED GAUGES.  Conventional, extended/XMS and disk-space bars now
    CHARGE UP from empty when the window opens, each with a bright
    leading edge - the whole panel repaints ~6x a second off the BIOS
    tick via the partial-present fast path, so it costs almost nothing.
  * MORE DETAIL, HONESTLY PROBED.  A new equipment strip reads the BIOS
    equipment word (INT 11h): serial (COM) and parallel (LPT) port
    counts, the number of floppy drives and whether a game port is
    present - alongside the total RAM and a blinking LIVE indicator.
    No faked readouts; only what the machine actually reports.

NEW IN 0.40 - A FOURTEENTH LIGHT SHOW EFFECT: VOXEL LANDSCAPE
------------------------------------------------------------
  * VOXEL LANDSCAPE.  The Light Show's new showpiece: a Comanche-style
    terrain flyover.  A procedurally generated 128x128 world - rolling
    hills, blue lakes, sandy shores, bare rock and snow-capped ranges -
    is ray-marched front-to-back one z-slice at a time, with a per-column
    y-buffer so a near ridge cleanly occludes everything behind it and a
    vertical sky gradient fills the top.  The camera flies forward with a
    gentle bank, bob and pitch.  It is all integer maths off the shared
    parabolic sine table - the world runs in Q8 fixed point, the
    projection costs one divide per slice, and a level-of-detail march
    thins the far slices - so it flies on a bare 386SX with no
    coprocessor and never touches the theme's palette slots.  Fourteen
    effects now cycle in the Light Show and the idle screensaver.

NEW IN 0.39 - A THIRTEENTH LIGHT SHOW EFFECT: WIREFRAME
------------------------------------------------------
  * WIREFRAME.  A new effect joins the Light Show reel: a 3D wire-cube
    tumbling on all three axes over a quiet starfield, its twelve edges
    lit in a rainbow that flows around the frame and its eight corners
    drawn as glowing spheres - larger and brighter on the near face,
    small and dim on the far one, so a bare frame of lines still reads
    as solid depth.  Like every effect it is pure integer maths off the
    shared parabolic sine table (the same fixed-point rotation the
    vector balls use) and a clamped Bresenham line, so it flies on a
    bare 386SX with no coprocessor.  SPACE / arrows reach it, ESC
    returns to the desktop; thirteen effects now cycle in both the
    interactive show and the idle screensaver.

NEW IN 0.38 - CASTALIA 92: THE WINDOWS-95 CUT  (RELEASE CANDIDATE)
------------------------------------------------------------------
  This is the release-candidate build of the Castalia 92 era: every
  roadmap feature through v0.8 is in, the docs are frozen, and the one
  box left open before v1.0 is hardening on real 386/486 iron.

  * REBRANDED TO CASTALIA 92.  The whole environment now wears the
    Castalia 92 name - boot splash, Start menu, About box, window
    titles and the Light Show scroller - while the look stays as close
    to Windows 95 as the palette allows.
  * A REAL START BUTTON.  The Vista-style orb is gone.  In its place is
    a proper Windows-95 Start button: a crisp, black-outlined pixel-art
    castle (keep, two towers under red roofs, a pennant, lit windows and
    an arched gate) beside an "Inicio" caption, in a raised frame that
    sinks when the Dominus menu is open.
  * PIXEL-ART ICONS.  Every marquee icon was redrawn as clean, fully
    outlined pixel art: a brand-new My Computer (a beige CRT on a desktop
    case), plus sharper Folder, Toolbox, Arcade, Gramophone, Terminal,
    System, Info, Drawer, Disk, Oracle, Notepad and Gauge icons.
  * A TIDIER DESKTOP.  The desktop now shows just four icons - My
    Computer, Arcade, Toolbox and Gramophone - laid out top-to-bottom
    then left-to-right in neat columns.
  * A REBUILT ABOUT BOX.  The hand-drawn night scene is gone; the banner
    now shows the real desktop wallpaper straight through the window,
    with the embossed marque floating over it, and a fuller identity
    block beneath.
  * A BRUTAL ORACLE BENCHMARK.  The System Oracle's benchmark grew from
    three bars to a six-test suite - ALU, integer hash, memory copy,
    memory read, video fill and video lines - each with a raw x-index
    against a 386SX/16 and a bar, folded into one composite ORACLE SCORE
    with a machine-class rating ("486-class throughput", and so on).
  * A RICHER DESKTOP GRADIENT.  The default backdrop now sweeps on a
    smoothstep curve from a lit sky at the top to a deep base at the
    foot, and the About box repaints far less often for a calmer idle.
  * SIX NEW WALLPAPERS.  BLISS (rolling green hills under puffy
    clouds), COSMOS (a ringed planet over a nebula), DUNES (a banded
    retro sun setting over the desert), NIGHT (the castle by
    moonlight), REEF (light rays, fish and kelp under the sea) and
    PEAKS (snow-capped mountains over a pine forest at dawn) join
    CASTLE and SUNSET in ASSETS\ICONS - each a 320x200 GIF drawn for
    this release, 3-7 KB apiece.  Settings' "Paper" row is now a
    < name > cycler that walks every shipped tile AND picture,
    applying each on the spot.
  * THE PICTURE SHOW.  A full-screen GIF gallery in the Toolbox: it
    walks every .GIF in ASSETS\ICONS (the wallpapers, plus any picture
    you drop there) with a palette dissolve between slides - LEFT/RIGHT
    to browse, ESC back to the desktop.
  * FIND ON THE START MENU.  "Find File" now sits above Run... in the
    Dominus menu with its own magnifier glyph - the authentic Win95
    menu order.
  * A WALLPAPER PALETTE FIX.  Returning from the Light Show, the
    screensaver or the Cinema used to leave a GIF wallpaper wearing the
    effect's colours (the free DAC window had been redriven); the
    desktop now reloads the wallpaper palette on the way back.
  * FIND FILE.  The classic Win95 accessory joins the Toolbox: give it
    an 8.3 wildcard (*.TXT, CONFIG.*) and it sweeps every directory of
    the drive breadth-first, listing name, size and folder - with the
    hourglass up while it works, arrows/PgUp/PgDn to scroll, and its
    own magnifier-over-folder icon.
  * SORTABLE DISK CABINET.  A new toolbar button (or F3) cycles the
    listing order - Name, Extension, Size (biggest first) or Date
    (newest first); directories stay alphabetical on top.
  * A DRAGGABLE SCROLL THUMB.  Grab the Cabinet's scroll-bar thumb and
    the list follows the pointer live (fast-path repaints); clicking
    the track still pages.
  * CONFIGURABLE FILE ASSOCIATIONS.  The Cabinet's double-click table
    (text -> Scrap Box, .ICN -> Sketch Pad, WAV/MID -> Gramophone,
    FLI/FLC -> Cinema, executables -> DOS, anything else -> Hex Peek)
    can now be overridden per extension from the INI's new [assoc]
    section - e.g. TXT=peek or PCX=peek.
  * A SELF-FILLING PROGRAM DRAWER.  Point the INI's new [drawer]
    scan=DIR at a directory and every EXE, COM and BAT in it joins the
    drawer's grid automatically - sorted by name, launched with that
    directory as its working dir, capped to what the screen can show.
  * A GRAPHICAL INSTALLER.  Building from source now also produces
    INSTALL.EXE - a Castalia-styled setup wizard (editable target
    path, an AUTOEXEC.BAT PATH option, a live progress bar) built
    from the same video/font/ui objects as the desktop.  INSTALL.BAT
    remains the zero-tools way to install a binary distribution.
  * A TWELFTH THEME: REDMOND.  The pure-VGA Windows scheme - primaries
    straight off the 16-colour card, no tinting anywhere.  Louder than
    classic, exactly period.
  * SYSTEM EVENT SOUNDS.  The PC speaker now marks the desktop's life,
    Windows-95 style: a soft blip when a window opens, falling blips on
    close and minimize, a rising one on restore, and the classic
    attention ding when a dialog pops.  All non-blocking, all silenced
    by sound=false (or the tray speaker).
  * REAL DOCS.  docs\GUIDE.TXT is a five-minute user guide and
    docs\FORMATS.TXT freezes every on-disk format (INI reference,
    .ICN, the .ICO subset, GIF wallpaper rules, CASTALIA.HI); the
    ROADMAP is up to date again.  INSTALL.BAT now ships the .GIF and
    .ICO assets too, and "INSTALL <dir> PATH" appends the PATH line to
    AUTOEXEC.BAT (duplicate-guarded; nothing is touched without the
    explicit argument).

NEW IN 0.36 - THE CASTALIA ORB, AND A SKINNED GRAMOPHONE
--------------------------------------------------------
  * A SPECTACULAR ABOUT BOX.  The About page is now a living nighttime
    diorama: a depth-graded sky (dithered dark at the zenith, blue at the
    horizon), a cratered moon, FORTY twinkling stars, a SHOOTING STAR
    that streaks across every few seconds, and the castle - pennant
    waving - on a dark hill.  Below it the marque is set at 2x in a deep
    white-over-blue-over-black emboss, with the version, the tagline and
    a fact line ("20,000+ lines of C - 55 modules - 0 libs").  A new
    HISTORY tab rolls the release milestones from 0.25 to today, and the
    whole box is wider to fit its six tabs.
  * TWO NEW PROGRAMS.
      - PONG.  The 1972 classic against a beatable house AI: the paddle
        follows your mouse (arrows work too), each rally gets a touch
        faster, spin follows where the ball meets the paddle, and the
        first to seven wins.  Big scoreboard digits, net, sounds.
      - CALENDAR.  A month view on the DOS clock with today ringed in
        navy; arrows (or Left/Right) turn the month, PgUp/PgDn the year,
        Home snaps back to today.  Weekends in red, like a desk blotter.
    Pong joins the Arcade group; Calendar sits in the Toolbox and the
    Start menu's Documents.
  * A THIRD NEW GAME: 2048.  The tile-sliding puzzle, backported to
    1989: slide the board with the arrows, equal tiles merge and double,
    a new tile drops each move.  The whole game state is sixteen bytes.
    Reach 2048 (the board keeps going); run out of moves and it is over.
  * BREAKER GROWS LEVELS.  Clearing the wall now advances you to the
    next level instead of ending the game: six wall shapes cycle (full,
    checkerboard, pyramid, columns, diamond, fortress), level 3 hardens
    the top row into two-hit mortar bricks (level 5 hardens two rows),
    the ball gains pace every third level, and each cleared wall pays a
    bonus ball.  The status line shows the level.  Smashed bricks now also
    drop POWER-UP CAPSULES you catch on the paddle: [W] a wider paddle for
    a while, [+] a free ball, [S] the ball slows down.
  * PERSISTENT HIGH SCORES.  Breaker and 2048 remember your best across
    sessions in CASTALIA.HI (a tiny "game score" text file); the best is
    shown on the game-over line and the 2048 header.  A missing or
    read-only file is simply "no records yet" - the games never fail.
  * THE SKETCH PAD LEARNS TOOLS.  Alongside the freehand pen: FILL (a
    flood fill of the clicked region - recursion-free, so it can never
    blow the stack) and LINE (click the two ends and a Bresenham line
    rules between them).  The active tool shows pressed-in on the
    toolbar.
  * MAXIMIZE HAS AN ANIMATION.  Maximizing or restoring a window now
    flies the frame outline between the old and new geometry, exactly
    like the minimize-to-taskbar zoom.  (animations=false in the INI
    still turns every zoom off.)
  * A NEW START BUTTON: THE CASTALIA ORB.  The taskbar Start button no
    longer wears a flat little crest - it wears a glossy crystal ORB with
    the castle sealed at its heart.  A spherical highlight ramps from a
    bright top-left down to a deep navy, a dark rim rounds the edge, a
    gold keep with battlements and a gate stands in the middle, and a
    white specular spark crowns it.  There is no label and no button
    frame - just the pearl seated on the taskbar, Windows-Vista style.
    It is drawn procedurally in a single pass (no .ICO to load or miss),
    so it stays crisp at any size and recolours with the active theme; the
    larger orbs even pick up a glassy reflection on the lower rim.
  * THE GRAMOPHONE IS NOW A SKINNED PLAYER.  The Gramophone window is
    rebuilt as a 386-native take on the classic mid-90s music player:
      - a black LCD strip up top with the elapsed / total time in green
        digits, a SCROLLING title marquee, and the format tag (MID / WAV);
      - a full SPECTRUM ANALYZER for MIDI - bars climbing from a green
        floor through yellow to red, each with a slow-falling white peak
        cap, bouncing to the notes on the OPL - or an oscilloscope for a
        WAV;
      - a device / quality line (OPL FM synth, Sound Blaster or PC
        speaker, plus BPM or sample rate);
      - a real TRANSPORT ROW of pixel-glyph buttons - previous, play,
        pause, stop, next, EJECT (load another file without leaving the
        player) and REPEAT (loop the song);
      - and a click-to-seek POSITION SLIDER with a raised thumb.
    Space or Enter toggles play/pause; the arrow keys nudge the position.
  * SHARPER ICONS.  The Gramophone's desktop icon is now a matching
    mini-player with its own little spectrum; the folder and floppy icons
    pick up a glassy sheen.  It is all still pure vid_* drawing - free on
    a 386SX.
  * MORE WINDOWS-95 EVERYWHERE.
      - ICONS IN THE START MENU.  Every entry now carries a little icon on
        its left - a window for Programs, a page for Documents, sliders
        for Settings, a note for Media, a "?" for Help, a prompt for Run,
        a red power symbol for Shut Down - exactly like the real thing.
      - A GRADIENT BRAND BANNER.  The menu's left stripe is a real
        vertical gradient - navy blooming into sky blue toward the
        taskbar - with "CASTALIA95" set in raised, embossed capitals.
      - SHARPER CAPTION BUTTONS.  A window's title-bar buttons are the
        genuine Windows-95 glyphs now: a little window for Maximize, two
        overlapping windows for Restore, a low-left bar for Minimize and
        a bold X for Close.
      - THE FULL SHUT-DOWN SEQUENCE.  Shut Down first shows the calm blue
        "Please wait while your computer shuts down..." screen (the
        Castalia crest standing in for the flag), then the black panel
        everyone remembers: big amber "It's now safe to turn off your
        computer."  Press a key and you drop back to DOS.
      - A SYSTEM-MENU ICON ON EVERY WINDOW.  Each window now wears the
        Castalia crest at the top-left of its title bar, the way a Win95
        window shows its icon.
      - A SYSTEM TRAY.  A little speaker sits by the taskbar clock - and
        wears a red slash when the sound is switched off, just like the
        volume icon of 1995.
      - "CLICK HERE TO BEGIN."  A brand-new desktop greets you with the
        famous yellow balloon and a red arrow pointing at the Start orb;
        it vanishes the instant you click or press a key.
      - MUTE FROM THE TRAY.  Click the tray speaker to toggle the sound
        on and off; the red slash follows the setting, and the change
        takes effect at once.
      - A BLUE-SCREEN EASTER EGG.  Run "bsod" (or "crash") for the fatal-
        exception screen everyone loved to hate - "A fatal exception 0E
        has occurred..." and all.  Any key returns you to the desktop.
      - AN HOURGLASS WAIT CURSOR.  The pointer turns into the classic
        hourglass while a slow, non-interactive job runs - probing the
        hardware for the System Panel, running the Benchmark, or scanning
        the drives for My Computer - then snaps back to the arrow.
      - EVERY WINDOW WEARS ITS OWN FACE.  The title-bar system icon is
        per-application now: a folder on My Computer and the drawers, a
        ruled page on the note tools, a die on the games, a note on the
        players, a monitor on the system tools, a clock, a calculator, a
        paint box - and the crest where nothing fits better.
      - WINDOWS-95 MOUSE GESTURES.  Double-click a title bar to MAXIMIZE
        (and again to restore).  The title-bar icon is the system icon:
        click it to SHADE the window to its bare title bar, double-click
        it to CLOSE - so shading moved from the old title double-click to
        the icon, and every gesture lands where 1995 muscle memory
        expects it.

NEW IN 0.35 - A FULL GENERAL-MIDI VOICE BANK
--------------------------------------------
  * SIXTEEN FAMILIES, NOT SIX VOICES.  The OPL/AdLib FM synth's
    instrument bank grew from six shared patches to eighteen: one
    distinct timbre for each General-MIDI family (piano, chromatic
    percussion, organ, guitar, bass, strings, ensemble, brass, reed,
    pipe, synth lead, pad, effects, ethnic, percussive, sound-fx) plus
    dedicated electric-piano and synth-bass voices.
  * A MIDI's program-change events now select the matching family, so
    each part sounds like itself - a piano is a piano, a bass is a bass -
    instead of everything collapsing onto a handful of voices.
  * Still free on a 386: it is only FM register data, and it works on
    any AdLib-compatible chip (no Sound Blaster required for music).

NEW IN 0.34 - MY COMPUTER, WITH DRIVE ICONS
-------------------------------------------
  * A REAL "MY COMPUTER" ROOT.  Opening My Computer now shows the drives
    as big icons in a grid - "Local Disk (C:)", "Floppy (A:)", one per
    drive DOS reports - exactly like Windows 95, instead of dropping
    straight into a file list.
      - Click to select, arrow keys to move, double-click or Enter to
        open a drive and browse its folders.
      - "Up" on the toolbar (or Backspace) from a drive's root climbs
        back out to the drive icons.
  * The folder view itself is unchanged - the same toolbar, drive strip,
    path line and scrolling file list, now one hop in from the drives.

NEW IN 0.33 - WINDOWS-95 MESSAGE BOXES
--------------------------------------
  * THE REAL THING.  Every modal dialog (Run, Rename, Delete, New Folder,
    the Save/Load and Gramophone/Hex Peek prompts) is now a genuine
    Windows-95 message box.
      - The default button (OK / Yes) wears the heavy black ring.
      - Buttons press IN under the mouse and only fire on release over
        them - slide off before you let go and nothing happens.
      - A dotted focus rectangle follows Tab and the arrow keys; Enter
        takes the focused button, Space too, Esc cancels.
      - A working close (X) box sits in the gradient title bar.
  * It is still synchronous and self-contained - each box runs its own
    little event loop over the frozen back buffer, so a click only ever
    repaints the box's own footprint.

NEW IN 0.32 - A START MENU THAT CASCADES
----------------------------------------
  * FLY-OUT SUBMENUS.  The Start ("Dominus") menu is now a single tidy
    column with Windows-95 cascades.  Programs, Documents, Settings and
    Media each show the little right-arrow and open a child column when
    you hover them - no more one long flat list.
      - Programs : your pinned [shortcut] items, then the Program Drawer
                   and an MS-DOS "Command Room" prompt.
      - Documents: Scrap Box, Cardfile, Agenda, Hex Peek.
      - Settings : Settings, Colors, Character Map.
      - Media    : Gramophone, Cinema, Light Show.
    Help, Run... and Shut Down... sit at the foot, and a vertical
    "CASTALIA95" brand stripe runs down the left edge.
  * NEVER OFF-SCREEN.  A submenu that would spill past the right edge
    flips to the left of its parent; one taller than the 320x200 screen
    wraps into balanced columns, exactly like the real Programs menu on a
    machine with a lot of software installed.
  * Still instant: opening and cascading only repaint the menu's own
    footprint, so it stays snappy on a 386SX.

NEW IN 0.31 - THE WINDOWS-95 LOOK, DIALLED IN
---------------------------------------------
  * PIXEL-ACCURATE 1995.  The default "Classic" theme is now the exact
    Windows-95 palette: teal desktop, silver (C0C0C0) 3D face, a PURE
    WHITE highlight and 808080 shadow so every bevel is crisp and snappy,
    navy title bars, black text.  Buttons, lists, menus and the taskbar
    all read as the real thing.
  * THE ICONIC TITLE BAR.  An active window now wears the famous navy ->
    sky-blue title gradient (the Windows-95-Plus / Windows-98 look): solid
    navy on the left blooming into bright blue on the right, white title
    text on top.  An inactive window falls back to a flat gray bar, just
    as it did in 1995.
  * It costs nothing: the whole restyle is palette values and one gradient
    ramp, so a 386SX draws it exactly as fast as before.
  * (Prefer another look?  The Settings panel still cross-fades between all
    the built-in themes, and the castle wallpaper is still the backdrop.)

NEW IN 0.30 - DRUMS, AND CLEARER SOUND-BLASTER WAV
--------------------------------------------------
  * MIDI HAS A BEAT NOW.  Channel 10 (percussion) plays as real drums on
    the OPL chip's RHYTHM MODE - bass drum, snare, hi-hat, tom and cymbal,
    mapped from the General-MIDI drum map.  Rhythm mode overlays the drums
    on channels 6-8, so the music runs on six melodic FM voices plus the
    five percussion voices.  A song has a groove, not just chords.
  * CLEARER WAV.  When a Sound Blaster is present a WAV now plays near its
    NATIVE sample rate - downsampled only enough to fit the buffer - rather
    than the old fixed ~4 kHz, so digitised audio is much crisper.  (No
    Sound Blaster?  The PC-speaker path is exactly as before.)
  * A drum-and-melody demo ships in ASSETS\MEDIA\DRUMBEAT.MID.

NEW IN 0.29 - FM INSTRUMENTS (A GENERAL-MIDI VOICE BANK)
-------------------------------------------------------
  * THE FM MUSIC HAS INSTRUMENTS NOW.  Instead of playing every MIDI file
    on one organ voice, the Gramophone reads a file's program-change
    events per channel and gives each note the matching OPL patch from a
    small General-MIDI-flavoured bank - PIANO, ORGAN, STRINGS, BASS, BRASS
    or LEAD - so a piano part sounds like a piano and a bass line like a
    bass.
  * The instrument travels WITH its note into the chord, so when different
    parts are struck together each voice keeps its own timbre.  The FM chip
    is only re-programmed when a voice's instrument actually changes, so
    the extra realism costs almost nothing - still 386SX-cheap.
  * Nothing to configure: drop in any .MID and it plays with voices.  (No
    FM chip?  The top line still plays on the PC speaker as before.)

NEW IN 0.28 - OPL FM MUSIC (POLYPHONIC), AND A REAL STARTUP CHIME
----------------------------------------------------------------
  * MIDI IS MUSIC NOW.  A from-scratch OPL2 (Yamaha YM3812 / AdLib)
    driver plays a MIDI file as real, POLYPHONIC, instrument-timbred FM
    music through the FM chip on any AdLib or Sound Blaster - a world away
    from the one-voice square wave the PC speaker managed.  Nine voices,
    all register pokes to ports 388h/389h, no interrupts, no DMA - so it
    is cheap and runs on a bare 386SX.
  * HOW IT WORKS: the MIDI parser now keeps EVERY note struck at a given
    moment (not just the top one), so the song becomes a stream of CHORDS.
    Each chord is sounded on as many FM voices as it has notes; the drum
    channel is skipped.  The Gramophone's piano roll stacks one bar per
    voice, so you can SEE the harmony - a triad is three bars in a column.
  * A REAL STARTUP CHIME.  With sound on and an FM chip present, Castalia
    opens with a bright polyphonic arpeggio that lands on a major chord.
    (No chip?  The old PC-speaker chime plays, and MIDI falls back to the
    top-line melody on the speaker.)
  * SEE IT: the Gramophone names its synth ("OPL FM synth (polyphonic)"),
    and the System Oracle's Sound line reports the FM chip.

NEW IN 0.27 - SOUND BLASTER DIGITAL AUDIO
-----------------------------------------
  * THE GRAMOPHONE SPEAKS THROUGH A SOUND BLASTER NOW.  A WAV plays as
    real digital audio out of the card's DAC instead of pulse-width
    modulation on the PC speaker - a night-and-day difference in quality.
  * HOW IT FINDS THE CARD: it reads the BLASTER environment variable
    (A=base port, I=IRQ, D=8-bit DMA), and if that is absent it probes
    the usual base ports (220h, 240h, ...), resetting the DSP and reading
    its version to learn what it is (Sound Blaster / 2.0 / Pro / 16).
  * HOW IT PLAYS: the 8-bit samples go out as one single-cycle DMA
    transfer programmed straight on the 8237 controller and the DSP.  The
    card's interrupt line is masked at the PIC for the duration, so there
    is NO interrupt handler to install and nothing left hooked when the
    burst ends - and the whole thing is integer, paced off the BIOS tick,
    so it runs on a bare 386SX.  A key stops it early.
  * NO CARD, NO PROBLEM: with no Sound Blaster present it falls straight
    back to the PC-speaker PWM burst, exactly as before.
  * SEE IT: the Gramophone names its output device, and the System
    Oracle's Overview page lists the card and its base port on a "Sound"
    line.  (MIDI still plays its melody on the PC speaker.)

NEW IN 0.26 - THE CINEMA: A FULL-MOTION FLI/FLC VIDEO PLAYER
-----------------------------------------------------------
  * CASTALIA PLAYS VIDEO NOW.  A from-scratch decoder for the Autodesk
    Animator "flic" formats - .FLI (0xAF11) and .FLC (0xAF12) - plays
    full-motion animations full-screen in 256 colours.  It handles the
    whole chunk set real flics use: the COLOR_256 and COLOR_64 palette
    chunks, BYTE_RUN run-length full frames, the FLI (LC) and FLC (SS2)
    inter-frame deltas, BLACK and FLI_COPY.  It streams the file frame by
    frame - it never loads the whole animation - so even a long flic fits
    in tiny memory, and it paces off the BIOS tick with no floating point,
    so it runs on a bare 386SX.
  * OPEN IT two ways: from the Toolbox ("Cinema") to watch the bundled
    demo, or double-click any .FLI/.FLC file in My Computer.  Any key or a
    mouse click ends it and returns you to the desktop.
  * IT PLAYS NICE WITH THE WALLPAPER.  A flic drives all 256 palette
    slots, which includes the window a GIF wallpaper's colours live in; the
    player saves that window before it starts and restores it on exit, so
    the desktop comes back looking exactly as you left it.
  * THE BUNDLED DEMO, ASSETS\MEDIA\CINEMA.FLC: a colour-cycling plasma
    with the CASTALIA/386 wordmark shimmering through it - 64 frames of
    pure palette animation, the classic demoscene move, in ~110 KB.
  * 256-colour Mode 13h only; the 16-colour 640x480 mode shows a short
    notice instead.

  (Windows 95 needed extra software to play video.  Castalia does it out
  of the box.)

NEW IN 0.25 - THE CASTLE CREST, FROM A REAL .ICO, AND A NEW WALLPAPER
--------------------------------------------------------------------
  * THE START BUTTON WEARS A REAL WINDOWS ICON.  The Castalia castle
    crest is now loaded from ASSETS\ICONS\CASTLOGO.ICO - a genuine .ICO
    file - and scaled down onto the taskbar Start button, so the same
    piece of art is the brand mark from the button to the About box.
    (Miss the file and the old hand-drawn badge takes its place.)
  * A NEW DEFAULT WALLPAPER, "CASTLE".  A castle on a rocky island at
    dusk, a pennant flying from its keep - the Castalia backdrop.  It
    ships as ASSETS\ICONS\CASTLE.GIF (320x200, 128 colours) and shares
    the screen with the 16-colour UI exactly like SUNSET did: its
    palette sits in the free DAC window 16..191, the UI and the desktop
    gradients keep their own slots, so a 128-colour picture and a
    Windows-style desktop coexist with no clash.  Prefer the old dusk
    sky?  Set  wallpaper=ASSETS\ICONS\SUNSET.GIF  in CASTALIA.INI.
  * UNDER THE HOOD: a new icon_draw_box() nearest-neighbour-scales any
    32x32 icon into a box of any size - the trick that fits a 32x32
    crest onto a 10-pixel-tall Start button as a few run-length fills.

  Next on the Windows-95 track: an FLI/FLC video player.

NEW IN 0.24 - THE CASTLE LOGO, AND A SPECTACULAR ABOUT BOX
---------------------------------------------------------
  * CASTALIA HAS A LOGO NOW: a little castle, drawn on the taskbar
    Start button (badge + name, Windows-95 style) and, big and proud,
    on the About box.
  * THE ABOUT BOX IS A SHOW.  Five tabs:
      - About .... the castle logo on a twinkling night sky, its
                   pennant waving; name, version, tagline.
      - Credits .. a rolling demoscene-style scroller: created by
                   DAVE ABELLAN, licensed through Tombatossals
                   Softworks LLC, and a from-scratch manifesto.
      - System ... live facts - RAM, conventional/free memory, disk,
                   video mode, and a BIOS-tick counter ticking away.
      - Built .... the ledger: 18,000+ lines of C, 50 modules, ZERO
                   third-party libraries, Open Watcom, and the list of
                   everything hand-rolled (VGA, GIF/LZW, WAV+MIDI, ICO).
      - License .. MIT, (C) 2026 Tombatossals Softworks LLC.
    Switch tabs by click, Tab, arrows or 1-5.  And there are easter
    eggs for the curious - click the castle a few times, or try a
    certain famous ten-key cheat code...

NEW IN 0.23 - A REAL GIF PICTURE AS YOUR DESKTOP WALLPAPER
---------------------------------------------------------
  * wallpaper= now accepts a GIF.  A full 320x200 GIF87a/GIF89a picture
    is decoded (complete LZW, interlaced or not) and shown as the
    desktop background - a real photo-style wallpaper, not a tile.  A
    hand-made SUNSET.GIF ships in ASSETS\ICONS and the default desktop
    now wears it (a castle at dusk, ordered-dithered so the sky
    gradient is smooth in 128 colours).
  * The clever part is the palette.  The shell's UI lives in DAC slots
    0..15 and its two gradient ramps in 192..255; a GIF's palette is
    loaded into the free window 16..191 and its pixels shifted to
    match - so a 128-colour picture and the 16-colour Windows-style UI
    share the screen with NO palette clash, and switching theme leaves
    the wallpaper untouched.  It is composed once into the cached
    background, so - like every Castalia wallpaper - it costs nothing
    per frame; even a 386SX drags windows over it at full speed.
  * (Only in Mode 13h, where there are 256 colours to divide up; the
    16-colour 640x480 mode keeps the tiled .ICN wallpapers.)

  Next on the same track: an FLI/FLC video player.

NEW IN 0.22 - REAL WINDOWS .ICO ICONS, AND "MY COMPUTER"
-------------------------------------------------------
  * CASTALIA NOW READS REAL WINDOWS .ICO FILES.  iconN_icon= (and a
    Program Drawer / shortcut icon=) accepts a genuine .ICO alongside
    the old text .ICN - the loader decodes 1/4/8-bpp palette icons AND
    24/32-bpp truecolour, honours the AND transparency mask, and scales
    any size to 32x32.  Each icon's colours are mapped once to the
    nearest of the 16 active theme slots, so real Win95-style icon art
    renders here AND recolours when you switch themes.  A crafted set
    ships in ASSETS\ICONS: COMPUTR, FOLDER, GAMES, MEDIA, TOOLS, INFO,
    DRIVE and PROGRAMS - and the default desktop now wears them.
  * THE FILE BROWSER IS "MY COMPUTER" now (its window, its desktop
    icon and its Dominus entry), with a proper monitor-and-tower icon -
    the Windows-95 feel this whole project is chasing.
  * (Fixed along the way: the config icon-path field was one byte too
    short for "ASSETS\ICONS\PROGRAMS.ICO", silently truncating the
    longest paths so those icons failed to load; it is now roomy.)

  Coming next on the same track: GIF images as desktop wallpaper (LZW,
  256 colours straight into the VGA DAC), then an FLI/FLC video player.

NEW IN 0.21 - THE GRAMOPHONE: WAV AND MIDI ON THE PC SPEAKER
------------------------------------------------------------
  * THE GRAMOPHONE plays sound files on nothing but the PC speaker -
    no sound card required, exactly as the era demanded.
      - A .WAV (PCM) is played as digitised audio by pulse-width
        modulation of the speaker (the RealSound trick), paced by the
        8253 channel-0 counter so the pitch is the same on a 386SX and
        a Pentium.  A live OSCILLOSCOPE traces the actual samples.
      - A .MID (Standard MIDI File) is parsed - variable-length delta
        times, running status, tempo meta events, all of it - and
        reduced to its top-voice melody, which plays note by note off
        the BIOS tick (so the desktop stays live) under a scrolling
        PIANO-ROLL of the tune with the current note lit.
    Double-click a .WAV or .MID in the Disk Cabinet, or Run "play".
    Two samples ship in ASSETS\MEDIA (SWEEP.WAV, ODE.MID).
  * A note on the wider media question (icons and wallpaper in ICO /
    GIF / PNG / JPG, and video): all are feasible on a 386 to varying
    degrees.  GIF (LZW, 256-colour, palette straight into the VGA DAC)
    is the natural next step for image wallpaper and is coming next;
    ICO follows; FLI/FLC is the right "video" format for this class of
    machine (MPEG/AVI is not real-time on a 386SX).  PNG and JPG are
    possible but heavy (inflate; Huffman + IDCT) and come last.

NEW IN 0.20 - TWO NEW TOOLS, EVERY FILE OPENS, AND A SNAPPIER MENU
------------------------------------------------------------------
  * AGENDA - a persistent to-do list.  Add entries through a dialog,
    tick them off with a click or the SPACE bar (done items strike
    through and go grey), delete with the button or Del.  The list
    lives in AGENDA.TXT next to CASTALIA.EXE as plain "[x] ..." lines,
    so any editor - or the Scrap Box - reads and writes the same file.
    Saved after every change.
  * HEX PEEK - a classic hex viewer: offset, eight bytes, ASCII
    column.  Pages are read on demand (one seek+read per scroll), so
    a 500 MB file costs the same as a 5-byte one.  PgUp/PgDn/Home/End
    and the arrows move through it.
  * EVERY FILE IN THE DISK CABINET NOW OPENS.  Double-click an
    executable and it runs; a .TXT opens in the Scrap Box, a .ICN on
    the Sketch Pad; and ANYTHING ELSE - a .EXE you want to inspect, an
    unknown extension, a data file - lands in Hex Peek.  A double
    click always answers with something true now.
  * A SNAPPIER DESKTOP.  Popping the Dominus menu (from the launcher
    or a right-click) and dismissing it used to blit the whole 64 KB
    frame; now only the menu's own rectangle and the taskbar are
    copied to VGA.  And dropping a dragged desktop icon no longer
    re-reads every .ICN from disk - the two bitmaps are swapped in
    memory (two far copies), so the rearrange is instant.

NEW IN 0.19 - A FACE FOR EVERYTHING, WALLPAPER ART, AND A VOICE
---------------------------------------------------------------
  * EVERY GAME AND TOOL HAS ITS OWN ICON now - sixteen new pieces of
    procedural 16-colour art.  The Arcade stops being a row of
    identical dice: the Fifteen Puzzle shows its sliding tiles,
    Minefield its spiked mine, Reversi its discs, Serpent its snake
    (apple included), Breaker its bricks and paddle, Echo its four
    Simon quadrants, Quadrix its falling piece, Depot its crate,
    Patience its ace of spades, Lights Out its lamp grid.  The
    Toolbox matches: tabbed index cards for Cardfile, the Mandelbrot
    silhouette for Fractal, a slider panel for Settings, a rising
    bar chart for Benchmark, and the all-seeing eye for the Oracle.
    Every icon scales to 64x64 in Mode 12h automatically.
  * A WALLPAPER COLLECTION ships in ASSETS/ICONS - CLOUDS (yes,
    clouds), STARS, DIAMOND lattice and CIRCUIT traces - and the
    Settings panel grew a "Paper" row: click one and the desktop
    re-tiles INSTANTLY, click [Save to INI] and it is yours forever.
    All four recolour with the active theme, and all four are free
    at runtime thanks to the cached background.
  * THE MENU WEARS THE BRAND: a Windows-95-style vertical stripe
    down the Dominus menu's left edge, CASTALIA stacked letter by
    letter with the /386 mark at its foot.
  * THE SHELL HAS A VOICE: tiny PC-speaker blips for opening a
    window, closing one, minimize, maximize/restore and popping the
    launcher - each a single short tone, all silenced by the Sound
    toggle (or sound=false in the INI).

NEW IN 0.18 - THE SYSTEM ORACLE, MAXIMIZE, AND INSTANT WINDOW RAISES
--------------------------------------------------------------------
  * THE SYSTEM ORACLE.  The diagnostic suite this class of machine
    always deserved - AIDA's spirit, 1991's body.  Eight pages behind
    a sidebar: Overview, CPU, Memory, Video, Disks, DOS/BIOS, Ports
    and Benchmark, every line probed from the metal:
      - CPU class via the EFLAGS AC/ID-bit tests, vendor and family
        via CPUID where it exists, FPU via FNSTSW, and a speed index
        measured against the BIOS tick;
      - true extended memory from the XMS driver itself, EMS pages
        from the EMM, free DOS memory, XMS presence;
      - VESA version and video RAM, BIOS mode, disk volumes with
        sizes, BIOS drive geometry, COM/LPT addresses, mouse driver
        version, BIOS date;
      - and the BENCHMARK: CPU, memory and video, each drawn as the
        classic horizontal bars racing THIS PC against the PC/XT,
        the AT 286 and the 386SX/16.  Find it in the Toolbox, or
        Run... "oracle".  Know thyself - and thy PC.
  * MAXIMIZE.  Every window now carries the full [_][^][X] set: the
    new middle box zooms a window to the whole work area and back
    (drag or resize a maximized window and it simply becomes a normal
    window again).  One more step toward the big environments.
  * INSTANT RAISES.  Clicking a background window used to recompose
    AND re-present the entire screen; now only the three rectangles
    that actually change are copied to VGA (the two windows and the
    taskbar).  It also fixes a subtle stale-screen case when a raise
    was armed as a drag and released without moving.

NEW IN 0.17 - THE KEYBOARD DESKTOP, WINDOW MANAGEMENT, LESS REPAINTING
----------------------------------------------------------------------
  * THE WHOLE DESKTOP DRIVES FROM THE KEYBOARD.  Arrow keys walk the
    icon grid (column by column, the way the icons are laid out) and
    ENTER launches the selection - combined with F1 help, ESC and
    Alt+Tab, the shell is now fully usable without a mouse, exactly
    as promised on a machine whose mouse driver did not load.
  * WINDOW MANAGEMENT, LIKE THE BIG ENVIRONMENTS: F5 CASCADES the open
    windows into a readable stagger, F6 TILES them across the desk
    (panes never shrink below an applet's minimum, so layouts stay
    valid).  Double-click an empty stretch of the taskbar and every
    window drops to its button - instant "show desktop".
  * WINDOWS REMEMBER WHERE THEY WERE.  Close the Calculator in the
    corner you like it in, and it reopens right there, at the size you
    grew it to (per applet, for the session).
  * The taskbar CLOCK IS A BUTTON now: click it and the Clock desk
    accessory (with the calendar) springs out of it.
  * LESS REPAINTING: windows completely hidden behind a higher window
    are no longer composed at all (occlusion culling), so stacked
    windows stop billing you for pixels you cannot see.  The Disk
    Cabinet keeps its own F5 (copy) - the arrangement keys only act
    when the focused window has no use for them.

NEW IN 0.16 - WALLPAPER, A SETTINGS PANEL THAT SAVES, MOVABLE ICONS
-------------------------------------------------------------------
  * WALLPAPER.  Add  wallpaper=ASSETS\ICONS\GLOBE.ICN  to [desktop] in
    CASTALIA.INI and the 32x32 .ICN tiles across the whole desktop, on
    top of the backdrop pattern.  Transparent pixels let the pattern
    show through, and the art recolours with the active theme (switch
    to amber and your globes go amber).  It is composed ONCE into the
    cached background, so it costs literally nothing while you work -
    spectacle with a zero interaction-speed bill.
  * SETTINGS NOW SAVES.  The panel grew a screensaver delay row
    (Off / 1m / 2m / 5m) and a [Save to INI] button: your theme,
    backdrop, animation, sound and screensaver choices are written
    back into CASTALIA.INI - and the writer REWRITES the file line by
    line, so every comment, icon, shortcut and colour override you
    have in there survives untouched.  Only the managed keys change.
  * MOVABLE DESKTOP ICONS.  Press an icon and drag: after a few pixels
    a rubber-band outline follows the pointer, and dropping it on
    another icon swaps the two desktop slots.  Arrange the desk YOUR
    way (a plain click still selects, a double-click still launches).
  * PATIENCE deals from the keyboard: SPACE turns the next stock card,
    so draw-heavy endgames stop being a pixel-hunt.

NEW IN 0.15 - PATIENCE, SETTINGS, AND A MUCH FASTER DESKTOP
-----------------------------------------------------------
  * PATIENCE - the game a graphical environment is judged by.  Full
    klondike solitaire on green felt: seven columns, draw-one stock with
    endless redeals, four foundations, click-to-pick-up / click-to-drop
    (runs move together), double-click sends a card to its foundation,
    and winning pours the traditional cascade of bouncing cards.
  * SETTINGS - a control panel at last.  Click any of the eleven themes
    and the whole desktop CROSS-FADES into it live; switch the backdrop
    (solid / dots / weave / gradient); toggle animations and sound.
    Session-only by design - CASTALIA.INI stays the permanent record.
  * LIGHTS OUT - the classic lamp-toggling puzzle.  Every board is
    generated from a solved position, so every board is solvable.
  * A MUCH FASTER DESKTOP.  Two structural changes:
      - The composed desktop background (backdrop + icons + labels) is
        cached in a spare frame buffer.  Rebuilding the scene is now ONE
        386 dword copy instead of recomputing gradients, icons and text
        every time - at 640x480 this is the difference you can feel.
      - Clicks now declare exactly the damage they cause.  A click in a
        window repaints just that window; an inert click repaints
        NOTHING; only raising / closing / launching repaints the scene.
        Background game ticks present only their own window's footprint.
        (Before: every click recomposed and re-presented the entire
        screen, which is why the desktop felt sluggish.)
    Plus quicker fades, snappier zooms and a shorter boot splash.

NEW IN 0.14 - MORE TO PLAY, MORE TO USE, MORE TO SEE
----------------------------------------------------
  * TWO NEW GAMES in the Arcade:
      Quadrix ... the falling-blocks game.  Seven pieces, a 10x18 well,
                  arrows steer, Up rotates, SPACE slams.  Speeds up
                  every eighth cleared line.
      Depot ..... a warehouse box-pushing puzzle: shove every crate
                  onto its bay.  Ten floors, every one verified
                  solvable by machine before shipping; U undoes a
                  careless shove, R restarts a floor.
  * TWO NEW TOOLS in the Toolbox:
      Stopwatch . big double-height digits with tenths, lap memory,
                  plus a 1-99 minute kitchen timer that beeps and
                  flashes TIME! when it lands.
      Eyes ...... two big cartoon eyes that follow your pointer around
                  the desktop and blink when you least expect it.  The
                  mandatory desk toy.
  * RESIZABLE WINDOWS: drag the hatched grip in a window's bottom-right
    corner.  Windows only grow beyond the size they opened at, so every
    layout stays valid - a roomier Scrap Box, a bigger Serpent pit, a
    taller Disk Cabinet listing.
  * RIGHT-CLICK anywhere on the desktop and the Dominus menu unfurls
    right at the pointer - the fastest launch in DOS.
  * F1 opens a quick-reference Help window from anywhere.
  * The Disk Cabinet learned ASSOCIATIONS: double-click a .TXT (or .ME,
    .DOC, .LOG, .INI, .ASC) and it opens in the Scrap Box; double-click
    a .ICN icon bitmap and it opens on the Sketch Pad easel.
  * A twelfth Light Show effect: FIREWORKS - rockets, gravity, and a
    night sky that swallows the sparks.
  * Re-opening an applet now refreshes its window title, and BUILD.BAT
    (which had fallen behind the file list) builds the whole tree again.

NEW IN 0.13 - FASTER, SMOOTHER, SHOWIER
---------------------------------------
  * Palette fades everywhere: the boot splash fades in from black, the
    desktop cross-fades in after it, launching a DOS program dissolves
    the desktop and fades it back on return, the Light Show and the
    screensaver fade between effects, and quitting fades to black.
    All gated by animations= in CASTALIA.INI, like every effect below.
  * Window zooms got a motion trail and vertical-retrace pacing (the
    same smooth ~1/8s flourish on a 386SX and a fast emulator), and
    they now mean something: a window springs OUT OF the icon, menu
    item or drawer cell you double-clicked, minimizes INTO its own
    taskbar button, and flies back out of that button when restored.
  * The Dominus menu unfurls out of the taskbar, and wraps into columns
    if it would ever outgrow the screen (16 entries used to run off it).
  * WINDOW SHADING: double-click a title bar to roll the window up to
    just its title (and again to unroll) - peek under a window without
    touching your layout.  Rolled-up games politely pause.
  * Two new Light Show effects: a giant SINE SCROLLER and the classic
    TWISTER column - eleven effects now cycle in the show and the
    screensaver.
  * The taskbar clock is alive: it repaints (only its own little well)
    the moment the minute rolls over.  It used to freeze until some
    other repaint happened by.
  * A faster engine under it all: 386 dword blits for both presents
    (Mode 12h presents move 150 KB per frame - now in half the bus
    cycles), planar fills that compute their masks once per rectangle
    instead of per pixel, text drawn a scan line at a time instead of
    a pixel at a time (the single hottest path at 640x480), dithered
    shadows in row strokes, bitmap icons in run-length strokes, and
    the BIOS tick read straight from the BIOS data area instead of
    through an interrupt.
  * And it is a better citizen: when nothing needs drawing, Castalia
    HALTS the CPU until the next interrupt instead of spinning.  An
    idle desktop now uses roughly no CPU - kinder to laptops, emulator
    hosts and the power bill - yet wakes instantly on input, because
    every animation paces off the 18.2 Hz BIOS tick anyway.
  * Bug fixes: the Music Box oscilloscope no longer garbles (a 16-bit
    overflow), the Fractal repaints only freshly computed rows instead
    of all 24 KB per tick, Serpent ends in a PERFECT win instead of
    hanging when you fill the whole board, the Calculator reports
    Error on overflow instead of a silently wrong answer, Total RAM
    no longer wraps on machines with more than 64 MB, windows can no
    longer slide under the taller Mode 12h taskbar, and ~24 KB of icon
    tables moved out of the crowded near-data segment into memory
    allocated from DOS on demand.
