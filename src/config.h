/* ======================================================================
 * config.h - CASTALIA.INI loader for CASTALIA/386
 * ----------------------------------------------------------------------
 * A tiny, forgiving INI reader.  Everything has a built-in default, so
 * the shell runs correctly even if CASTALIA.INI is missing or partial.
 *
 * Recognised layout (see CASTALIA.INI for a full example):
 *
 *   [system]
 *   theme=classic           ; classic | penumbra | bureau | winsteel
 *   video=mode13h           ; informational for the MVP (only 13h exists)
 *   default_path=C:\        ; starting directory for the Disk Cabinet
 *
 *   [mouse]
 *   enabled=true
 *
 *   [clock]
 *   enabled=true
 *
 *   [desktop]
 *   icon1_name=Disk Cabinet
 *   icon1_command=fileman
 *   ... up to icon8 ...
 *
 *   [shortcut]              ; repeatable - each block adds a Dominus entry
 *   name=DOS Editor
 *   path=C:\DOS
 *   command=EDIT.COM
 *
 * "command" values are either an internal verb (fileman, sysinfo, about,
 * exit) or an external DOS command line (COMMAND.COM, EDIT.COM, A.BAT).
 * ====================================================================== */
#ifndef CONFIG_H
#define CONFIG_H

#include "castalia.h"

#define CFG_MAX_ICONS     8
#define CFG_MAX_SHORTCUTS 16
#define CFG_NAME_LEN      24
#define CFG_CMD_LEN       64
#define CFG_PATH_LEN      64

/* Icon path: long enough for "ASSETS\ICONS\PROGRAMS.ICO" (a full 8.3 name
   in a two-level subdirectory) plus the terminator - the old [24] silently
   truncated the longer .ICO paths so those icons failed to load. */
#define CFG_ICON_LEN      40

typedef struct {
    char name[CFG_NAME_LEN];
    char command[CFG_CMD_LEN];
    char icon[CFG_ICON_LEN];       /* optional .ICN/.ICO path; "" = none  */
} CfgIcon;

typedef struct {
    char   name[CFG_NAME_LEN];
    char   path[CFG_PATH_LEN];
    char   command[CFG_CMD_LEN];
    char   icon[CFG_ICON_LEN];     /* optional .ICN/.ICO path; "" = none  */
    bool_t freemem;                /* unload Castalia for this program?   */
} CfgShortcut;

/* One [assoc] row: extension (no dot) -> the applet that opens it. */
#define CFG_MAX_ASSOC 12
typedef struct {
    char ext[5];
    char app[9];
} CfgAssoc;

typedef struct {
    char   theme[16];
    char   video[12];
    char   default_path[CFG_PATH_LEN];
    bool_t mouse_enabled;
    bool_t clock_enabled;
    bool_t anim_enabled;           /* window zoom animations                 */
    bool_t sound_enabled;          /* startup chime                          */
    int    screensaver_secs;       /* idle seconds before the Light Show; 0=off */

    /* Parallel-port DAC for the Gramophone's WAV playback ([system]
       lptdac=covox|dss + lptport=1..3); "off" when unset.  Declared, not
       probed - poking bytes at a printer prints garbage. */
    char   lptdac[8];
    int    lptport;

    /* The Start button's caption ([system] startlabel=).  "Inicio" by
       default; the taskbar sizes the button to fit. */
    char   startlabel[12];

    CfgIcon icons[CFG_MAX_ICONS];
    int     icon_count;

    CfgShortcut shortcuts[CFG_MAX_SHORTCUTS];
    int         shortcut_count;

    /* Per-slot palette overrides from the [colors] section. */
    u8     color_set[16];           /* non-zero = slot overridden          */
    u8     color_rgb[16 * 3];       /* R,G,B per slot (0..255)             */

    char   pattern[12];             /* desktop pattern: solid|dots|grid|weave */
    char   wallpaper[32];           /* .ICN tiled over the desktop; "" = none */

    /* The Program Drawer's optional scan directory ([drawer] scan=DIR):
       its EXE, COM and BAT files join the [shortcut] entries. */
    char drawer_scan[CFG_PATH_LEN];

    /* File associations from [assoc]: EXT=applet.  Applets that can open
       a document: scrap | paint | gram | cinema | peek | dos.  Entries
       here take precedence over the built-in defaults (main.c). */
    CfgAssoc assoc[CFG_MAX_ASSOC];
    int      assoc_count;
} Config;

/* Populate cfg with the built-in defaults (a usable desktop). */
void   config_defaults(Config *cfg);

/* Overlay settings from filename onto cfg. Returns TRUE if the file was
   opened and read, FALSE if it was missing (defaults are kept). */
bool_t config_load(const char *filename, Config *cfg);

/* Write the LIVE settings (theme, backdrop, animations, sound, saver)
   back into filename.  The file is rewritten line by line so comments,
   ordering, icons, shortcuts and colour overrides all survive; only the
   managed keys change (and are appended if missing).  A missing file is
   created from scratch.  Returns TRUE on success. */
bool_t config_save(const char *filename, const Config *cfg);

#endif /* CONFIG_H */
