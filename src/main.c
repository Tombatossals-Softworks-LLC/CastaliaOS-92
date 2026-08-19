/* ======================================================================
 * main.c - CASTALIA/386 entry point, event loop and application glue
 * ----------------------------------------------------------------------
 * CASTALIA/386 is a graphical SHELL that runs on top of MS-DOS / FreeDOS.
 * It is NOT an operating system, kernel or bootloader: DOS keeps managing
 * the disk, files and memory; Castalia draws a desktop on the VGA and
 * launches DOS programs.
 *
 * Rendering model (see ARCHITECTURE.TXT):
 *   - The scene (desktop, windows, taskbar, menu) is composed into an
 *     off-screen back buffer and presented in one blit when "dirty".
 *   - The mouse cursor is a software sprite drawn on top of the visible
 *     framebuffer.  When only the mouse moves we repaint just the scene
 *     rectangle it used to cover (from the back buffer) and redraw it -
 *     no full-frame work, so the pointer stays smooth on a 386SX.
 * ====================================================================== */
#include <dos.h>       /* delay                                          */
#include <i86.h>       /* int86 (tick counter for double-click)          */
#include <stdlib.h>    /* getenv (free-memory launch detection)          */
#include <string.h>    /* strcmp/strcpy prototypes (W308 hygiene)        */
#include <stdio.h>
#include "castalia.h"
#include "video.h"
#include "font.h"
#include "mouse.h"
#include "keyboard.h"
#include "ui.h"
#include "config.h"
#include "system.h"
#include "inspect.h"
#include "window.h"
#include "menu.h"
#include "files.h"
#include "desktop.h"
#include "launcher.h"
#include "calc.h"
#include "scrap.h"
#include "clock.h"
#include "paint.h"
#include "drawer.h"
#include "bench.h"
#include "music.h"
#include "puzzle.h"
#include "ttt.h"
#include "mines.h"
#include "reversi.h"
#include "snake.h"
#include "breaker.h"
#include "echo.h"
#include "fractal.h"
#include "charmap.h"
#include "colors.h"
#include "card.h"
#include "group.h"
#include "quadrix.h"
#include "depot.h"
#include "timer.h"
#include "eyes.h"
#include "patience.h"
#include "lights.h"
#include "settings.h"
#include "oracle.h"
#include "peek.h"
#include "agenda.h"
#include "find.h"
#include "picshow.h"
#include "media.h"
#include "lptdac.h"
#include "flic.h"
#include "opl.h"
#include "about.h"
#include "dialog.h"
#include "demo.h"
#include "recent.h"
#include "filedlg.h"
#include "splash.h"
#include "pong.h"
#include "calendar.h"
#include "g2048.h"
#include "corral.h"
#include "typist.h"

static Config g_cfg;
static bool_t g_quit       = FALSE;
static bool_t g_dirty      = TRUE;
static bool_t g_have_mouse = FALSE;

/* Partial-present bookkeeping (reset at the top of each event-loop pass):
   g_win_content = the only change is the focused window's own content
   (an animation tick or a key/click it handled); g_struct_dirty = something
   structural changed (menu, taskbar clock, a click, a drag, a launch) and a
   full-scene redraw is required.  The fast path runs only when content
   changed and nothing structural did. */
static bool_t g_win_content  = FALSE;
static bool_t g_struct_dirty = FALSE;

/* Partial present: a full render_scene() still composes the WHOLE back
   buffer (always correct), but when every change this pass has a known,
   small footprint we copy only those rectangles to VGA instead of the
   entire 64000-byte frame.  Up to PRES_MAX separate rects are kept (a
   window raise touches two windows AND the taskbar - unioning them into
   one box would cover most of the screen); overflow unions into the
   last.  g_full_present is set by any change whose extent is large or
   unknown; if it is set (or nothing marked a rect) we present the whole
   frame - the safe default. */
#define PRES_MAX 4
static Rect   g_pres_list[PRES_MAX];
static int    g_pres_n       = 0;
static bool_t g_full_present = FALSE;

/* Desktop icon drag-and-drop: pressing an icon ARMS a drag; once the
   pointer travels a few pixels with the button held, the icon's cell
   follows it as a rubber-band outline, and dropping it on another icon
   swaps the two desktop slots.  A plain click (no travel) still just
   selects, and a double-click still launches - arming is free. */
static int    g_icon_drag   = -1;      /* icon index armed, or -1          */
static int    g_icon_down_x = 0;       /* where the press happened         */
static int    g_icon_down_y = 0;
static bool_t g_icon_live   = FALSE;   /* outline is following the mouse   */

/* The first-run "Click here to begin" balloon over the Start orb; cleared
   the moment the user clicks or presses a key. */
static bool_t g_start_hint  = TRUE;

static void mark_present(int x, int y, int w, int h)
{
    if (w <= 0 || h <= 0)
        return;
    if (g_pres_n < PRES_MAX) {
        rect_set(&g_pres_list[g_pres_n], x, y, w, h);
        ++g_pres_n;
    } else {
        /* List full.  Unioning the overflow into the last rect could
           balloon a 40x12 clock well into most of the screen and still
           call itself a "partial" present - slower than the whole frame
           AND dishonest about it.  Just present everything. */
        g_full_present = TRUE;
    }
}

/* A structural change of unknown extent: recompose the whole scene and
   present the whole frame.  (The pattern appeared in every third branch
   of the input handlers - one name keeps the damage discipline legible.) */
static void damage_all(void)
{
    g_dirty        = TRUE;
    g_struct_dirty = TRUE;
    g_full_present = TRUE;
}

/* The startup chime.  On an FM chip it is a bright polyphonic arpeggio that
   settles into a major chord; on a bare machine the PC-speaker chime plays
   instead.  Only sounded when sound is enabled (INI sound=). */
static void boot_chime(void)
{
    if (opl_present()) {
        static const int arp[4] = { 60, 64, 67, 72 };   /* C  E  G  C'      */
        unsigned long t;
        int i;
        opl_init();
        for (i = 0; i < 4; ++i) {
            opl_note_on(i, arp[i]);
            t = sys_ticks();
            while (sys_ticks() - t < 2UL) { /* ~110 ms between notes */ }
        }
        t = sys_ticks();
        while (sys_ticks() - t < 9UL) { /* let the chord ring ~0.5 s */ }
        opl_silence();
    } else {
        music_chime();
    }
}

/* ---- small helpers --------------------------------------------------- */

static bool_t streqi(const char *a, const char *b)
{
    while (*a && *b) {
        char ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca = (char)(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = (char)(cb - 'A' + 'a');
        if (ca != cb)
            return FALSE;
        ++a; ++b;
    }
    return (*a == *b) ? TRUE : FALSE;
}

/* BIOS tick counter (~18.2 Hz) for double-click timing.  Read straight
   from the BIOS data area (sys_ticks) - the event loop asks several times
   per pass and the old INT 1Ah round trip was pure overhead. */
static unsigned long get_ticks(void)
{
    return sys_ticks();
}

/* ---- actions --------------------------------------------------------- */

static void render_scene(void);        /* forward */

/* A dashed-outline zoom between two rectangles with a two-deep motion
   trail, drawn straight to the visible frame and erased from the back
   buffer.  Each step waits one vertical retrace, so the zoom is a smooth,
   identical ~120 ms flourish on every machine - a 386SX and a modern
   emulator alike - instead of either a crawl or an invisible blink. */
static void zoom_rect(int fx, int fy, int fw, int fh,
                      int tx, int ty, int tw, int th)
{
    int steps = 6, s;
    Rect t1, t2;                       /* the outline trail (newest first)  */
    int nt = 0;
    for (s = 1; s <= steps; ++s) {
        int x = fx + (tx - fx) * s / steps;
        int y = fy + (ty - fy) * s / steps;
        int w = fw + (tw - fw) * s / steps;
        int h = fh + (th - fh) * s / steps;
        if (w < 2) w = 2;
        if (h < 2) h = 2;
        vid_outline(x, y, w, h, C_WHITE);
        vid_vsync();
        if (nt == 2)                   /* erase the oldest of the trail     */
            vid_restore_outline(t2.x, t2.y, t2.w, t2.h);
        t2 = t1;
        rect_set(&t1, x, y, w, h);
        if (nt < 2) ++nt;
    }
    if (nt == 2)
        vid_restore_outline(t2.x, t2.y, t2.w, t2.h);
    vid_restore_outline(t1.x, t1.y, t1.w, t1.h);
}

/* Where the next window-open zoom should start.  Launch sites (a desktop
   icon, a Dominus menu item, a Drawer or group cell) record their own
   screen rectangle here, so the window visibly springs out of the thing
   that was clicked - not out of an anonymous point in space. */
static Rect   g_zoom_from;
static bool_t g_zoom_from_set = FALSE;

static void zoom_origin(const Rect *r)
{
    g_zoom_from = *r;
    g_zoom_from_set = TRUE;
}

/* Zoom a newly opened window out of its launch origin (or its own centre
   when no origin was recorded).  It is briefly hidden so the backdrop
   under the zoom is the true pre-window scene. */
static void animate_open(int id)
{
    Rect r, from;
    if (!g_cfg.anim_enabled || !wm_window_rect(id, &r)) {
        g_zoom_from_set = FALSE;
        return;
    }
    if (g_zoom_from_set) {
        from = g_zoom_from;
        g_zoom_from_set = FALSE;
    } else {
        rect_set(&from, r.x + r.w / 2 - 4, r.y + r.h / 2 - 4, 8, 8);
    }
    wm_set_min(id, TRUE);
    render_scene();
    vid_present();
    wm_set_min(id, FALSE);
    zoom_rect(from.x, from.y, from.w, from.h, r.x, r.y, r.w, r.h);
}

/* Play a window zoom over the current scene: 0 = close (shrink to the
   centre), 1 = minimize (fly INTO the window's own taskbar button),
   2 = restore (fly OUT of that button back to the window). */
static void animate_action(const Rect *ar, int kind, int bar_i, int bar_n)
{
    Rect b;
    if (!g_cfg.anim_enabled)
        return;
    if (kind == 1 || kind == 2) {
        if (bar_i >= 0)
            desktop_bar_button_rect(bar_i, bar_n, &b);
        else
            rect_set(&b, ar->x + ar->w / 2 - 20, SCREEN_H - TASKBAR_H,
                     40, TASKBAR_H);
        if (kind == 1)
            zoom_rect(ar->x, ar->y, ar->w, ar->h, b.x, b.y, b.w, b.h);
        else
            zoom_rect(b.x, b.y, b.w, b.h, ar->x, ar->y, ar->w, ar->h);
    } else {
        int cx = ar->x + ar->w / 2, cy = ar->y + ar->h / 2;
        zoom_rect(ar->x, ar->y, ar->w, ar->h, cx - 2, cy - 2, 4, 4);
    }
}

/* Open a window centred on the screen, kept above the taskbar. Works for
   either video mode because it uses the runtime SCREEN_W/SCREEN_H. */
static void open_centered(int kind, const char *title, int w, int h)
{
    int sc = font_h() / 8;             /* 1 normally, 2 at 640x480         */
    int x, y, id;
    music_sfx(760, 1);                 /* a soft "window opens" blip       */
    if (sc < 1) sc = 1;
    w *= sc; h *= sc;
    x = (SCREEN_W - w) / 2;
    y = (SCREEN_H - h) / 2;
    if (y + h > SCREEN_H - TASKBAR_H) y = SCREEN_H - TASKBAR_H - h;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    id = wm_open(kind, title, x, y, w, h);
    if (id < 0) {
        /* The six-slot pool is full.  This used to do NOTHING at all -
           the user picked a menu item, or pressed F1, and the shell sat
           there as if the keystroke had been swallowed. */
        dialog_message("Castalia", "Too many windows are open.",
                       "Close one and try again.");
        damage_all();
        return;
    }
    music_sfx(880, 1);                 /* the "window arrives" blip        */
    animate_open(id);
}

/* Show the hourglass while a slow, non-interactive operation runs (a
   hardware probe, the benchmark, a drive scan).  Drawn at once so the wait
   is visible; the arrow returns on the next repaint after the op. */
static void busy_cursor(bool_t on)
{
    if (!g_have_mouse)
        return;
    mouse_set_busy(on);
    if (on) { mouse_erase(); mouse_draw(); }
}

/* After a full-screen takeover (the Light Show, the screensaver, the
   Cinema, the Picture Show) the free DAC window 16..191 holds someone
   else's colours: a GIF wallpaper must reload its palette, and the scene
   cache with it.  Cheap (one decode) and only when a wallpaper is set. */
static void restore_wallpaper(void)
{
    if (g_cfg.wallpaper[0] != '\0')
        desktop_set_wallpaper(g_cfg.wallpaper);
}

/* Leaving the desktop used to throw away whatever was still open: only
   wm_close_id() ever asked about unsaved work, so quitting with a typed
   Scrap Box, a drawn Sketch Pad or an edited Cardfile lost all of it in
   silence.  Ask once per dirty document; FALSE cancels the exit.

   This must stay in step with the close-box prompts in wm_close_id().
   It did not: Settings grew a dirty flag and a prompt of its own AFTER
   this function was written, on the close path only, so a theme picked
   and not saved survived closing the window and died on Shut Down. */
static bool_t ok_to_quit(void)
{
    card_flush();                      /* the deck saves itself, no prompt   */
    /* ...unless it could not.  A clipped deck is refused a write on
       purpose, and a read-only disk refuses one for us; either way the
       flush above is a no-op and this is the last moment to say so. */
    if (card_is_dirty() &&
        dialog_confirm("Cardfile", "The deck could not be saved.",
                       "Leave Castalia anyway?") != DLG_YES)
        return FALSE;
    if (scrap_is_dirty() &&
        dialog_confirm("Scrap Box", "The document has unsaved changes.",
                       "Leave Castalia anyway?") != DLG_YES)
        return FALSE;
    if (paint_is_dirty() &&
        dialog_confirm("Sketch Pad", "The drawing has unsaved changes.",
                       "Leave Castalia anyway?") != DLG_YES)
        return FALSE;
    /* The Agenda writes on every change, so it has no dirty flag - but a
       write it could not make leaves the same hole, and its red banner
       is only visible while its window is. */
    if (agenda_unsaved() &&
        dialog_confirm("Agenda", "AGENDA.TXT could not be updated.",
                       "Leave Castalia anyway?") != DLG_YES)
        return FALSE;
    /* Settings applies LIVE, so the desktop already wears the new theme
       while CASTALIA.INI still holds the old one - the one unsaved state
       that looks saved. */
    if (settings_is_dirty() &&
        dialog_confirm("Settings", "These preferences are not in",
                       "CASTALIA.INI yet.  Leave anyway?") != DLG_YES)
        return FALSE;
    return TRUE;
}

/* COMMAND.COM's own built-ins: no file to find on disk, but perfectly
   legitimate things to type into Run. */
static const char * const far DOS_BUILTINS[] = {
    "cd", "chdir", "cls", "copy", "date", "del", "dir", "echo", "erase",
    "exit", "md", "mkdir", "more", "path", "prompt", "rd", "ren",
    "rename", "rmdir", "set", "time", "type", "ver", "vol"
};
#define DOS_BUILTIN_N ((int)(sizeof(DOS_BUILTINS) / sizeof(DOS_BUILTINS[0])))

static bool_t file_there(const char *dir, const char *name, const char *ext)
{
    char p[128];
    FILE *f;
    int n = 0;
    if (dir != NULL && dir[0] != '\0') {
        while (dir[n] != '\0' && n < (int)sizeof(p) - 20) { p[n] = dir[n]; ++n; }
        if (n > 0 && p[n - 1] != '\\' && p[n - 1] != '/' && p[n - 1] != ':')
            p[n++] = '\\';
    }
    while (*name != '\0' && n < (int)sizeof(p) - 6) p[n++] = *name++;
    while (*ext  != '\0' && n < (int)sizeof(p) - 1) p[n++] = *ext++;
    p[n] = '\0';
    f = fopen(p, "rb");
    if (f == NULL)
        return FALSE;
    fclose(f);
    return TRUE;
}

/* TRUE if DOS could actually run this.  A typo in the Run box used to
   fall straight through to system(): the shell tore itself down to text
   mode, printed "Illegal command: calculater.", and demanded a keypress
   before it would come back.  For a word that names nothing, say so in a
   dialog and stay in the GUI. */
static bool_t command_runnable(const char *command, const char *path)
{
    char tok[64];
    int  i = 0, j;
    bool_t dotted = FALSE;
    while (command[i] == ' ' || command[i] == '\t') ++i;
    for (j = 0; command[i] != '\0' && command[i] != ' ' &&
                command[i] != '\t' && j < (int)sizeof(tok) - 1; ++i, ++j) {
        tok[j] = command[i];
        if (command[i] == '.') dotted = TRUE;
    }
    tok[j] = '\0';
    if (j == 0)
        return FALSE;
    /* A path or a drive letter: let DOS have it, we cannot judge. */
    for (i = 0; tok[i] != '\0'; ++i)
        if (tok[i] == '\\' || tok[i] == '/' || tok[i] == ':')
            return TRUE;
    for (i = 0; i < DOS_BUILTIN_N; ++i)
        if (streqi(tok, DOS_BUILTINS[i]))
            return TRUE;
    if (dotted)
        return file_there(NULL, tok, "") || file_there(path, tok, "");
    return file_there(NULL, tok, ".COM") || file_there(NULL, tok, ".EXE") ||
           file_there(NULL, tok, ".BAT") || file_there(path, tok, ".COM") ||
           file_there(path, tok, ".EXE") || file_there(path, tok, ".BAT");
}

static void execute_command(const char *command, const char *path)
{
    const char *use_path = (path != NULL && path[0] != '\0')
                         ? path : g_cfg.default_path;

    if (command == NULL || command[0] == '\0')
        return;

    if (streqi(command, "fileman")) {
        /* My Computer always opens on the drive-icon root, just like
           Windows 95 - double-click a drive to browse its folders. */
        busy_cursor(TRUE);
        files_open_computer();
        busy_cursor(FALSE);
        open_centered(WIN_FILEMAN, "My Computer", 284, 168);
    } else if (streqi(command, "inspect") || streqi(command, "inspector") ||
               streqi(command, "sysinfo") || streqi(command, "system")) {
        inspect_open();                /* reprobe + (re)start the animation */
        open_centered(WIN_INSPECT, "System Inspector", 300, 186);
    } else if (streqi(command, "bench") || streqi(command, "benchmark")) {
        busy_cursor(TRUE);
        bench_run();
        busy_cursor(FALSE);
        open_centered(WIN_BENCH, "Benchmark", 280, 176);
    } else if (streqi(command, "puzzle") || streqi(command, "fifteen")) {
        puzzle_open();
        open_centered(WIN_PUZZLE, "Fifteen", 164, 188);
    } else if (streqi(command, "ttt") || streqi(command, "tictactoe")) {
        ttt_open();
        open_centered(WIN_TTT, "Tic-Tac-Toe", 156, 178);
    } else if (streqi(command, "mines") || streqi(command, "minefield")) {
        mines_open();
        open_centered(WIN_MINES, "Minefield", 156, 186);
    } else if (streqi(command, "reversi") || streqi(command, "othello")) {
        reversi_open();
        open_centered(WIN_REVERSI, "Reversi", 172, 190);
    } else if (streqi(command, "snake") || streqi(command, "serpent")) {
        snake_open();
        open_centered(WIN_SNAKE, "Serpent", 236, 168);
    } else if (streqi(command, "breaker") || streqi(command, "blocks") ||
               streqi(command, "bricks")) {
        breaker_open();
        open_centered(WIN_BREAKER, "Breaker", 236, 176);
    } else if (streqi(command, "echo") || streqi(command, "memory")) {
        echo_open();
        open_centered(WIN_ECHO, "Echo", 184, 190);
    } else if (streqi(command, "fractal") || streqi(command, "mandelbrot")) {
        fractal_open();
        open_centered(WIN_FRACT, "Fractal", 200, 164);
    } else if (streqi(command, "charmap") || streqi(command, "chars")) {
        charmap_open();
        open_centered(WIN_CHARMAP, "Character Map", 220, 110);
    } else if (streqi(command, "colors") || streqi(command, "palette")) {
        colors_open(g_cfg.theme);
        open_centered(WIN_COLORS, "Colors", 224, 178);
    } else if (streqi(command, "cardfile") || streqi(command, "cards")) {
        card_open();
        open_centered(WIN_CARD, "Cardfile", 224, 172);
    } else if (streqi(command, "quadrix") || streqi(command, "tetra")) {
        quadrix_open();
        open_centered(WIN_QUADRIX, "Quadrix", 178, 172);
    } else if (streqi(command, "depot") || streqi(command, "sokoban")) {
        depot_open();
        open_centered(WIN_DEPOT, "Depot", 208, 172);
    } else if (streqi(command, "stopwatch") || streqi(command, "timer")) {
        timer_open();
        open_centered(WIN_TIMER, "Stopwatch", 204, 156);
    } else if (streqi(command, "eyes")) {
        eyes_open();
        open_centered(WIN_EYES, "Eyes", 150, 112);
    } else if (streqi(command, "help")) {
        /* 288, not 226: at 226 the client held 34 characters and the
           per-window key lines - "Space start/stop, L lap, R reset,
           Enter timer" is 45 - were clipped at the border.  Help is the
           window that can least afford to be cut off. */
        open_centered(WIN_HELP, "Help", 288, 178);
    } else if (streqi(command, "patience") || streqi(command, "solitaire")) {
        patience_open();
        open_centered(WIN_PATIENCE, "Patience", 302, 184);
    } else if (streqi(command, "lights") || streqi(command, "lightsout")) {
        lights_open();
        open_centered(WIN_LIGHTS, "Lights Out", 170, 176);
    } else if (streqi(command, "settings") || streqi(command, "control")) {
        settings_open(&g_cfg);
        open_centered(WIN_SETTINGS, "Settings", 240, 190);
    } else if (streqi(command, "oracle") || streqi(command, "probe") ||
               streqi(command, "aida")) {
        oracle_open();
        open_centered(WIN_ORACLE, "System Oracle", 306, 184);
    } else if (streqi(command, "find") || streqi(command, "search")) {
        find_open();
        open_centered(WIN_FIND, "Find File", 300, 184);
    } else if (streqi(command, "agenda") || streqi(command, "todo")) {
        agenda_open();
        open_centered(WIN_AGENDA, "Agenda", 220, 172);
    } else if (streqi(command, "peek") || streqi(command, "hexview")) {
        static char pkbuf[64] = "";
        if (filedlg("Inspect a file", "*.*", pkbuf,
                    (int)sizeof(pkbuf), FALSE) && pkbuf[0]) {
            if (peek_open_file(pkbuf))
                open_centered(WIN_PEEK, pkbuf, 306, 176);
            else
                dialog_message("Hex Peek", "Could not open", pkbuf);
        }
    } else if (streqi(command, "gramophone") || streqi(command, "media") ||
               streqi(command, "play")) {
        static char plbuf[64] = "";
        /* BOTH formats it plays.  "*.WA?" alone could not reach a .MID
           at all, in an applet whose own error message says "Not a
           WAV/MIDI" - and the Eject button below asked for "*.*" and so
           offered CASTALIA.EXE.  One filter, and the picker takes a
           semicolon-separated list because DOS matches one wildcard per
           findfirst. */
        if (filedlg("Play a sound", "*.WA?;*.MI?", plbuf,
                    (int)sizeof(plbuf), FALSE) && plbuf[0]) {
            if (media_open_file(plbuf))
                open_centered(WIN_MEDIA, "Gramophone", 236, 184);
            else
                dialog_message("Gramophone", "Not a WAV/MIDI", plbuf);
        }
    } else if (streqi(command, "gram")) {
        open_centered(WIN_MEDIA, "Gramophone", 236, 184);
    } else if (streqi(command, "music") || streqi(command, "tunes")) {
        /* Back from the dead.  The Music Box lost its verb in 0.44 and
           kept its draw, its click handler and its tick - main.c has
           been calling music_tick() the whole time, guarded by a
           wm_has_kind(WIN_MUSIC) that could never be true.  An entire
           working applet, unreachable, and FORMATS.TXT went on
           promising the verb for eleven versions. */
        music_open();
        open_centered(WIN_MUSIC, "Music Box", 190, 150);
    } else if (streqi(command, "run")) {
        static char runbuf[64] = "";
        if (dialog_input("Run", "Command or program:", runbuf,
                         (int)sizeof(runbuf)) == DLG_OK && runbuf[0])
            execute_command(runbuf, g_cfg.default_path);
    } else if (streqi(command, "pictures") || streqi(command, "picshow") ||
               streqi(command, "gallery")) {
        picshow_run();
        video_set_theme(g_cfg.theme);
        restore_wallpaper();
    } else if (streqi(command, "cinema") || streqi(command, "movie") ||
               streqi(command, "flic")) {
        flic_play((const char *)0, g_cfg.theme);   /* bundled CINEMA.FLC   */
        restore_wallpaper();
    } else if (streqi(command, "tools") || streqi(command, "toolbox")) {
        int gw, gh;
        group_open(GRP_TOOLS);
        group_window_size(GRP_TOOLS, &gw, &gh);
        open_centered(WIN_GROUP, group_title(GRP_TOOLS), gw, gh);
    } else if (streqi(command, "arcade") || streqi(command, "games")) {
        int gw, gh;
        group_open(GRP_ARCADE);
        group_window_size(GRP_ARCADE, &gw, &gh);
        open_centered(WIN_GROUP, group_title(GRP_ARCADE), gw, gh);
    } else if (streqi(command, "about")) {
        about_open();
        open_centered(WIN_ABOUT, "About Castalia 92", 284, 186);
    } else if (streqi(command, "pong")) {
        pong_open();
        open_centered(WIN_PONG, "Pong", 220, 160);
    } else if (streqi(command, "calendar") || streqi(command, "cal")) {
        calendar_open();
        open_centered(WIN_CAL, "Calendar", 190, 150);
    } else if (streqi(command, "2048") || streqi(command, "merge")) {
        g2048_open();
        open_centered(WIN_G2048, "2048", 160, 172);
    } else if (streqi(command, "lightshow") || streqi(command, "demo") ||
               streqi(command, "demos")    || streqi(command, "effects")) {
        /* The Light Show: 17 full-screen demoscene effects.  It had been
           left with no caller at all - reachable only by idling until the
           screensaver fired - so the best-looking thing in the shell was
           effectively unreleased. */
        demo_run(g_cfg.theme);
        video_set_theme(g_cfg.theme);
        restore_wallpaper();
        damage_all();
    } else if (streqi(command, "corral") || streqi(command, "jezz")) {
        corral_open();
        open_centered(WIN_CORRAL, "Corral", 224, 172);
    } else if (streqi(command, "typist") || streqi(command, "typing")) {
        typist_open();
        open_centered(WIN_TYPIST, "Typing Tutor", 264, 130);
    } else if (streqi(command, "calc")) {
        if (!wm_has_kind(WIN_CALC))
            calc_reset();              /* keep the running total on reopen */
        open_centered(WIN_CALC, "Calculator", 132, 168);
    } else if (streqi(command, "scrap")) {
        open_centered(WIN_SCRAP, "Scrap Box", 224, 164);
    } else if (streqi(command, "clock")) {
        open_centered(WIN_CLOCK, "Clock", 168, 172);
    } else if (streqi(command, "paint") || streqi(command, "sketch")) {
        /* Only start a fresh canvas when there is no Sketch Pad already
           open.  open_centered() merely RAISES an existing window, so
           picking the Sketch Pad a second time used to wipe the drawing
           with no prompt - and because paint_reset() leaves g_dirty set,
           the shell then offered to save the blank canvas it had just
           made.  Same for the Calculator's running total below. */
        if (!wm_has_kind(WIN_PAINT))
            paint_reset();
        open_centered(WIN_PAINT, "Sketch Pad", 200, 180);
    } else if (streqi(command, "drawer") || streqi(command, "programs")) {
        int dw, dh;
        drawer_open(&g_cfg);
        drawer_window_size(drawer_entry_count(), &dw, &dh);
        open_centered(WIN_DRAWER, "Program Drawer", dw, dh);
    } else if (streqi(command, "recentclear")) {
        recent_clear();                /* Start > Documents > Clear the list */
    } else if (streqi(command, "exit")) {
        /* "Shut Down..." - the ellipsis promises a dialog, and a stray
           Esc on the desktop already asks.  Ending the session from the
           menu with no question at all was the odd one out. */
        if (dialog_confirm("Shut Down", "Leave Castalia and return to DOS?",
                           NULL) == DLG_YES && ok_to_quit())
            g_quit = TRUE;
        damage_all();                  /* the prompt sat on top of the scene */
    } else if (streqi(command, "bsod") || streqi(command, "crash")) {
        bsod_show();                   /* the blue-screen easter egg          */
        damage_all();                  /* then bring the desktop back         */
    } else if (!command_runnable(command, use_path)) {
        /* Nothing of that name to run.  Answer in the GUI instead of
           dropping to text mode to print "Illegal command". */
        dialog_message("Run", "No such command or program:", command);
    } else {
        /* External DOS program. */
        launcher_run(use_path, command, g_cfg.theme);
        kb_flush();
        /* The INT 33h driver kept counting presses while the child ran;
           drain them or they replay as desktop clicks on return. */
        mouse_update();
        (void)mouse_take_lpresses();
        (void)mouse_take_rpresses();
    }
    g_dirty = TRUE;
    g_zoom_from_set = FALSE;           /* never leak an origin to a later open */
}

/* Every verb execute_command() handles itself.  This used to be 91
   hand-written streqi() comparisons duplicating execute_command's own
   literals, and the two drifted apart twice: a [shortcut] with
   freemem=true naming a verb missing from the list UNLOADED the shell
   and handed the word to DOS, so "find" ran DOS's FIND.EXE.  One
   list now, and ci/consistency.sh fails the build if it ever stops
   matching the verbs execute_command actually compares against. */
static const char * const far INTERNAL_VERBS[] = {
    "2048", "about", "agenda", "aida", "arcade", "bench", "benchmark",
    "blocks", "breaker", "bricks", "bsod", "cal", "calc", "calendar",
    "cardfile", "cards", "charmap", "chars", "cinema", "clock", "colors",
    "control", "corral", "crash", "demo", "demos", "depot", "drawer", "echo",
    "effects", "exit", "eyes", "fifteen", "fileman", "find", "flic",
    "fractal", "gallery", "games", "gram", "gramophone", "help",
    "hexview", "inspect", "inspector", "jezz", "lights", "lightshow",
    "lightsout", "mandelbrot", "media", "memory", "merge", "minefield",
    "mines", "movie", "music", "oracle", "othello", "paint", "palette",
    "patience", "peek", "picshow", "pictures", "play", "pong", "probe",
    "programs", "puzzle", "quadrix", "recentclear", "reversi", "run",
    "scrap", "search",
    "serpent", "settings", "sketch", "snake", "sokoban", "solitaire",
    "stopwatch", "sysinfo", "system", "tetra", "tictactoe", "timer",
    "todo", "toolbox", "tools", "ttt", "tunes", "typing", "typist"
};
#define INTERNAL_N ((int)(sizeof(INTERNAL_VERBS) / sizeof(INTERNAL_VERBS[0])))

static bool_t is_internal(const char *c)
{
    int i;
    for (i = 0; i < INTERNAL_N; ++i)
        if (streqi(c, INTERNAL_VERBS[i]))
            return TRUE;
    return FALSE;
}

/* Launch a configured entry (from the Dominus menu or the Program Drawer).
   A freemem=true entry that names an EXTERNAL program, when Castalia was
   started through the CASTSHEL.BAT wrapper, is run by UNLOADING the shell:
   we write CASTRUN.BAT and quit, and the wrapper runs the program with all
   of conventional memory and relaunches us.  In every other case (internal
   verb, freemem off, or no wrapper) we use the normal resident launch. */
static void launch_entry(const char *command, const char *path, bool_t freemem)
{
    if (command == NULL || command[0] == '\0')
        return;
    if (freemem && !is_internal(command) && getenv("CASTSHEL") != NULL) {
        /* This unloads the shell, so it loses unsaved work exactly like a
           quit does - ask with the same voice. */
        if (!ok_to_quit()) {
            damage_all();
            return;
        }
        if (!launcher_write_runfile(path, command)) {
            dialog_message("Run", "Could not write CASTRUN.BAT.",
                           "Disk full, or write-protected.");
            damage_all();
            return;                    /* do NOT close the desktop        */
        }
        g_quit  = TRUE;
        g_dirty = TRUE;
        return;
    }
    execute_command(command, path);
}

/* Case-insensitive test of a file name's extension. */
static bool_t has_ext(const char *name, const char *ext)
{
    int dot = -1, i;
    for (i = 0; name[i] != '\0'; ++i)
        if (name[i] == '.')
            dot = i;
    if (dot < 0)
        return FALSE;
    return streqi(name + dot + 1, ext);
}

/* Join directory + file name into path[] (cap includes the NUL). */
static void doc_path(char *path, int cap, const char *cwd, const char *name)
{
    int i = 0, j = 0;
    while (cwd[i] != '\0' && i < cap - 2) {
        path[i] = cwd[i];
        ++i;
    }
    if (i > 0 && path[i - 1] != '\\')
        path[i++] = '\\';
    while (name[j] != '\0' && i < cap - 1)
        path[i++] = name[j++];
    path[i] = '\0';
}

/* Which applet opens `name`: the INI [assoc] table first (EXT=applet),
   then the built-in defaults.  "dos" hands the file to DOS; anything
   unclaimed lands in the Hex Peek, so a double click always answers. */
static const char *assoc_app_for(const char *name)
{
    int i, dot = -1;
    for (i = 0; name[i] != '\0'; ++i)
        if (name[i] == '.')
            dot = i;
    if (dot >= 0) {
        for (i = 0; i < g_cfg.assoc_count; ++i)
            if (streqi(name + dot + 1, g_cfg.assoc[i].ext))
                return g_cfg.assoc[i].app;
    }
    if (has_ext(name, "txt") || has_ext(name, "doc") || has_ext(name, "me") ||
        has_ext(name, "log") || has_ext(name, "ini") || has_ext(name, "asc"))
        return "scrap";
    if (has_ext(name, "icn"))                          return "paint";
    if (has_ext(name, "fli") || has_ext(name, "flc"))  return "cinema";
    if (has_ext(name, "wav") || has_ext(name, "mid") ||
        has_ext(name, "midi"))                         return "gram";
    if (has_ext(name, "exe") || has_ext(name, "com") ||
        has_ext(name, "bat"))                          return "dos";
    return "peek";
}

/* Open one document by folder + 8.3 name, through the association table.
   Split out of do_fileman_launch so Find File can reach it: the Cabinet
   owned the only route to "open this file in the right applet", and the
   search results had no way in. */
static void open_document(const char *cwd, const char *cmd)
{
    const char *app = assoc_app_for(cmd);
    char path[132];

    if (strcmp(app, "dos") == 0) {          /* executables spawn         */
        launcher_run(cwd, cmd, g_cfg.theme);
        kb_flush();
        mouse_update();                     /* drain presses accrued     */
        (void)mouse_take_lpresses();        /* while the child ran       */
        (void)mouse_take_rpresses();
        files_rescan();
        g_dirty = TRUE;
        return;
    }

    /* Remember it for Start > Documents.  Executables are launches, not
       documents, so they are noted above this point deliberately - the
       menu is for things you were WORKING on. */
    recent_note(cwd, cmd);

    doc_path(path, (int)sizeof(path), cwd, cmd);
    if (strcmp(app, "scrap") == 0) {
        /* Opening a .TXT from the Cabinet replaces the buffer, so it has
           to ask the same question the New and Load buttons do - this
           path threw unsaved typing away without a word. */
        if (!scrap_ok_to_replace()) { damage_all(); return; }
        scrap_open(path);
        open_centered(WIN_SCRAP, cmd, 224, 164);
    } else if (strcmp(app, "paint") == 0) {
        if (!paint_ok_to_replace()) { damage_all(); return; }
        paint_open_file(path);
        open_centered(WIN_PAINT, "Sketch Pad", 200, 180);
    } else if (strcmp(app, "cinema") == 0) {
        flic_play(path, g_cfg.theme);
        restore_wallpaper();                /* the player drives the DAC */
    } else if (strcmp(app, "gram") == 0) {
        if (media_open_file(path))
            open_centered(WIN_MEDIA, "Gramophone", 236, 184);
    } else {                                /* "peek", and any unknown   */
        if (peek_open_file(path))
            open_centered(WIN_PEEK, cmd, 306, 176);
    }
    g_dirty = TRUE;
}

static void do_fileman_launch(void)
{
    open_document(files_cwd(), files_launch_command());
}

/* Launch the entry the Program Drawer just chose.  Routing through
   execute_command() means a drawer entry behaves exactly like the same
   command from the Dominus menu - internal verb or external program. */
static void do_drawer_launch(void)
{
    Rect fr;
    if (drawer_take_launch_rect(&fr))
        zoom_origin(&fr);              /* zoom the applet out of its cell   */
    launch_entry(drawer_launch_command(), drawer_launch_path(),
                 drawer_launch_freemem());
}

/* Open the applet a Toolbox / Arcade group just chose (always internal). */
static void do_group_launch(void)
{
    Rect fr;
    if (group_take_launch_rect(&fr))
        zoom_origin(&fr);              /* zoom the applet out of its cell   */
    execute_command(group_launch_command(), "");
}

/* ---- rendering ------------------------------------------------------- */

static void render_scene(void)
{
    desktop_draw();
    wm_draw_all();
    desktop_draw_taskbar(menu_is_open());
    if (g_start_hint && !menu_is_open())
        desktop_draw_start_hint();
    menu_draw();
}

/* The union of everything this pass is going to blit, grown by the dither
   drop-shadow margin (a window's shadow falls outside its own rect, and
   the callers mark the rect).  FALSE when there is nothing to union. */
static bool_t present_union(Rect *u)
{
    int i, m = 3 * ui_scale() + 1;
    int x0, y0, x1, y1;
    if (g_pres_n <= 0)
        return FALSE;
    x0 = g_pres_list[0].x;
    y0 = g_pres_list[0].y;
    x1 = x0 + g_pres_list[0].w;
    y1 = y0 + g_pres_list[0].h;
    for (i = 1; i < g_pres_n; ++i) {
        const Rect *r = &g_pres_list[i];
        if (r->x < x0) x0 = r->x;
        if (r->y < y0) y0 = r->y;
        if (r->x + r->w > x1) x1 = r->x + r->w;
        if (r->y + r->h > y1) y1 = r->y + r->h;
    }
    x0 -= m; y0 -= m; x1 += m; y1 += m;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > SCREEN_W) x1 = SCREEN_W;
    if (y1 > SCREEN_H) y1 = SCREEN_H;
    rect_set(u, x0, y0, x1 - x0, y1 - y0);
    return (u->w > 0 && u->h > 0) ? TRUE : FALSE;
}

/* Compose only what is about to be blitted.  A full render_scene() is
   always correct but always costs the same: desktop_draw() alone restores
   64000 bytes from the scene cache, and then every window is re-composed
   on top - which the Inspector paid six times a second and the taskbar
   clock once a minute, to change a few hundred pixels.

   Fencing the primitives to the union means the cache restore is a few
   rows, the windows outside it draw nothing, and the ones inside it write
   only the covered part.  The back buffer outside the union keeps last
   pass's pixels, which are still correct: the same union governs what gets
   blitted, so anything not in it did not change. */
static void render_scene_clipped(const Rect *u)
{
    vid_set_clip(u->x, u->y, u->w, u->h);
    vid_cache_restore_rect(u->x, u->y, u->w, u->h);
    desktop_draw_over_cache();
    wm_draw_all();
    if (u->y + u->h > TASKBAR_Y)
        desktop_draw_taskbar(menu_is_open());
    if (g_start_hint && !menu_is_open())
        desktop_draw_start_hint();
    menu_draw();
    vid_clear_clip();
}

/* ---- mouse event dispatch ------------------------------------------- */

/* Open the Dominus menu with its bottom-left corner at an anchor (the
   launcher button, or wherever the right mouse button was clicked), and
   unfurl it: compose the scene (menu included) into the back buffer, then
   reveal the menu in vsync-paced slices rising out of the anchor.  Pure
   blits of already-composed pixels - cheap. */
static void open_menu_at(int anchor_x, int anchor_bottom)
{
    music_sfx(1000, 1);                /* the launcher "click"             */
    menu_open(g_cfg.shortcuts, g_cfg.shortcut_count, anchor_x, anchor_bottom);
    if (g_cfg.anim_enabled) {
        Rect mr;
        int s, n = 4;
        render_scene();
        menu_bounds(&mr);
        mouse_erase();
        for (s = 1; s <= n; ++s) {
            int hh = (int)((long)mr.h * s / n);
            vid_blit_rect(mr.x, mr.y + mr.h - hh, mr.w, hh);
            vid_vsync();
        }
        mouse_draw();
    }
}

/* A window's content animated (a game tick, the clock, the stopwatch).
   Focused: repaint just that window through the fast path.  Visible but
   not focused: recompose the scene (cheap now - the desktop restores from
   the cache) but PRESENT only the window's own footprint.  Minimized:
   nothing on screen changes, so do not even mark the scene dirty. */
static void tick_present(int kind)
{
    Rect r;
    if (wm_top_kind() == kind) {
        g_dirty = TRUE;
        g_win_content = TRUE;
        return;
    }
    if (wm_kind_rect(kind, &r) == 1) {
        g_dirty = TRUE;
        g_struct_dirty = TRUE;
        mark_present(r.x, r.y, r.w, r.h);
    }
}

/* Every branch sets exactly the damage it causes: a click on a focused
   window's content repaints just that window; raising, closing, shading
   or launching repaints the scene; a click that changes nothing repaints
   nothing at all.  (This used to be a blanket full-redraw+full-present on
   EVERY click - the single biggest cause of interaction lag.) */
static void on_left_down(int mx, int my, bool_t dbl)
{
    g_dirty = TRUE;
    g_icon_drag = -1;                  /* any press re-decides the icon drag */

    if (menu_is_open()) {
        Rect mb;
        const CfgShortcut *sc;
        menu_bounds(&mb);              /* footprint BEFORE it closes       */
        sc = menu_click(mx, my);
        if (sc != NULL) {
            Rect fr;
            damage_all();              /* a launch follows: full repaint   */
            if (menu_take_click_rect(&fr))
                zoom_origin(&fr);      /* window springs out of the item   */
            /* A Documents entry is a FILE, not a verb or a program: it
               goes to the same association route the Disk Cabinet uses,
               not to execute_command (which would look for a program
               called NOTES.TXT and rightly not find one). */
            if (menu_result_is_document())
                open_document(sc->path, sc->command);
            else
                launch_entry(sc->command, sc->path, sc->freemem);
        } else if (menu_is_open()) {
            /* A submenu header was clicked: it expanded and the menu stays
               open - recompose so the new submenu is drawn. */
            damage_all();
        } else {
            /* Dismissed: only the menu's own footprint (and the released
               launcher button) changed on screen. */
            g_dirty = TRUE;
            g_struct_dirty = TRUE;
            mark_present(mb.x, mb.y, mb.w, mb.h);
            mark_present(0, TASKBAR_Y, SCREEN_W, TASKBAR_H);
        }
        return;
    }

    {
        Rect   pre_top;
        bool_t had_top = wm_top_rect(&pre_top);
        bool_t on_top  = wm_point_on_top(mx, my);
        int r = wm_press(mx, my, dbl);
        if (r == WM_LAUNCH || r == WM_LAUNCH_PROG || r == WM_LAUNCH_GROUP) {
            damage_all();
            if (r == WM_LAUNCH)           do_fileman_launch();
            else if (r == WM_LAUNCH_PROG) do_drawer_launch();
            else                          do_group_launch();
            return;
        }
        if (oracle_poll_damage() || wm_poll_retitled()) {
            /* The Oracle's benchmark paints its video sub-tests in absolute
               screen coordinates, so the whole scene has to be rebuilt -
               a window-only repaint would leave the colour bands standing
               over every window that was not itself dirty. */
            damage_all();
            return;
        }
        if (r == WM_REDRAW && on_top && !dialog_took_over()) {
            g_win_content = TRUE;      /* content click: window-only paint */
            return;
        }
        /* A background window came forward (raise only, or raise plus a
           content click it handled).  The full scene recomposes, but only
           three rectangles actually changed on screen: the old top window
           (its title bar dimmed), the raised window, and the taskbar (the
           active button moved) - blit those, not the whole frame. */
        if (r == WM_RAISED ||
            (r == WM_REDRAW && !on_top && !dialog_took_over())) {
            Rect nt;
            g_struct_dirty = TRUE;
            if (had_top)
                mark_present(pre_top.x, pre_top.y, pre_top.w, pre_top.h);
            if (wm_top_rect(&nt))
                mark_present(nt.x, nt.y, nt.w, nt.h);
            mark_present(0, TASKBAR_Y, SCREEN_W, TASKBAR_H);
            return;
        }
        if (r == WM_NONE && wm_dragging()) {
            g_dirty = FALSE;           /* drag/resize armed: outline soon  */
            return;
        }
        if (r == WM_NONE && on_top) {
            g_dirty = FALSE;           /* inert click: nothing changed     */
            return;
        }
        if (r != WM_MISS) {
            damage_all();     /* close/minimize/shade/maximize/...   */
            return;
        }
    }

    /* The click missed every window: taskbar / desktop. */
    if (desktop_launcher_hit(mx, my)) {
        int ax, ab;
        Rect mr;
        g_dirty = TRUE;
        g_struct_dirty = TRUE;
        desktop_launcher_anchor(&ax, &ab);
        open_menu_at(ax, ab);
        /* Only the menu appeared (and the launcher button pressed in):
           blit those two rectangles, not the whole frame. */
        menu_bounds(&mr);
        mark_present(mr.x, mr.y, mr.w, mr.h);
        mark_present(0, TASKBAR_Y, SCREEN_W, TASKBAR_H);
        return;
    }

    {
        int b = desktop_taskbar_button_at(mx, my);
        if (b >= 0) {
            damage_all();
            wm_bar_click(b);        /* restore / minimize / raise a window  */
            return;
        }
    }

    /* The rest of the taskbar: the clock is a button in disguise (it
       opens the Clock desk accessory), and double-clicking an empty
       stretch of the bar clears the whole desk ("show desktop"). */
    if (my >= TASKBAR_Y) {
        Rect cr, sr;
        /* The tray speaker toggles the sound (and its red mute slash). */
        desktop_tray_speaker_rect(&sr);
        if (g_cfg.clock_enabled && rect_contains(&sr, mx, my)) {
            g_cfg.sound_enabled = g_cfg.sound_enabled ? FALSE : TRUE;
            music_set_sfx(g_cfg.sound_enabled);
            if (g_cfg.sound_enabled)
                music_sfx(880, 1);     /* a small confirming blip          */
            damage_all();
            return;
        }
        desktop_clock_rect(&cr);
        if (g_cfg.clock_enabled && rect_contains(&cr, mx, my)) {
            damage_all();
            zoom_origin(&cr);          /* the window springs out of it     */
            execute_command("clock", g_cfg.default_path);
            return;
        }
        if (dbl && wm_any_open()) {
            damage_all();
            wm_minimize_all();
            return;
        }
        g_dirty = FALSE;               /* single click on the bare bar     */
        return;
    }

    {
        int icon = desktop_icon_at(mx, my);
        int old  = desktop_selected();
        if (icon >= 0) {
            desktop_select(icon);
            if (!dbl) {                /* arm a possible drag-to-rearrange  */
                g_icon_drag   = icon;
                g_icon_down_x = mx;
                g_icon_down_y = my;
                g_icon_live   = FALSE;
            }
            if (dbl) {
                Rect fr;
                damage_all();
                desktop_cell_rect(icon, &fr);
                zoom_origin(&fr);      /* window springs out of the icon   */
                execute_command(desktop_icon_command(icon), g_cfg.default_path);
            } else if (old != icon) {
                /* Single-click highlight: only two icon cells change, so
                   blit just those instead of the whole desktop. */
                Rect r;
                g_struct_dirty = TRUE;
                desktop_cell_rect(icon, &r);
                mark_present(r.x, r.y, r.w, r.h);
                if (old >= 0) {
                    desktop_cell_rect(old, &r);
                    mark_present(r.x, r.y, r.w, r.h);
                }
            } else {
                g_dirty = FALSE;       /* clicked the already-selected icon */
            }
        } else if (old >= 0) {
            Rect r;                                /* deselect: repaint one cell */
            desktop_select(-1);
            g_struct_dirty = TRUE;
            desktop_cell_rect(old, &r);
            mark_present(r.x, r.y, r.w, r.h);
        } else {
            g_dirty = FALSE;           /* empty desktop: nothing changed   */
        }
    }
}

/* ---- keyboard event dispatch ---------------------------------------- */

static void on_key(int key)
{
    if (key == KEY_ESC) {
        if (menu_is_open()) {
            Rect mb;
            menu_bounds(&mb);
            menu_close();
            g_dirty = TRUE;
            g_struct_dirty = TRUE;
            mark_present(mb.x, mb.y, mb.w, mb.h);
            mark_present(0, TASKBAR_Y, SCREEN_W, TASKBAR_H);
            return;
        }
        if (wm_any_open()) {
            wm_close_top();
        } else {
            /* ESC on the bare desktop used to drop straight to DOS with no
               confirmation - one stray keypress and the session was over. */
            if (dialog_confirm("Exit Castalia",
                               "Leave the desktop and", "return to DOS?")
                == DLG_YES && ok_to_quit())
                g_quit = TRUE;
        }
        damage_all();
        return;
    }

    if (key == KEY_BACKTAB || key == KEY_ALTTAB) {   /* cycle the windows  */
        wm_cycle();
        damage_all();
        return;
    }

    if (key == KEY_F1) {                    /* the quick-reference window    */
        menu_close();
        help_set_context(wm_top_kind());    /* answer "what can I do HERE"  */
        execute_command("help", "");
        damage_all();
        return;
    }

    /* The menu is up: the keyboard drives it (it used to swallow every
       key instead).  A launched leaf runs exactly like a mouse pick. */
    if (menu_is_open()) {
        const CfgShortcut *sc = NULL;
        if (menu_key(key, &sc)) {
            if (sc != NULL) {
                Rect mr;
                if (menu_take_click_rect(&mr))
                    zoom_origin(&mr);
                /* A Documents entry is a FILE, not a verb or a program: it
               goes to the same association route the Disk Cabinet uses,
               not to execute_command (which would look for a program
               called NOTES.TXT and rightly not find one). */
            if (menu_result_is_document())
                open_document(sc->path, sc->command);
            else
                launch_entry(sc->command, sc->path, sc->freemem);
            }
            damage_all();
        }
        return;
    }

    /* F10 (the classic DOS menu key) opens the Dominus menu from the
       keyboard; Ctrl+Esc too where the BIOS reports it. */
    if (key == KEY_F10 || key == KEY_CTRLESC) {
        int ax, ab;
        desktop_launcher_anchor(&ax, &ab);
        open_menu_at(ax, ab);
        damage_all();
        return;
    }

    /* No visible window has the focus: the keyboard drives the DESKTOP.
       Arrows walk the icon grid, ENTER launches - the whole shell is
       usable without ever touching the mouse. */
    if (wm_top_kind() < 0) {
        int launch = -1;
        int old = desktop_selected();
        if (desktop_key(key, &launch)) {
            if (launch >= 0) {
                Rect fr;
                damage_all();
                desktop_cell_rect(launch, &fr);
                zoom_origin(&fr);      /* window springs out of the icon   */
                execute_command(desktop_icon_command(launch),
                                g_cfg.default_path);
            } else {
                /* Selection moved: blit just the two changed cells. */
                Rect r;
                g_dirty = TRUE;
                g_struct_dirty = TRUE;
                desktop_cell_rect(desktop_selected(), &r);
                mark_present(r.x, r.y, r.w, r.h);
                if (old >= 0 && old != desktop_selected()) {
                    desktop_cell_rect(old, &r);
                    mark_present(r.x, r.y, r.w, r.h);
                }
            }
            return;
        }
    }

    {
        int r = wm_key(key);
        if (r == WM_LAUNCH || r == WM_LAUNCH_PROG || r == WM_LAUNCH_GROUP) {
            damage_all();
            if (r == WM_LAUNCH)           do_fileman_launch();
            else if (r == WM_LAUNCH_PROG) do_drawer_launch();
            else                          do_group_launch();
        } else if (r == WM_STRUCT) {
            /* A keypress that changed more than its own window - Settings
               switching the theme, the backdrop or the wallpaper. */
            damage_all();
        } else if (r == WM_REDRAW) {
            g_dirty = TRUE;
            if (dialog_took_over() ||      /* Load/Save box painted over all */
                oracle_poll_damage() ||    /* benchmark scribbled everywhere */
                wm_poll_retitled()) {      /* the title BAR, not the client */
                damage_all();
            } else {
                g_win_content = TRUE;      /* a key the focused window handled */
            }
        } else if ((key == KEY_F5 || key == KEY_F6) && wm_any_open()) {
            /* Nobody claimed the key (the Disk Cabinet keeps its own
               F5 = copy): arrange the windows across the desk. */
            if (key == KEY_F5)
                wm_cascade();
            else
                wm_tile();
            damage_all();
        }
    }
}

/* ---- the event loop -------------------------------------------------- */

/* Move the rubber-band outline to nr: erase the previous one (if any and
   if it moved), draw the new one straight on the visible frame, remember
   it.  Shared by window move/resize drags and desktop icon drags. */
static void track_outline(const Rect *nr, Rect *cur, bool_t *shown)
{
    if (*shown && nr->x == cur->x && nr->y == cur->y &&
        nr->w == cur->w && nr->h == cur->h)
        return;
    if (*shown)
        vid_restore_outline(cur->x, cur->y, cur->w, cur->h);
    mouse_erase();
    vid_outline(nr->x, nr->y, nr->w, nr->h, C_WHITE);
    mouse_draw();
    *cur = *nr;
    *shown = TRUE;
}

static void event_loop(void)
{
    int prev_buttons = 0;
    int prev_mx = -1, prev_my = -1;
    unsigned long last_click_tick = 0;
    unsigned long last_clock_tick = 0;
    unsigned long last_inspect_tick = 0;
    unsigned long last_bench_tick = 0;
    unsigned long last_min_poll = 0;
    int shown_minute = -1;             /* taskbar clock's displayed minute  */
    int last_cx = -99, last_cy = -99;
    Rect outline_r;                    /* current drag outline rectangle   */
    bool_t dragging_shown = FALSE;     /* is an outline currently drawn?    */
    unsigned long last_activity = get_ticks();  /* for the idle screensaver */
    int act_mx = -1, act_my = -1;

    while (!g_quit) {
        int mx, my, buttons, key;
        int topk;                      /* wm_top_kind(), read once a pass  */

        g_win_content  = FALSE;        /* reset the partial-present flags   */
        g_struct_dirty = FALSE;
        g_pres_n       = 0;
        g_full_present = FALSE;

        if (g_have_mouse)
            mouse_update();
        mx = mouse_x();
        my = mouse_y();
        buttons = mouse_buttons();

        /* Keyboard.  DRAINED, not one per pass.
           One key per compose is plenty at Mode 13h speeds on anything
           modern, and loses characters on the hardware this shell is
           actually for: a compose in Mode 12h on a 386SX is slow enough
           that a typist outruns the frame rate, the fifteen-key BIOS
           buffer fills, and the BIOS discards the rest with a beep.
           Measured under DOSBox at cycles=1100, which is about a
           386SX/16: "typed on a 386" arrived in the Scrap Box as
           "typed on ".
           Bounded rather than a bare while: hold a key down and the
           BIOS auto-repeat refills the buffer as fast as it is drained,
           and an unbounded loop would never get back to composing.
           SIXTEEN, because the BIOS buffer holds fifteen: a cap that
           empties it leaves nothing behind for the next pass, where a
           smaller one just moves the backlog.  Eight was the first
           guess and still lost characters - "the quick brown fox jumps
           over the lazy dog" arrived as "the quick brown fox jumpover
           thedog" - because the keys arrive faster than a pass that
           only takes eight of them.
           Batching is also cheaper than the old behaviour - several
           edits now share one repaint - and it is safe with the
           partial-present flags, because the window-only fast path
           needs !g_struct_dirty and damage_all() sets that, so the
           strongest damage any of the keys asked for is the one that
           wins. */
        {
            int drained = 0;
            key = KEY_NONE;
            while (drained < 16) {
                int k = kb_poll();
                if (k == KEY_NONE)
                    break;
                key = k;           /* the last one, for the idle test */
                on_key(k);
                ++drained;
                if (g_quit)
                    break;
            }
        }

        /* Track activity for the idle screensaver (before any 'continue'). */
        if (key != KEY_NONE || buttons != 0 || mx != act_mx || my != act_my) {
            last_activity = get_ticks();
            act_mx = mx; act_my = my;
        }

        /* The "Click here to begin" balloon vanishes on the first real
           interaction (a click or a key) - a clean full repaint erases it. */
        if (g_start_hint && (key != KEY_NONE || buttons != 0)) {
            g_start_hint = FALSE;
            damage_all();
        }

        /* Mouse buttons.  Clicks are driven off the driver's hardware press
           counter (mouse_take_lpresses), NOT the polled button STATE: a full
           repaint can keep us from polling for tens of ms, during which a
           whole press+release could be missed by edge detection - which is
           why double-clicks used to need several tries.  The counter never
           loses a press, so every click and double-click now registers. */
        if (g_have_mouse) {
            int    np      = mouse_take_lpresses();
            bool_t held    = (buttons & MB_LEFT) != 0;
            bool_t left_up = !held && (prev_buttons & MB_LEFT);

            if (np > 0) {
                unsigned long now = get_ticks();
                /* Two presses in one poll is unambiguously a double-click;
                   otherwise fall back to the time+distance window (forgiving:
                   ~0.6 s and +/-8 px, since a real mouse drifts between the
                   two clicks). */
                bool_t dbl = (np >= 2) ? TRUE : FALSE;
                if (!dbl && now - last_click_tick <= 11 &&
                    (mx - last_cx) > -9 && (mx - last_cx) < 9 &&
                    (my - last_cy) > -9 && (my - last_cy) < 9)
                    dbl = TRUE;
                last_click_tick = now;
                last_cx = mx;
                last_cy = my;
                on_left_down(mx, my, dbl);
                /* A quick click that was already released before we polled
                   can leave a title-bar drag (or corner resize) armed; ask
                   the driver for the CURRENT state - the state cached at the
                   top of this pass predates the press and used to cancel a
                   drag the instant it was armed. */
                if (wm_dragging() && !mouse_left_now())
                    wm_release();
            }
            if (left_up) {
                Rect oldr, newr;
                bool_t bounded = wm_drag_bounds(&oldr, &newr);   /* pre-release */
                if (dragging_shown) {          /* erase the last drag outline  */
                    vid_restore_outline(outline_r.x, outline_r.y,
                                        outline_r.w, outline_r.h);
                    dragging_shown = FALSE;
                }
                if (wm_release()) {    /* committed a move -> repaint once */
                    g_dirty = TRUE;
                    g_struct_dirty = TRUE;
                    /* Only the window's old and new footprints changed
                       (plus the taskbar, whose active button may have
                       moved if the drag also raised the window) - blit
                       those instead of the whole screen. */
                    if (bounded) {
                        mark_present(oldr.x, oldr.y, oldr.w, oldr.h);
                        mark_present(newr.x, newr.y, newr.w, newr.h);
                        mark_present(0, TASKBAR_Y, SCREEN_W, TASKBAR_H);
                    }
                }
                /* Icon drop: released over another icon's cell -> swap the
                   two desktop slots and rebuild (the INI itself changes
                   only if the user saves from Settings). */
                if (g_icon_drag >= 0) {
                    int src = g_icon_drag;
                    int dst = wm_over_window(mx, my)
                                  ? -1 : desktop_icon_at(mx, my);
                    bool_t moved = g_icon_live;
                    g_icon_drag = -1;
                    g_icon_live = FALSE;
                    if (moved) {
                        if (dst >= 0 && dst != src) {
                            CfgIcon tmp = g_cfg.icons[src];
                            g_cfg.icons[src] = g_cfg.icons[dst];
                            g_cfg.icons[dst] = tmp;
                            /* Swap the loaded art in memory - no disk. */
                            desktop_swap_bitmaps(src, dst);
                            desktop_select(dst);   /* it lands selected      */
                        }
                        damage_all();
                    }
                }
            }

            /* Right-click on the bare desktop: the Dominus menu appears
               right where the pointer is - the fastest way to launch. */
            {
                int nr = mouse_take_rpresses();
                if (nr > 0 && !menu_is_open() && !wm_dragging()) {
                    if (wm_over_window(mx, my)) {
                        /* Inside a window the right button used to do
                           nothing at all, anywhere in the shell.  Offer it
                           to the window first (Minefield flags with it,
                           the way Minesweeper always has). */
                        int rr = wm_rpress(mx, my);
                        if (rr == WM_REDRAW || rr == WM_RAISED) {
                            /* WM_RAISED means the stack was reordered:
                               without a repaint the screen would keep
                               showing the old z-order. */
                            g_dirty = TRUE;
                            g_struct_dirty = TRUE;
                            g_full_present = TRUE;
                        }
                    } else {
                        Rect mr;
                        g_dirty = TRUE;
                        g_struct_dirty = TRUE;
                        open_menu_at(mx, my);
                        menu_bounds(&mr);
                        mark_present(mr.x, mr.y, mr.w, mr.h);
                        /* the launcher shows pressed while it is open */
                        mark_present(0, TASKBAR_Y, SCREEN_W, TASKBAR_H);
                    }
                }
            }

            /* Window drag: a rubber-band OUTLINE follows the mouse and the
               window itself moves only on release, so we never repaint the
               whole scene mid-drag.  That is the fast, period-correct way
               to drag on a 386SX.  Drawn straight to the visible frame. */
            if ((buttons & MB_LEFT) && wm_dragging()) {
                Rect nr;
                wm_drag(mx, my);
                wm_drag_rect(&nr);
                track_outline(&nr, &outline_r, &dragging_shown);
                prev_mx = mx; prev_my = my;
                prev_buttons = buttons;
                continue;              /* skip the full-scene present path */
            }

            /* Desktop icon drag: the same rubber-band treatment as a
               window drag - only the outline moves until the drop. */
            if ((buttons & MB_LEFT) && g_icon_drag >= 0 && !wm_dragging()) {
                if (!g_icon_live) {
                    int dx = mx - g_icon_down_x, dy = my - g_icon_down_y;
                    if (dx > 3 || dx < -3 || dy > 3 || dy < -3)
                        g_icon_live = TRUE;   /* travelled enough: it's a drag */
                }
                if (g_icon_live) {
                    Rect nr;
                    desktop_cell_rect(g_icon_drag, &nr);
                    nr.x += mx - g_icon_down_x;
                    nr.y += my - g_icon_down_y;
                    track_outline(&nr, &outline_r, &dragging_shown);
                    prev_mx = mx; prev_my = my;
                    prev_buttons = buttons;
                    continue;          /* skip the full-scene present path */
                }
            }
            dragging_shown = FALSE;

            /* A held button over the Sketch Pad canvas paints free-hand.
               The cell is drawn straight into the back buffer, so we blit
               ONLY that cell - no whole-scene repaint per painted pixel. */
            if (buttons & MB_LEFT) {
                Rect pr;
                if (wm_content_drag(mx, my, &pr)) {
                    mouse_erase();
                    vid_blit_rect(pr.x, pr.y, pr.w, pr.h);
                    mouse_draw();
                    prev_mx = mx; prev_my = my;
                    prev_buttons = buttons;
                    continue;              /* handled: skip the full present */
                }
            }

            /* Menu hover feedback: redraw only the menu and blit just its
               footprint, so sweeping the mouse down the items is instant
               instead of repainting the whole desktop on every step.  When a
               cascade opens or closes the footprint grows/shrinks - the old
               submenu pixels must be recomposed from the desktop, so we fall
               through to a full repaint in that case instead of a fast blit. */
            /* Only when the POINTER ACTUALLY MOVED.  menu_hover ran on
               every pass, so it reset the highlight to whatever row the
               stationary pointer happened to sit on microseconds after
               menu_key had moved it - F10 then Down did nothing at all,
               and the shell's primary launcher was mouse-only.  Same
               clobber-every-frame shape as drawer.c's follow_sel. */
            if (menu_is_open() && (mx != prev_mx || my != prev_my)) {
                Rect before, after;
                menu_bounds(&before);
                if (menu_hover(mx, my)) {
                    menu_bounds(&after);
                    if (after.x == before.x && after.y == before.y &&
                        after.w == before.w && after.h == before.h) {
                        mouse_erase();
                        menu_draw();
                        vid_blit_rect(after.x, after.y, after.w, after.h);
                        mouse_draw();
                        prev_mx = mx; prev_my = my;
                        prev_buttons = buttons;
                        continue;          /* footprint unchanged: fast blit  */
                    }
                    damage_all();          /* cascade opened/closed: full frame */
                }
            }
        }

        /* Window close / minimize / restore zoom - played over the still-
           current back buffer, then the repaint drops (or shows) it. */
        {
            Rect ar; int akind, abar, abarn;
            if (wm_take_anim(&ar, &akind, &abar, &abarn)) {
                /* Event sounds, Windows-95 style: a falling blip when a
                   window closes or minimizes, a rising one on restore. */
                music_sfx((akind == 2) ? 660 : (akind == 1) ? 494 : 392, 1);
                animate_action(&ar, akind, abar, abarn);
                g_full_present = TRUE;     /* a window came or went: full frame */
            }
            /* A maximize / restore: fly the frame outline between the old
               and new geometry, the way the minimize zoom flies to the bar. */
            {
                Rect mzf, mzt;
                if (wm_take_maxzoom(&mzf, &mzt)) {
                    if (g_cfg.anim_enabled)
                        zoom_rect(mzf.x, mzf.y, mzf.w, mzf.h,
                                  mzt.x, mzt.y, mzt.w, mzt.h);
                    g_full_present = TRUE;
                }
            }
        }

        /* Idle screensaver: after the configured idle time, fade out, run
           the Light Show until any key or mouse activity, and fade the
           desktop back in (the fade-in rides the present below).  The BIOS
           tick resets at midnight; treat a backward jump as fresh activity
           or the unsigned difference would fire the saver mid-keystroke. */
        if (get_ticks() < last_activity)
            last_activity = get_ticks();
        if (g_cfg.screensaver_secs > 0 && !menu_is_open() &&
            get_ticks() - last_activity >
                (unsigned long)g_cfg.screensaver_secs * 18UL) {
            demo_screensaver(g_cfg.theme, g_have_mouse);
            last_activity = get_ticks();
            act_mx = mouse_x(); act_my = mouse_y();
            restore_wallpaper();  /* the saver drove DAC 16..191      */
            damage_all();         /* whole screen was the Light Show  */
        }

        /* Keep an open Clock applet ticking (~2x/second): focused, the
           fast path repaints just its window; visible below others, the
           scene recomposes (cheap - cached desktop) and only the clock's
           own footprint is presented. */
        if (wm_has_kind(WIN_CLOCK)) {
            unsigned long now = get_ticks();
            if (now - last_clock_tick >= 9) {
                last_clock_tick = now;
                tick_present(WIN_CLOCK);
            }
        }

        /* The System Inspector animates too: its gauges charge up on open,
           the oscilloscope sweeps and the LED clock ticks (~6x/second). */
        if (wm_has_kind(WIN_INSPECT)) {
            unsigned long now = get_ticks();
            if (now - last_inspect_tick >= 3) {
                last_inspect_tick = now;
                tick_present(WIN_INSPECT);
            }
        }

        /* The Benchmark's result bars charge up for ~0.5 s after a run;
           only spend repaints while that animation is actually running. */
        if (wm_has_kind(WIN_BENCH) && bench_animating()) {
            unsigned long now = get_ticks();
            if (now - last_bench_tick >= 3) {
                last_bench_tick = now;
                tick_present(WIN_BENCH);
            }
        }

        /* Live taskbar clock: when the minute rolls over, recompose and
           blit JUST the clock well.  (It used to freeze at its boot-time
           reading until some unrelated repaint happened by.) */
        if (g_cfg.clock_enabled) {
            unsigned long nowt = get_ticks();
            if (nowt - last_min_poll >= 18UL) {
                struct dostime_t tt;
                last_min_poll = nowt;
                _dos_gettime(&tt);
                if ((int)tt.minute != shown_minute) {
                    Rect cr;
                    shown_minute = (int)tt.minute;
                    g_dirty = TRUE;
                    g_struct_dirty = TRUE;
                    desktop_clock_rect(&cr);
                    mark_present(cr.x, cr.y, cr.w, cr.h);
                }
            }
        }

        /* Animation ticks.  An animating window's own tick is a content-only
           change - it can take the partial-present fast path below unless
           something structural also happened this pass. */
        if (wm_has_kind(WIN_MUSIC)) {
            bool_t adv = music_tick();     /* keeps playing even minimized */
            if (adv || (music_is_playing() && wm_top_kind() == WIN_MUSIC))
                tick_present(WIN_MUSIC);
        } else if (music_is_playing()) {
            music_stop();
        }
        /* The Gramophone's MIDI melody advances one note at a time, and its
           piano roll scrolls with it - a content-only change. */
        if (wm_has_kind(WIN_MEDIA)) {
            if (media_poll_open()) {   /* Eject: load a new file, in place    */
                /* The picker, not a bare field.  Every other path into
                   the Gramophone already used it - the "play" verb three
                   hundred lines up does - but its own EJECT button still
                   asked the user to type a path from memory, which is
                   the exact thing filedlg was written to end. */
                static char plbuf[CFG_PATH_LEN] = "";
                if (filedlg("Play a sound", "*.WA?;*.MI?", plbuf,
                            (int)sizeof(plbuf), FALSE) && plbuf[0]) {
                    if (!media_open_file(plbuf))
                        dialog_message("Gramophone", "Not a WAV/MIDI", plbuf);
                }
                damage_all();
            } else if (media_poll_folder()) {  /* + FOLDER: build a playlist */
                /* The folder picker, not a text field asking the user to
                   remember a path - which is what this had been doing
                   since the picker was written. */
                static char fdir[CFG_PATH_LEN] = "";
                if (filedlg_folder("Add a folder of tunes",
                                   fdir, (int)sizeof(fdir)))
                    media_add_folder(fdir);
                damage_all();
            } else if (media_tick(wm_top_kind() == WIN_MEDIA ? TRUE : FALSE)) {
                tick_present(WIN_MEDIA);
            }
        } else if (media_is_playing()) {
            media_stop();
        }
        /* Find File asked for a pattern: pop the modal input, then sweep
           the drive under the hourglass (same poll pattern as Eject). */
        if (wm_has_kind(WIN_FIND) && find_poll_prompt()) {
            static char fpat[13] = "*.EXE";
            if (dialog_input("Find File", "Pattern (e.g. *.TXT):", fpat,
                             (int)sizeof(fpat)) == DLG_OK && fpat[0]) {
                busy_cursor(TRUE);
                find_run(fpat, find_text());
                busy_cursor(FALSE);
            }
            damage_all();
        }
        /* The other half: text a match must CONTAIN.  Seeded with the
           current filter, so Enter keeps it and a cleared field drops
           back to a plain name search - the field is on the panel either
           way, because a filter you cannot see is a filter that makes
           the applet look broken. */
        if (wm_has_kind(WIN_FIND) && find_poll_tprompt()) {
            static char ftxt[24];
            strcpy(ftxt, find_text());
            if (dialog_input("Find File", "Containing text:", ftxt,
                             (int)sizeof(ftxt)) == DLG_OK) {
                busy_cursor(TRUE);
                find_run(find_pattern(), ftxt);
                busy_cursor(FALSE);
            }
            damage_all();
        }
        /* ...and Enter on a result opens it, through the very same
           association route the Disk Cabinet uses. */
        if (wm_has_kind(WIN_FIND)) {
            char fdir[96], fnam[16];
            if (find_poll_open(fdir, (int)sizeof(fdir),
                               fnam, (int)sizeof(fnam))) {
                open_document(fdir, fnam);
                damage_all();
            } else if (find_poll_goto(fdir, (int)sizeof(fdir))) {
                /* F4: the other half of finding something.  Enter opens
                   the file; this puts the Disk Cabinet in the folder
                   holding it, so you can rename, copy or delete it
                   without navigating there by hand. */
                busy_cursor(TRUE);
                files_open(fdir);
                busy_cursor(FALSE);
                open_centered(WIN_FILEMAN, "My Computer", 284, 168);
                damage_all();
            }
        }
        /* Every self-animating applet, from the window table.  This was
           ten hand-written lines, each re-asking wm_top_kind(). */
        wm_tick_all(tick_present);

        /* One wm_top_kind() for the whole tick region.  It walks the
           z-order, and the block below used to call it six more times a
           pass to re-ask a question whose answer cannot change here. */
        topk = wm_top_kind();

        /* Breaker runs (and takes mouse steering) only while it is the top
           window - focusing another window pauses the ball. */
        if (topk == WIN_BREAKER) {
            Rect bcl;
            if (wm_top_client_rect(&bcl)) {
                if (breaker_tick(&bcl)) tick_present(WIN_BREAKER);
                if (g_have_mouse && mx != prev_mx &&
                    breaker_mouse(&bcl, mx)) tick_present(WIN_BREAKER);
            }
        }
        /* Pong: the rally runs off the BIOS tick; the player's paddle
           follows the mouse's height. */
        if (topk == WIN_PONG) {
            Rect pcl;
            if (wm_top_client_rect(&pcl)) {
                if (pong_tick(&pcl)) tick_present(WIN_PONG);
                if (g_have_mouse && my != prev_my &&
                    pong_mouse(&pcl, my)) tick_present(WIN_PONG);
            }
        }
        /* Corral: balls and the growing fence run only while it is the
           top window (Breaker's politeness rule); the pointer feeds the
           aiming guide. */
        if (topk == WIN_CORRAL) {
            Rect ccl;
            if (wm_top_client_rect(&ccl)) {
                if (corral_tick(&ccl)) tick_present(WIN_CORRAL);
                if (g_have_mouse && (mx != prev_mx || my != prev_my) &&
                    corral_mouse(&ccl, mx, my)) tick_present(WIN_CORRAL);
            }
        }
        /* Dragging the Disk Cabinet's scroll thumb: while the button is
           held the thumb follows the pointer; release (or losing focus)
           lets go.  Presented via the fast path - list-only repaints. */
        if (files_thumb_active()) {
            if (topk != WIN_FILEMAN || !g_have_mouse ||
                (mouse_buttons() & 1) == 0)
                files_thumb_end();
            else if (files_thumb_drag(my))
                tick_present(WIN_FILEMAN);
        }
        /* Settings: pointing at a theme swatch lights the live preview
           strip (a content-only repaint through the fast path). */
        if (topk == WIN_SETTINGS && g_have_mouse &&
            (mx != prev_mx || my != prev_my) && settings_mouse(mx, my))
            tick_present(WIN_SETTINGS);
        /* The Eyes follow the pointer (and blink) while they are focused. */
        if (topk == WIN_EYES) {
            Rect ecl;
            if (wm_top_client_rect(&ecl)) {
                if (g_have_mouse && (mx != prev_mx || my != prev_my) &&
                    eyes_mouse(&ecl, mx, my))
                    tick_present(WIN_EYES);
            }
            if (eyes_tick())
                tick_present(WIN_EYES);
        }
        music_sfx_service();          /* silence a finished game sound effect  */

        /* Present. */
        if (g_dirty) {
            int tk = topk;
            Rect tr;
            /* Fast path: the only change is an animating top window's own
               content (a tick or a key/click it handled) and nothing
               structural.  Repaint JUST that window - no full-scene redraw,
               so the gradient desktop is not recomputed - and blit only its
               footprint.  This keeps direction changes and ticks snappy. */
            if (g_win_content && !g_struct_dirty && !menu_is_open() &&
                !wm_dragging() && wm_top_rect(&tr)) {
                Rect cl, d;
                /* Serpent and Breaker change only a few cells per frame,
                   and the Fractal only the strip of rows it just computed -
                   redraw and blit just those.  Everything else repaints the
                   whole window (still cheaper than a full-scene present). */
                if (tk == WIN_SNAKE && wm_top_client_rect(&cl) &&
                    snake_step_draw(&cl, &d)) {
                    vid_blit_rect(d.x, d.y, d.w, d.h);
                } else if (tk == WIN_BREAKER && wm_top_client_rect(&cl) &&
                           breaker_step_draw(&cl, &d)) {
                    vid_blit_rect(d.x, d.y, d.w, d.h);
                } else if (tk == WIN_FRACT && wm_top_client_rect(&cl) &&
                           fractal_step_draw(&cl, &d)) {
                    vid_blit_rect(d.x, d.y, d.w, d.h);
                } else if (wm_draw_top_client(&cl)) {
                    /* Nothing outside the client can have changed on a
                       content tick, so leave the chrome standing: the
                       dither shadow, the face fill, the raised bevel, the
                       gradient title bar, the badge, the three caption
                       boxes and the resize grip all survive untouched.
                       That is most of a window's pixels, skipped 18x/s. */
                    vid_blit_rect(cl.x, cl.y, cl.w, cl.h);
                } else {
                    wm_draw_top();
                    vid_blit_rect(tr.x, tr.y, tr.w, tr.h);
                }
                if (g_have_mouse) { mouse_erase(); mouse_draw(); }
            } else {
                Rect pu;
                if (g_pres_n > 0 && !g_full_present && desktop_cache_ready() &&
                    present_union(&pu)) {
                    /* Blit only the changed rectangles.  A full vid_present
                       would overwrite the old cursor; a partial blit will
                       not, so erase it from the (freshly composed) back
                       buffer first. */
                    int pi;
                    render_scene_clipped(&pu);
                    if (g_have_mouse)
                        mouse_erase();
                    for (pi = 0; pi < g_pres_n; ++pi)
                        vid_blit_rect(g_pres_list[pi].x, g_pres_list[pi].y,
                                      g_pres_list[pi].w, g_pres_list[pi].h);
                } else {
                    render_scene();        /* always a correct full compose  */
                    vid_present();
                }
                if (g_have_mouse)
                    mouse_draw();
            }
            g_dirty = FALSE;
            prev_mx = mx;
            prev_my = my;
            /* Just presented while blacked out (returning from a launch,
               the Light Show or the screensaver): reveal it with a fade. */
            if (video_is_dark())
                video_fade_in();
        } else if (g_have_mouse && (mx != prev_mx || my != prev_my)) {
            /* Only the pointer moved: cheap erase + redraw. */
            mouse_erase();
            mouse_draw();
            prev_mx = mx;
            prev_my = my;
        }

        prev_buttons = buttons;

        /* Nothing to draw and nothing pending: halt the CPU until the next
           hardware interrupt.  Every animation in the shell paces off the
           18.2 Hz BIOS tick and input raises its own IRQ, so this costs no
           latency at all - it just stops the idle desktop from burning
           100% CPU (and the laptop battery, and the emulator host core). */
        if (!g_quit && !g_dirty)
            sys_idle();
    }
}

/* ---- entry point ----------------------------------------------------- */

int main(void)
{
    /* 0. Remember where we live: data files (INI, agenda, high scores,
       the gallery) stay anchored here no matter where the Disk Cabinet
       later wanders. */
    sys_capture_home();

    /* 1. Configuration (falls back to built-in defaults).  Read it from our
       OWN directory, not the current one: INSTALL.BAT puts Castalia on the
       PATH, so launching it from anywhere else used to read no INI at all
       while Settings kept saving to the home copy - every preference the
       user set came back forgotten on the next boot. */
    config_defaults(&g_cfg);
    {
        char inip[132];
        sys_home_path(inip, (int)sizeof(inip), "CASTALIA.INI");
        config_load(inip, &g_cfg);
    }
    recent_load();                     /* Start > Documents, from last run */
    lptdac_config(g_cfg.lptdac, g_cfg.lptport);   /* declared LPT DAC, if any */

    /* Fail not-ready drives gracefully instead of hanging the GUI. */
    crit_error_install();

    /* Minimum-system gate: 1 MB of RAM - the Amiga-500-class baseline this
       project targets.  Checked in text mode so the message is readable. */
    {
        unsigned long have = system_total_ram_kb();
        if (have < CAST_MIN_RAM_KB) {
            printf("\n%s  -  %s\n%s\n\n", CAST_NAME, CAST_TAGLINE, CAST_COMPANY);
            printf("Not enough memory.\n\n");
            printf("%s needs at least %u KB of RAM;\n",
                   CAST_NAME, (unsigned)CAST_MIN_RAM_KB);
            printf("this machine reports %lu KB.\n\n", have);
            printf("(1 MB is the baseline - an Amiga 500 reached it with the\n");
            printf(" classic 512 KB slow expansion.  Castalia aims that high.)\n\n");
            return 2;
        }
    }

    /* 2. Graphics (mode from CASTALIA.INI: mode13h or mode12h). */
    if (!video_init(g_cfg.video)) {
        fprintf(stderr,
                "CASTALIA/386: could not allocate the video back buffer.\n"
                "Mode 13h needs ~64 KB free; Mode 12h needs ~150 KB.\n");
        return 1;
    }
    video_enable_fades(g_cfg.anim_enabled);
    video_blackout();                  /* boot fades in from black          */
    video_set_overrides(g_cfg.color_set, g_cfg.color_rgb);
    video_set_theme(g_cfg.theme);

    /* Font / icon scale: big (8x16) at 640x480, normal (8x8) at 320x200. */
    font_init();

    /* 3. Boot splash (uses the 8x8 face for the logo; the full gradient
       version runs in Mode 13h, a clean panel in Mode 12h).  It fades in
       from black, and the desktop cross-fades in after it. */
    splash_show();
    video_fade_out();

    if (video_is_big()) { font_set_big(TRUE);  ui_set_scale(2); }
    else                { font_set_big(FALSE); ui_set_scale(1); }

    /* 4. Input + subsystems. */
    if (g_cfg.mouse_enabled)
        g_have_mouse = mouse_init();
    if (g_have_mouse)
        mouse_set_bounds(0, 0, SCREEN_W, SCREEN_H);

    system_set_mouse(g_have_mouse);
    system_gather();

    music_set_sfx(g_cfg.sound_enabled);   /* game sound effects (INI sound=)  */

    wm_init();
    desktop_init(&g_cfg);

    /* 5. Run.  Fade the desktop in, then sound a short startup chime. */
    g_dirty = TRUE;
    render_scene();
    vid_present();
    if (g_have_mouse)
        mouse_draw();
    video_fade_in();
    if (g_cfg.sound_enabled)
        boot_chime();
    event_loop();

    /* 6. Shutdown: silence the speaker, show the Windows-95 farewell screen
       until a key is pressed, then fade to black, restore text mode and free
       memory. */
    music_stop();
    if (g_have_mouse)
        mouse_hide();
    splash_shutdown();
    video_fade_out();
    video_shutdown();

    printf("CASTALIA/386 - desktop closed. Back at DOS.\n");
    printf("Tombatossals Softworks.\n");
    return 0;
}
