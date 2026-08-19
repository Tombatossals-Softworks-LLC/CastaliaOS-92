/* ======================================================================
 * settings.c - The Settings panel for CASTALIA/386
 * ====================================================================== */
#include <string.h>
#include "settings.h"
#include "video.h"
#include "desktop.h"
#include "ui.h"
#include "font.h"
#include "music.h"
#include "colors.h"
#include "system.h"    /* sys_home_path: save to the home CASTALIA.INI   */
#include "keyboard.h"

static Config *g_cfg = (Config *)0;

static const char * const THEMES[] = {
    "classic",  "penumbra", "bureau",
    "winsteel", "moncloa",  "workbench",
    "ocean",    "rose",     "midnight",
    "amber",    "matrix",   "redmond",
    "sunset",   "forest",   "hotdog",
    "slate",    "sakura",   "dos"
};
#define NTHEMES (int)(sizeof(THEMES) / sizeof(THEMES[0]))

/* One line of character per theme, shown in the live preview strip.  KEEP
   IN THE SAME ORDER AS THEMES[].  Far: DGROUP is nearly full. */
static const char far TAGLINE[18][24] = {
    "the windows 95 look",  "dusk grays, soft light", "warm office beige",
    "brushed steel blues",  "sandy iberian office",   "an amiga 1.x homage",
    "cool deep-sea teals",  "warm elegant mauve",     "indigo-violet night",
    "a crt at dusk",        "green phosphor glow",    "pure vga primaries",
    "dusky plum evening",   "deep woodland greens",   "loud, period, beloved",
    "cool modern blue-gray","cherry-blossom pink",    "editor-blue nostalgia"
};

/* Copy far tagline i into a near buffer (out holds >= 24 bytes). */
static void tagline_near(int i, char *out)
{
    int k;
    for (k = 0; k < 23 && TAGLINE[i][k]; ++k) out[k] = TAGLINE[i][k];
    out[k] = '\0';
}

/* THEMES[] index of a theme name (0 = classic when unknown). */
static int theme_index(const char *name)
{
    int i;
    for (i = 0; i < NTHEMES; ++i)
        if (strcmp(name, THEMES[i]) == 0)
            return i;
    return 0;
}

/* The theme swatch the pointer rests on (-1 = none), fed by settings_mouse
   and painted large in the preview strip. */
static int g_hover = -1;

static const char * const PATTERNS[] = { "solid", "dots", "weave", "gradient" };
#define NPATTERNS 4

/* Screensaver choices (seconds; 0 = off). */
static const int SAVER_SECS[4] = { 0, 60, 120, 300 };
static const char * const SAVER_LBL[4] = { "Off", "1m", "2m", "5m" };

/* Wallpaper choices: the shipped tiles AND the full-screen GIF pictures
   (see ASSETS/ICONS).  Cycled with < / > so the row never overflows as
   the collection grows. */
#define NWALLS 13
static const char * const WALL_LBL[NWALLS] = {
    "(none)", "Clouds tile", "Diamond tile", "Circuit tile", "Stars tile",
    "Castle", "Sunset", "Bliss", "Cosmos", "Dunes", "Night",
    "Reef", "Peaks"
};
static const char * const WALL_PATH[NWALLS] = {
    "",
    "ASSETS\\ICONS\\CLOUDS.ICN",
    "ASSETS\\ICONS\\DIAMOND.ICN",
    "ASSETS\\ICONS\\CIRCUIT.ICN",
    "ASSETS\\ICONS\\STARS.ICN",
    "ASSETS\\ICONS\\CASTLE.GIF",
    "ASSETS\\ICONS\\SUNSET.GIF",
    "ASSETS\\ICONS\\BLISS.GIF",
    "ASSETS\\ICONS\\COSMOS.GIF",
    "ASSETS\\ICONS\\DUNES.GIF",
    "ASSETS\\ICONS\\NIGHT.GIF",
    "ASSETS\\ICONS\\REEF.GIF",
    "ASSETS\\ICONS\\PEAKS.GIF"
};

/* Index of the active wallpaper in WALL_PATH (0 when unrecognised). */
static int wall_index(void)
{
    int i;
    for (i = 0; i < NWALLS; ++i)
        if (strcmp(g_cfg->wallpaper, WALL_PATH[i]) == 0)
            return i;
    return 0;
}

/* Button rectangles, refreshed by each draw for the click handler. */
static Rect g_tb[NTHEMES];
static Rect g_pb[NPATTERNS];
static Rect g_anim_b, g_sound_b;
static Rect g_sv_b[4];
static Rect g_wp_prev, g_wp_next;      /* the wallpaper < / > cyclers      */
static Rect g_save_b;
static Rect g_clock_b;                 /* the taskbar-clock toggle          */

/* Tabbed pages: Theme | Desktop | System. */
static int  g_tab = 0;
static Rect g_tab_b[3];

/* Keyboard focus.  Settings was the one applet with no keyboard path at
   all: without a mouse driver every preference in the program was
   unreachable.  g_focus indexes the current tab's controls in the same
   order they are painted; -1 means nothing is focused yet. */
static int g_focus = -1;

static int tab_item_count(void)
{
    if (g_tab == 0) return NTHEMES;
    if (g_tab == 1) return NPATTERNS + 3;   /* patterns, < , > , animations */
    return 6;                               /* sound, clock, four timeouts  */
}

/* The screen rectangle of the focused control, so the draw can ring it. */
static bool_t focus_rect(Rect *r)
{
    int f = g_focus;
    if (f < 0 || f >= tab_item_count())
        return FALSE;
    if (g_tab == 0) { *r = g_tb[f]; return TRUE; }
    if (g_tab == 1) {
        if (f < NPATTERNS)      *r = g_pb[f];
        else if (f == NPATTERNS)     *r = g_wp_prev;
        else if (f == NPATTERNS + 1) *r = g_wp_next;
        else                         *r = g_anim_b;
        return TRUE;
    }
    if (f == 0)      *r = g_sound_b;
    else if (f == 1) *r = g_clock_b;
    else             *r = g_sv_b[f - 2];
    return TRUE;
}
static const char * const TAB_LBL[3] = { "Theme", "Desktop", "System" };

/* Result line after a Save click ("" until the first press). */
static char g_msg[24];

void settings_open(Config *cfg)
{
    g_cfg = cfg;
    g_msg[0] = '\0';
}

/* Settings applies every change LIVE - the theme, the pattern, the
   clock - so the screen already looks right while nothing has reached
   the INI.  Closing the window then dropped the lot without a word.
   Track it, and let the window manager ask. */
static bool_t g_dirty = FALSE;

bool_t settings_is_dirty(void)    { return g_dirty; }
void   settings_flush_state(void) { g_dirty = FALSE; }

static bool_t nameq(const char *a, const char *b)
{
    return (strcmp(a, b) == 0) ? TRUE : FALSE;
}

/* The current-palette slot nearest an RGB (same idea as the .ICO loader):
   lets a theme swatch be drawn with a colour the live 16-slot UI can show. */
static u8 nearest_slot(u8 r, u8 g, u8 b)
{
    int i, best = 0;
    long bd = 2000000L;
    for (i = 0; i < 16; ++i) {
        u8 rr, gg, bb;
        int dr, dg, db;
        long d;
        video_slot_rgb(i, &rr, &gg, &bb);
        dr = (int)r - rr; dg = (int)g - gg; db = (int)b - bb;
        d = (long)dr * dr + (long)dg * dg + (long)db * db;
        if (d < bd) { bd = d; best = i; }
    }
    return (u8)best;
}

/* A named theme's slot, mapped to the nearest drawable current slot. */
static u8 theme_slot(const char *name, int slot)
{
    u8 r, g, b;
    video_theme_rgb(name, slot, &r, &g, &b);
    return nearest_slot(r, g, b);
}

/* A theme button: a mini window-mockup swatch in that theme's own colours
   (desktop, a face window and a title strip) beside its name. */
static void theme_button(const Rect *r, const char *name, bool_t active)
{
    int o  = active ? 1 : 0;
    int sw = r->h - 4;
    int sx = r->x + 2 + o, sy = r->y + 2 + o;
    u8  dcol = theme_slot(name, C_DESKTOP);
    u8  tcol = theme_slot(name, C_TITLE);
    u8  fcol = theme_slot(name, C_FACE);
    ui_fill_face(r->x, r->y, r->w, r->h);
    if (active) ui_sink(r->x, r->y, r->w, r->h);
    else        ui_raise(r->x, r->y, r->w, r->h);
    vid_fillrect(sx, sy, sw, sw, dcol);              /* desktop backdrop     */
    vid_fillrect(sx + 1, sy + 2, sw - 2, sw - 3, fcol);   /* a face window   */
    vid_fillrect(sx + 1, sy + 2, sw - 2, 2, tcol);        /* its title strip */
    vid_rect(sx, sy, sw, sw, C_BLACK);
    font_draw(sx + sw + 3, r->y + (r->h - font_h()) / 2 + o, name, C_BLACK);
}

/* A little whole screen in a named theme's colours - desktop, a desktop
   icon, a window with title strip and paper, and the taskbar - for the
   live preview strip.  Every colour is mapped to the nearest live slot. */
static void theme_preview(int x, int y, int w, int h, const char *name)
{
    u8 dcol = theme_slot(name, C_DESKTOP), tcol = theme_slot(name, C_TITLE);
    u8 fcol = theme_slot(name, C_FACE),    hcol = theme_slot(name, C_HILIGHT);
    u8 wcol = theme_slot(name, C_WHITE);
    int tb = (h >= 24) ? 5 : 3;                  /* mini taskbar height     */
    int wx = x + w / 4, wy = y + 2;
    int ww = w * 5 / 8, wh = h - tb - 4;
    vid_fillrect(x, y, w, h, dcol);              /* the desktop             */
    vid_fillrect(x + 2, y + 2, 3, 3, wcol);      /* a desktop icon...       */
    vid_hline(x + 2, y + 6, 3, fcol);            /* ...and its label        */
    vid_fillrect(wx, wy, ww, wh, fcol);          /* a window                */
    vid_fillrect(wx + 1, wy + 1, ww - 2, tb - 1, tcol);   /* its title bar  */
    vid_fillrect(wx + 2, wy + tb + 1, ww - 4, wh - tb - 3, wcol); /* paper  */
    vid_rect(wx, wy, ww, wh, C_BLACK);
    vid_fillrect(x, y + h - tb, w, tb, fcol);    /* the taskbar             */
    vid_hline(x, y + h - tb, w, hcol);
    vid_fillrect(x + 1, y + h - tb + 1, tb + 3, tb - 2, tcol); /* Start     */
    vid_rect(x, y, w, h, C_BLACK);               /* the monitor bezel       */
}

void settings_draw(const Rect *cl)
{
    int bh = font_h() + 5, bw, i, x, y, t, th = font_h() + 5, tw, sy;
    if (g_cfg == (Config *)0)
        return;

    /* Tab strip: the active page's tab sits pressed. */
    y = cl->y + 3;
    tw = (cl->w - 8) / 3;
    for (t = 0; t < 3; ++t) {
        x = cl->x + 4 + t * (tw + 1);
        rect_set(&g_tab_b[t], x, y, tw, th);
        ui_tab(&g_tab_b[t], TAB_LBL[t], (t == g_tab) ? TRUE : FALSE);
    }
    ui_tab_page_top(cl->x + 4, y + th - 1, cl->w - 8,
                    (g_tab >= 0 && g_tab < 3) ? &g_tab_b[g_tab]
                                              : (const Rect *)0);
    y += th + 5;

    if (g_tab == 0) {                            /* -------- THEME -------- */
        int gw = (cl->w - 8) / 3, gh = font_h() + 9;
        for (i = 0; i < NTHEMES; ++i) {
            x = cl->x + 4 + (i % 3) * (gw + 1);
            rect_set(&g_tb[i], x, y + (i / 3) * (gh + 1), gw, gh);
            theme_button(&g_tb[i], THEMES[i], nameq(g_cfg->theme, THEMES[i]));
            if (i == g_hover)                    /* the pointer rests here  */
                vid_rect(g_tb[i].x + 1, g_tb[i].y + 1,
                         g_tb[i].w - 2, g_tb[i].h - 2, C_TITLE);
        }
        /* The live preview strip: point at any swatch and that theme is
           painted large - a whole mini screen - beside its name and a one-
           line character note.  With the pointer elsewhere it shows the
           theme now in force. */
        {
            int py = y + ((NTHEMES + 2) / 3) * (gh + 1) + 1;
            int ph = font_h() * 3;
            int px = cl->x + 4, pw = cl->w - 8;
            if (py + ph <= cl->y + cl->h - bh - 5) {
                int  idx = (g_hover >= 0) ? g_hover : theme_index(g_cfg->theme);
                int  mw  = (ph - 4) * 3 / 2;
                int  ty  = py + (ph - 2 * font_h() - 2) / 2;
                char tg[24];
                const char *lbl;
                ui_fill_face(px, py, pw, ph);
                ui_sink(px, py, pw, ph);
                theme_preview(px + 2, py + 2, mw, ph - 4, THEMES[idx]);
                tagline_near(idx, tg);
                font_draw(px + mw + 7, ty, THEMES[idx], C_TITLE);
                font_draw(px + mw + 7, ty + font_h() + 2, tg, C_BLACK);
                lbl = nameq(g_cfg->theme, THEMES[idx]) ? "active" : "preview";
                font_draw(px + pw - font_text_width(lbl) - 3, ty, lbl,
                          C_DKGRAY);
            }
        }
    } else if (g_tab == 1) {                     /* ------- DESKTOP ------- */
        font_draw(cl->x + 4, y, "Backdrop", C_TITLE);
        y += font_h() + 2;
        bw = (cl->w - 11) / 4;
        for (i = 0; i < NPATTERNS; ++i) {
            x = cl->x + 4 + i * (bw + 1);
            rect_set(&g_pb[i], x, y, bw, bh);
            ui_button(&g_pb[i], PATTERNS[i], nameq(g_cfg->pattern, PATTERNS[i]));
        }
        y += bh + 5;
        {
            int lx = font_adv() * 6 + 6;
            int aw = font_adv() + 8;
            int nx = cl->x + 4 + lx + aw + 2;
            int nw = cl->w - lx - 2 * aw - 13;
            font_draw(cl->x + 4, y + 3, "Paper", C_TITLE);
            rect_set(&g_wp_prev, cl->x + 4 + lx, y, aw, bh);
            ui_button(&g_wp_prev, "<", FALSE);
            vid_fillrect(nx, y, nw, bh, C_WHITE);
            ui_sink(nx, y, nw, bh);
            ui_text_center(nx, y + 3, nw, WALL_LBL[wall_index()], C_BLACK);
            rect_set(&g_wp_next, nx + nw + 2, y, aw, bh);
            ui_button(&g_wp_next, ">", FALSE);
        }
        y += bh + 5;
        /* A checkbox, not a push button captioned "ON": a pressed button
           reads as "held down", not as "this is the setting". */
        rect_set(&g_anim_b, cl->x + 4, y, cl->w - 8, bh);
        ui_checkbox(cl->x + 6, y + 1, g_cfg->anim_enabled,
                    "Window animations");
    } else {                                     /* ------- SYSTEM -------- */
        bw = (cl->w - 12) / 2;
        rect_set(&g_sound_b, cl->x + 4, y, bw, bh);
        ui_checkbox(cl->x + 6, y + 1, g_cfg->sound_enabled, "Sound");
        rect_set(&g_clock_b, cl->x + 8 + bw, y, bw, bh);
        ui_checkbox(cl->x + 10 + bw, y + 1, g_cfg->clock_enabled, "Clock");
        y += bh + 5;
        /* Four mutually exclusive timeouts: radio buttons say that; four
           push buttons with one held down did not. */
        {
            int gh = bh * 2 + 6;
            ui_groupbox(cl->x + 4, y, cl->w - 8, gh, "Screen saver");
            bw = (cl->w - 20) / 2;
            for (i = 0; i < 4; ++i) {
                int rx = cl->x + 10 + (i % 2) * bw;
                int ry = y + font_h() / 2 + 3 + (i / 2) * (font_h() + 3);
                rect_set(&g_sv_b[i], rx, ry, bw - 2, font_h() + 2);
                ui_radio(rx, ry, (g_cfg->screensaver_secs == SAVER_SECS[i])
                                 ? TRUE : FALSE, SAVER_LBL[i]);
            }
            y += gh - bh - 5;
        }
        y += bh + 7;
        font_draw(cl->x + 4, y, "Theme", C_DKGRAY);
        font_draw(cl->x + 4 + font_adv() * 7, y, g_cfg->theme, C_BLACK);
        y += font_h() + 3;
        font_draw(cl->x + 4, y, "Video", C_DKGRAY);
        font_draw(cl->x + 4 + font_adv() * 7, y,
                  video_is_big() ? "640x480x16" : "320x200x256", C_BLACK);
    }

    /* Save button (every tab), with the last save's verdict beside it. */
    sy = cl->y + cl->h - bh - 3;
    bw = (cl->w - 12) / 2;
    rect_set(&g_save_b, cl->x + 4, sy, bw, bh);
    ui_button(&g_save_b, "Save to INI", FALSE);
    font_draw(cl->x + 8 + bw, sy + 3,
              (g_msg[0] != '\0') ? g_msg : "keeps changes",
              (g_msg[0] != '\0') ? C_TITLE : C_DKGRAY);

    /* Ring the keyboard-focused control.  Drawn last so it sits over the
       button it marks, and only once the user has actually used a key -
       a mouse-only session never sees it. */
    {
        Rect fr;
        if (focus_rect(&fr))
            vid_rect(fr.x - 2, fr.y - 2, fr.w + 4, fr.h + 4, C_BLACK);
    }
}

/* Act on the i-th control of the current tab.  Shared by the mouse and
   the keyboard so the two can never drift apart.  TRUE = the change is
   bigger than this window (a new palette, pattern or wallpaper) and the
   whole scene has to be recomposed. */
static bool_t settings_activate(int i)
{
    if (g_cfg == (Config *)0 || i < 0 || i >= tab_item_count())
        return FALSE;
    /* One funnel for every setting, so nothing can change without being
       noticed as unsaved. */
    g_dirty = TRUE;
    if (g_tab == 0) {
        strcpy(g_cfg->theme, THEMES[i]);
        /* Cross-fade into the new palette: fade to black, load the theme
           into the shadow, and let the present path fade back in.  With
           animations off this is an instant switch. */
        video_fade_out();
        video_set_theme(g_cfg->theme);
        colors_open(g_cfg->theme);       /* keep the swatch board honest    */
        return TRUE;
    }
    if (g_tab == 1) {
        if (i < NPATTERNS) {
            strcpy(g_cfg->pattern, PATTERNS[i]);
            desktop_set_pattern(g_cfg->pattern);
            return TRUE;
        }
        if (i == NPATTERNS || i == NPATTERNS + 1) {
            int step = (i == NPATTERNS + 1) ? 1 : NWALLS - 1;
            int k = (wall_index() + step) % NWALLS;
            strcpy(g_cfg->wallpaper, WALL_PATH[k]);
            desktop_set_wallpaper(g_cfg->wallpaper);
            return TRUE;                 /* the whole desktop changed       */
        }
        g_cfg->anim_enabled = g_cfg->anim_enabled ? FALSE : TRUE;
        video_enable_fades(g_cfg->anim_enabled);
        return FALSE;
    }
    if (i == 0) {
        g_cfg->sound_enabled = g_cfg->sound_enabled ? FALSE : TRUE;
        music_set_sfx(g_cfg->sound_enabled);
        if (g_cfg->sound_enabled)
            music_sfx(880, 1);           /* a little "it works" blip        */
        return FALSE;
    }
    if (i == 1) {
        g_cfg->clock_enabled = g_cfg->clock_enabled ? FALSE : TRUE;
        return TRUE;                     /* the taskbar clock shows/hides   */
    }
    g_cfg->screensaver_secs = SAVER_SECS[i - 2];
    return FALSE;
}

/* Write the preferences out.  Always to the home CASTALIA.INI, never into
   whatever directory the Disk Cabinet last browsed to. */
static void settings_save(void)
{
    char inip[80];
    sys_home_path(inip, (int)sizeof(inip), "CASTALIA.INI");
    if (config_save(inip, g_cfg)) {
        strcpy(g_msg, "Saved to INI");
        g_dirty = FALSE;
        music_sfx(660, 1);
    } else {
        strcpy(g_msg, "Save FAILED");
    }
}

/* Tab and Left/Right change page; Up/Down (and PgUp/PgDn) walk the page's
   controls; Enter or Space activates; S saves.  Not Shift+Tab: the shell
   claims that one globally to cycle windows, so this panel can never see
   it - the comment used to promise a key that never arrives. */
bool_t settings_key(int key)
{
    int n;
    if (g_cfg == (Config *)0)
        return FALSE;
    n = tab_item_count();
    if (key == KEY_LEFT || key == KEY_TAB) {
        g_tab = (key == KEY_TAB) ? ((g_tab + 1) % 3) : ((g_tab + 2) % 3);
        g_focus = -1;
        return TRUE;
    }
    if (key == KEY_RIGHT) {
        g_tab = (g_tab + 1) % 3;
        g_focus = -1;
        return TRUE;
    }
    if (key == KEY_DOWN || key == KEY_PGDN) {
        g_focus = (g_focus < 0) ? 0 : (g_focus + 1) % n;
        return TRUE;
    }
    if (key == KEY_UP || key == KEY_PGUP) {
        g_focus = (g_focus <= 0) ? n - 1 : g_focus - 1;
        return TRUE;
    }
    if (key == KEY_ENTER || key == ' ') {
        if (g_focus < 0)
            return FALSE;
        (void)settings_activate(g_focus);
        return TRUE;                     /* always repaint: state changed   */
    }
    if (key == 's' || key == 'S') {
        settings_save();
        return TRUE;
    }
    return FALSE;
}

bool_t settings_click(const Rect *cl, int mx, int my)
{
    int i;
    (void)cl;
    if (g_cfg == (Config *)0)
        return FALSE;

    /* Switch tabs (repaints the window via WM_REDRAW - FALSE below). */
    for (i = 0; i < 3; ++i) {
        if (rect_contains(&g_tab_b[i], mx, my)) {
            g_tab = i;
            return FALSE;
        }
    }

    if (rect_contains(&g_save_b, mx, my)) {   /* Save works from any tab   */
        settings_save();
        return FALSE;
    }

    /* Hit-test the current tab's controls in painted order and hand the
       index to the shared activator, so a click and a keypress on the same
       control can never do different things. */
    {
        int n = tab_item_count();
        Rect r;
        for (i = 0; i < n; ++i) {
            int save = g_focus;
            g_focus = i;
            if (focus_rect(&r) && rect_contains(&r, mx, my)) {
                g_focus = save;           /* clicking must not move focus    */
                return settings_activate(i);
            }
            g_focus = save;
        }
    }
    return FALSE;
}

/* Pointer motion over the Theme tab: track which swatch the mouse rests
   on so the preview strip follows it live.  TRUE = the hover changed and
   the window wants a repaint.  The rects are refreshed by every draw, so
   this is a pure hit-test against the last painted layout. */
bool_t settings_mouse(int mx, int my)
{
    int i, h = -1;
    if (g_cfg == (Config *)0)
        return FALSE;
    if (g_tab == 0) {
        for (i = 0; i < NTHEMES; ++i) {
            if (rect_contains(&g_tb[i], mx, my)) {
                h = i;
                break;
            }
        }
    }
    if (h != g_hover) {
        g_hover = h;
        return TRUE;
    }
    return FALSE;
}
