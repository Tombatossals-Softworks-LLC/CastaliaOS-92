/* ======================================================================
 * config.c - CASTALIA.INI loader for CASTALIA/386
 * ====================================================================== */
#include <stdio.h>
#include <string.h>
#include "config.h"

/* ---- small string helpers ------------------------------------------- */

/* Trim leading and trailing whitespace in place; return start pointer. */
static char *trim(char *s)
{
    char *end;
    while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n')
        ++s;
    end = s + strlen(s);
    while (end > s) {
        char c = end[-1];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n')
            --end;
        else
            break;
    }
    *end = '\0';
    return s;
}

static void lower(char *s)
{
    for (; *s; ++s)
        if (*s >= 'A' && *s <= 'Z')
            *s = (char)(*s - 'A' + 'a');
}

static void copy_field(char *dst, const char *src, int max)
{
    int i = 0;
    while (src[i] != '\0' && i < max - 1) {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = '\0';
}

static bool_t parse_bool(const char *v)
{
    /* Reject the negative words FIRST.  The permissive 'o' test below is
       there for "on", but it also swallowed "off" - so sound=off,
       animations=off and clock=off all read as TRUE, and "off" is exactly
       what the shipped INI's own lptdac=off invites a user to write. */
    if ((v[0] == 'o' || v[0] == 'O') && (v[1] == 'f' || v[1] == 'F'))
        return FALSE;
    if (v[0] == 'n' || v[0] == 'N' || v[0] == 'f' || v[0] == 'F' ||
        v[0] == '0')
        return FALSE;
    if (v[0] == '1' || v[0] == 't' || v[0] == 'T' ||
        v[0] == 'y' || v[0] == 'Y' || v[0] == 'o' || v[0] == 'O')
        return TRUE;
    return FALSE;
}

static int parse_int(const char *v)
{
    int n = 0;
    while (*v >= '0' && *v <= '9') {
        if (n > 3276) return 32767;    /* saturate: n*10 would wrap 16-bit */
        n = n * 10 + (*v - '0');
        ++v;
    }
    return n;
}

/* Map a [colors] key to a palette slot (0..15), or -1 if unknown. */
static int color_slot(const char *name)
{
    if (strcmp(name, "black")    == 0) return 0;
    if (strcmp(name, "title")    == 0) return 1;
    if (strcmp(name, "desktop")  == 0) return 2;
    if (strcmp(name, "face")     == 0) return 3;
    if (strcmp(name, "shadow")   == 0) return 4;
    if (strcmp(name, "hilight")  == 0) return 5;
    if (strcmp(name, "white")    == 0) return 6;
    if (strcmp(name, "ltblue")   == 0) return 7;
    if (strcmp(name, "red")      == 0) return 8;
    if (strcmp(name, "yellow")   == 0) return 9;
    if (strcmp(name, "dkyellow") == 0) return 10;
    if (strcmp(name, "green")    == 0) return 11;
    if (strcmp(name, "dkgray")   == 0) return 12;
    if (strcmp(name, "blue")     == 0) return 13;
    if (strcmp(name, "cyan")     == 0) return 14;
    if (strcmp(name, "cream")    == 0) return 15;
    return -1;
}

/* Parse "R,G,B" (0..255 each). Returns TRUE on success. */
static bool_t parse_rgb(const char *v, u8 *r, u8 *g, u8 *b)
{
    int rr = 0, gg = 0, bb = 0, field = 0, val = 0, any = 0;
    while (*v) {
        if (*v >= '0' && *v <= '9') { val = val * 10 + (*v - '0'); any = 1; }
        else if (*v == ',') {
            if (field == 0) rr = val; else if (field == 1) gg = val;
            ++field; val = 0;
        }
        ++v;
    }
    if (field >= 2) bb = val; else return FALSE;
    if (!any) return FALSE;
    *r = (u8)(rr > 255 ? 255 : rr);
    *g = (u8)(gg > 255 ? 255 : gg);
    *b = (u8)(bb > 255 ? 255 : bb);
    return TRUE;
}

/* ---- defaults -------------------------------------------------------- */
void config_defaults(Config *cfg)
{
    memset(cfg, 0, sizeof(*cfg));

    strcpy(cfg->theme, "classic");
    strcpy(cfg->video, "mode13h");
    strcpy(cfg->default_path, "C:\\");
    strcpy(cfg->pattern, "solid");
    cfg->mouse_enabled = TRUE;
    cfg->clock_enabled = TRUE;
    cfg->anim_enabled  = TRUE;
    cfg->sound_enabled = TRUE;
    cfg->screensaver_secs = 90;
    strcpy(cfg->lptdac, "off");
    cfg->lptport = 1;
    strcpy(cfg->startlabel, "Inicio");

    /* Four desktop icons that work out of the box. */
    strcpy(cfg->icons[0].name, "My Computer");
    strcpy(cfg->icons[0].command, "fileman");
    strcpy(cfg->icons[1].name, "Command Room");
    strcpy(cfg->icons[1].command, "COMMAND.COM");
    strcpy(cfg->icons[2].name, "System Inspector");
    strcpy(cfg->icons[2].command, "inspect");
    strcpy(cfg->icons[3].name, "Program Drawer");
    strcpy(cfg->icons[3].command, "drawer");
    strcpy(cfg->icons[4].name, "About Castalia");
    strcpy(cfg->icons[4].command, "about");
    cfg->icon_count = 5;

    /* Default Dominus launcher menu. */
    strcpy(cfg->shortcuts[0].name, "My Computer");
    strcpy(cfg->shortcuts[0].command, "fileman");
    strcpy(cfg->shortcuts[1].name, "Command Room");
    strcpy(cfg->shortcuts[1].command, "COMMAND.COM");
    strcpy(cfg->shortcuts[2].name, "System Inspector");
    strcpy(cfg->shortcuts[2].command, "inspect");
    strcpy(cfg->shortcuts[3].name, "Calculator");
    strcpy(cfg->shortcuts[3].command, "calc");
    strcpy(cfg->shortcuts[4].name, "Scrap Box");
    strcpy(cfg->shortcuts[4].command, "scrap");
    strcpy(cfg->shortcuts[5].name, "Clock");
    strcpy(cfg->shortcuts[5].command, "clock");
    strcpy(cfg->shortcuts[6].name, "Sketch Pad");
    strcpy(cfg->shortcuts[6].command, "paint");
    strcpy(cfg->shortcuts[7].name, "About Castalia/386");
    strcpy(cfg->shortcuts[7].command, "about");
    strcpy(cfg->shortcuts[8].name, "Exit to DOS");
    strcpy(cfg->shortcuts[8].command, "exit");
    cfg->shortcut_count = 9;
}

/* ---- "iconN_field" key parsing -------------------------------------- */
/* On success sets *idx (0-based) and *field ("name"/"command"). */
static bool_t parse_icon_key(const char *key, int *idx, const char **field)
{
    int n;
    if (strncmp(key, "icon", 4) != 0)
        return FALSE;
    if (key[4] < '1' || key[4] > '9')
        return FALSE;
    n = key[4] - '0';
    if (key[5] != '_')
        return FALSE;
    *idx = n - 1;
    *field = key + 6;
    return (*idx >= 0 && *idx < CFG_MAX_ICONS) ? TRUE : FALSE;
}

/* ---- main loader ----------------------------------------------------- */
bool_t config_load(const char *filename, Config *cfg)
{
    FILE *f;
    char  line[160];
    char  section[24];
    int   cur_shortcut = -1;   /* index of the [shortcut] being filled    */
    bool_t cleared_sc = FALSE; /* has the INI taken over the menu?        */
    bool_t cleared_ic = FALSE; /* has the INI taken over the icons?       */

    f = fopen(filename, "r");
    if (f == NULL)
        return FALSE;

    section[0] = '\0';

    while (fgets(line, sizeof(line), f) != NULL) {
        char *s = trim(line);
        char *eq;
        char *key;
        char *val;

        if (s[0] == '\0' || s[0] == ';' || s[0] == '#')
            continue;

        if (s[0] == '[') {
            char *close = strchr(s, ']');
            if (close != NULL)
                *close = '\0';
            copy_field(section, s + 1, (int)sizeof(section));
            lower(section);
            if (strcmp(section, "shortcut") == 0) {
                /* The first [shortcut] in the file replaces the default
                   menu rather than appending to it. */
                if (!cleared_sc) {
                    cfg->shortcut_count = 0;
                    cleared_sc = TRUE;
                }
                if (cfg->shortcut_count < CFG_MAX_SHORTCUTS) {
                    cur_shortcut = cfg->shortcut_count++;
                    memset(&cfg->shortcuts[cur_shortcut], 0,
                           sizeof(cfg->shortcuts[cur_shortcut]));
                } else {
                    cur_shortcut = -1;   /* table full, ignore extras      */
                }
            }
            continue;
        }

        eq = strchr(s, '=');
        if (eq == NULL)
            continue;
        *eq = '\0';
        key = trim(s);
        val = trim(eq + 1);
        lower(key);

        if (strcmp(section, "system") == 0) {
            if      (strcmp(key, "theme") == 0)
                copy_field(cfg->theme, val, (int)sizeof(cfg->theme));
            else if (strcmp(key, "video") == 0)
                copy_field(cfg->video, val, (int)sizeof(cfg->video));
            else if (strcmp(key, "default_path") == 0)
                copy_field(cfg->default_path, val, (int)sizeof(cfg->default_path));
            else if (strcmp(key, "clock") == 0)
                cfg->clock_enabled = parse_bool(val);
            else if (strcmp(key, "animations") == 0)
                cfg->anim_enabled = parse_bool(val);
            else if (strcmp(key, "screensaver") == 0)
                cfg->screensaver_secs = parse_int(val);
            else if (strcmp(key, "sound") == 0)
                cfg->sound_enabled = parse_bool(val);
            else if (strcmp(key, "lptdac") == 0) {
                copy_field(cfg->lptdac, val, (int)sizeof(cfg->lptdac));
                lower(cfg->lptdac);
            } else if (strcmp(key, "lptport") == 0)
                cfg->lptport = parse_int(val);
            else if (strcmp(key, "startlabel") == 0)
                copy_field(cfg->startlabel, val, (int)sizeof(cfg->startlabel));
        } else if (strcmp(section, "mouse") == 0) {
            if (strcmp(key, "enabled") == 0)
                cfg->mouse_enabled = parse_bool(val);
        } else if (strcmp(section, "clock") == 0) {
            if (strcmp(key, "enabled") == 0)
                cfg->clock_enabled = parse_bool(val);
        } else if (strcmp(section, "desktop") == 0) {
            int idx;
            const char *field;
            if (strcmp(key, "pattern") == 0) {
                copy_field(cfg->pattern, val, (int)sizeof(cfg->pattern));
            } else if (strcmp(key, "wallpaper") == 0) {
                copy_field(cfg->wallpaper, val, (int)sizeof(cfg->wallpaper));
            } else if (parse_icon_key(key, &idx, &field)) {
                /* The first desktop icon defined in the file replaces the
                   default icon set rather than overlaying it. */
                if (!cleared_ic) {
                    memset(cfg->icons, 0, sizeof(cfg->icons));
                    cfg->icon_count = 0;
                    cleared_ic = TRUE;
                }
                if (idx + 1 > cfg->icon_count)
                    cfg->icon_count = idx + 1;
                if (strcmp(field, "name") == 0)
                    copy_field(cfg->icons[idx].name, val, CFG_NAME_LEN);
                else if (strcmp(field, "command") == 0)
                    copy_field(cfg->icons[idx].command, val, CFG_CMD_LEN);
                else if (strcmp(field, "icon") == 0)
                    copy_field(cfg->icons[idx].icon, val,
                               (int)sizeof(cfg->icons[idx].icon));
            }
        } else if (strcmp(section, "colors") == 0) {
            int slot = color_slot(key);
            u8 r, g, b;
            if (slot >= 0 && parse_rgb(val, &r, &g, &b)) {
                cfg->color_set[slot] = TRUE;
                cfg->color_rgb[slot * 3 + 0] = r;
                cfg->color_rgb[slot * 3 + 1] = g;
                cfg->color_rgb[slot * 3 + 2] = b;
            }
        } else if (strcmp(section, "drawer") == 0) {
            if (strcmp(key, "scan") == 0)
                copy_field(cfg->drawer_scan, val,
                           (int)sizeof(cfg->drawer_scan));
        } else if (strcmp(section, "assoc") == 0) {
            /* EXT=applet rows; a leading dot on the key is tolerated. */
            if (cfg->assoc_count < CFG_MAX_ASSOC && key[0] != '\0') {
                CfgAssoc *a = &cfg->assoc[cfg->assoc_count++];
                copy_field(a->ext, (key[0] == '.') ? key + 1 : key,
                           (int)sizeof(a->ext));
                copy_field(a->app, val, (int)sizeof(a->app));
                lower(a->app);
            }
        } else if (strcmp(section, "shortcut") == 0 && cur_shortcut >= 0) {
            CfgShortcut *sc = &cfg->shortcuts[cur_shortcut];
            if      (strcmp(key, "name") == 0)
                copy_field(sc->name, val, CFG_NAME_LEN);
            else if (strcmp(key, "path") == 0)
                copy_field(sc->path, val, CFG_PATH_LEN);
            else if (strcmp(key, "command") == 0)
                copy_field(sc->command, val, CFG_CMD_LEN);
            else if (strcmp(key, "icon") == 0)
                copy_field(sc->icon, val, (int)sizeof(sc->icon));
            else if (strcmp(key, "freemem") == 0)
                sc->freemem = parse_bool(val);
        }
    }

    fclose(f);

    /* Drop any desktop icon slots left empty by sparse numbering. */
    {
        int i, w = 0;
        for (i = 0; i < cfg->icon_count && i < CFG_MAX_ICONS; ++i) {
            if (cfg->icons[i].name[0] != '\0') {
                if (w != i)
                    cfg->icons[w] = cfg->icons[i];
                ++w;
            }
        }
        cfg->icon_count = w;
    }
    return TRUE;
}

/* ---- INI writer ------------------------------------------------------ */
/* config_save rewrites the file as a LINE STREAM: every line the loader
   would skip (comments, blanks, sections it does not manage, icons,
   shortcuts, colours) is copied through verbatim, and only the handful
   of keys the Settings panel can change are replaced in place.  Keys the
   file never had are appended at the end of their section, and a missing
   file is created from a clean slate.  The write goes to CASTALIA.TM$
   first and replaces the INI only after it closed cleanly, so a full
   disk cannot destroy the user's configuration. */

#define SV_THEME   0
#define SV_ANIM    1
#define SV_SOUND   2
#define SV_SAVER   3
#define SV_PATTERN 4
#define SV_WALL    5
#define SV_CLOCK   6
#define SV_COUNT   7

/* Which section each managed key lives in. */
static const char * const SV_HOME[SV_COUNT] = {
    "system", "system", "system", "system", "desktop", "desktop", "system"
};

static void save_value(FILE *out, int which, const Config *cfg)
{
    switch (which) {
    case SV_THEME:
        fprintf(out, "theme=%s\n", cfg->theme);
        break;
    case SV_ANIM:
        fprintf(out, "animations=%s\n", cfg->anim_enabled ? "true" : "false");
        break;
    case SV_SOUND:
        fprintf(out, "sound=%s\n", cfg->sound_enabled ? "true" : "false");
        break;
    case SV_SAVER:
        fprintf(out, "screensaver=%d\n", cfg->screensaver_secs);
        break;
    case SV_PATTERN:
        fprintf(out, "pattern=%s\n", cfg->pattern);
        break;
    case SV_WALL:
        fprintf(out, "wallpaper=%s\n", cfg->wallpaper);
        break;
    case SV_CLOCK:
        /* Settings has always offered a Clock toggle and always reported
           "Saved to INI"; the key was simply never written, so the choice
           came back on the next boot. */
        fprintf(out, "clock=%s\n", cfg->clock_enabled ? "true" : "false");
        break;
    }
}

/* Managed-value index for `key` inside `section`, or -1. */
static int save_slot(const char *section, const char *key)
{
    if (strcmp(section, "system") == 0) {
        if (strcmp(key, "theme") == 0)       return SV_THEME;
        if (strcmp(key, "animations") == 0)  return SV_ANIM;
        if (strcmp(key, "sound") == 0)       return SV_SOUND;
        if (strcmp(key, "screensaver") == 0) return SV_SAVER;
        if (strcmp(key, "clock") == 0)       return SV_CLOCK;
    } else if (strcmp(section, "desktop") == 0) {
        if (strcmp(key, "pattern") == 0)     return SV_PATTERN;
        if (strcmp(key, "wallpaper") == 0)   return SV_WALL;
    }
    return -1;
}

/* Append every managed key of `section` that has not been written yet. */
static void flush_section(FILE *out, const char *section,
                          bool_t *done, const Config *cfg)
{
    int i;
    for (i = 0; i < SV_COUNT; ++i) {
        if (!done[i] && strcmp(SV_HOME[i], section) == 0) {
            save_value(out, i, cfg);
            done[i] = TRUE;
        }
    }
}

bool_t config_save(const char *filename, const Config *cfg)
{
    FILE  *in;
    FILE  *out;
    char   line[160];
    char   parse[160];
    char   section[24];
    char   tmp[96];
    bool_t done[SV_COUNT];
    int    i;

    /* The temp file must live BESIDE the target: the caller may hand us
       an absolute home path while the current directory sits on another
       drive, and DOS cannot rename across drives - the remove() below
       would delete the INI and the rename would strand the new file
       elsewhere.  Same directory means same drive means a safe rename. */
    {
        int k, cut = 0;
        for (k = 0; filename[k] != '\0' && k < (int)sizeof(tmp) - 14; ++k) {
            tmp[k] = filename[k];
            if (filename[k] == '\\' || filename[k] == ':')
                cut = k + 1;
        }
        strcpy(tmp + cut, "CASTALIA.TM$");
    }

    for (i = 0; i < SV_COUNT; ++i)
        done[i] = FALSE;
    section[0] = '\0';

    /* Open the SOURCE first.  With the temp file created first, an
       existing INI that could not be opened - locked, or an I/O error -
       skipped the whole copy-through block, so every [shortcut], desktop
       icon and [colors] override was dropped, the rename landed, and
       this returned TRUE.  A silent, successful strip of the user's
       settings.  Only a genuinely ABSENT file may be treated as empty. */
    in = fopen(filename, "r");
    if (in == NULL) {
        FILE *probe = fopen(filename, "rb");
        if (probe != NULL) {           /* it exists but will not read       */
            fclose(probe);
            return FALSE;
        }
    }

    out = fopen(tmp, "w");
    if (out == NULL) {
        if (in != NULL) fclose(in);
        return FALSE;
    }

    if (in != NULL) {
        while (fgets(line, sizeof(line), in) != NULL) {
            char *s;
            strcpy(parse, line);      /* parse a copy; `line` stays verbatim */
            s = trim(parse);
            if (s[0] == '[') {
                char *close = strchr(s, ']');
                /* Leaving a section: append its still-missing keys first. */
                flush_section(out, section, done, cfg);
                if (close != NULL)
                    *close = '\0';
                copy_field(section, s + 1, (int)sizeof(section));
                lower(section);
                fputs(line, out);
                continue;
            }
            if (s[0] != '\0' && s[0] != ';' && s[0] != '#') {
                char *eq = strchr(s, '=');
                if (eq != NULL) {
                    char *key;
                    int   slot;
                    *eq = '\0';
                    key = trim(s);
                    lower(key);
                    slot = save_slot(section, key);
                    if (slot >= 0) {
                        save_value(out, slot, cfg);
                        done[slot] = TRUE;
                        continue;      /* replaced the old line             */
                    }
                }
            }
            fputs(line, out);
        }
        /* A bad sector mid-read ends fgets exactly like a clean EOF, so
           everything after it was silently dropped and the SHORT file was
           then renamed over the good one - the single path the temp-file
           design does not otherwise protect against. */
        if (ferror(in)) {
            fclose(in);
            fclose(out);
            remove(tmp);
            return FALSE;
        }
        fclose(in);
        flush_section(out, section, done, cfg);
    }

    /* Sections that never appeared get created at the end of the file. */
    {
        bool_t need_sys = FALSE, need_desk = FALSE;
        for (i = 0; i < SV_COUNT; ++i) {
            if (!done[i]) {
                if (strcmp(SV_HOME[i], "system") == 0)
                    need_sys = TRUE;
                else
                    need_desk = TRUE;
            }
        }
        if (need_sys) {
            fputs("\n[system]\n", out);
            flush_section(out, "system", done, cfg);
        }
        if (need_desk) {
            fputs("\n[desktop]\n", out);
            flush_section(out, "desktop", done, cfg);
        }
    }

    /* ferror before fclose: the writes are buffered, so a full disk shows
       up here and not at any individual fputs. */
    if (ferror(out)) {
        fclose(out);
        remove(tmp);
        return FALSE;
    }
    if (fclose(out) != 0) {
        remove(tmp);
        return FALSE;
    }

    /* NOT remove-then-rename.  If the remove succeeded and the rename
       then failed - read-only medium, a stale handle, a full directory -
       the user's settings were simply gone, with the replacement
       stranded in CASTALIA.TM$ and the Settings panel saying only "Save
       FAILED".  Rename the original aside instead, so there is always a
       complete file on disk, and put it back if the swap does not land. */
    {
        char bak[96];
        int  n = (int)strlen(filename);
        int  dot;
        /* REPLACE the extension, do not append one.  "CASTALIA.INI" +
           ".BAK" is CASTALIA.INI.BAK, which is not a legal 8.3 name: DOS
           refuses the rename, and control fell straight through to the
           remove-then-rename path six lines below - the very thing the
           comment above says must never happen.  The safety net was
           unreachable on the only filesystem this runs on. */
        if (n > (int)sizeof(bak) - 5)
            n = (int)sizeof(bak) - 5;
        memcpy(bak, filename, (size_t)n);
        bak[n] = '\0';
        dot = n;
        while (dot > 0 && bak[dot - 1] != '.' &&
               bak[dot - 1] != '\\' && bak[dot - 1] != '/' &&
               bak[dot - 1] != ':')
            --dot;
        if (dot > 0 && bak[dot - 1] == '.')
            n = dot - 1;               /* cut at the dot, keeping the stem  */
        strcpy(bak + n, ".BAK");

        remove(bak);                            /* a stale one would block  */
        if (rename(filename, bak) != 0) {
            /* No original to protect (first run), or it cannot be moved.
               The plain swap is the best that is available. */
            remove(filename);
            return (rename(tmp, filename) == 0) ? TRUE : FALSE;
        }
        if (rename(tmp, filename) != 0) {
            rename(bak, filename);              /* put the original back    */
            return FALSE;
        }
        remove(bak);
        return TRUE;
    }
}
