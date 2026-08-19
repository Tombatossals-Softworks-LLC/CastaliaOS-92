/* ======================================================================
 * desktop.c - Desktop, icon grid and taskbar for CASTALIA/386
 * ====================================================================== */
#include <dos.h>       /* _dos_gettime                                   */
#include <stdio.h>     /* sprintf                                        */
#include <string.h>
#include "desktop.h"
#include "video.h"
#include "ui.h"
#include "font.h"
#include "icon.h"
#include "window.h"
#include "keyboard.h"
#include "gif.h"
#include "system.h"    /* sys_home_path: anchor a relative wallpaper     */

/* Icon cell layout (scales with the icon size / font in Mode 12h). */
#define CELL_W   (ICON_SIZE + 24)            /* 56 at 32px icons          */
/* A cell must clear its own content: the icon (ICON_SIZE), a 2px gap, two
   label lines (font_h() each, 1px apart) and the 1px label shadow.  The old
   -2 was 6px short, so "Disk Cabinet" and "About Castalia" - the two names
   that wrap - printed their second line over the next row's icon.  The row
   count is unchanged in both video modes. */
#define CELL_H   (ICON_SIZE + font_h() * 2 + 6) /* 54 at 32px icons       */
#define GRID_X   (font_h())                  /* 8                         */
#define GRID_Y   (font_h() - 2)              /* 6                         */
/* As many rows as fit above the taskbar (3 at 320x200, 4 at 640x480), so a
   full column never pushes an icon's label down into the taskbar. */
#define ROWS_PER_COL (CAST_MAX(1, (TASKBAR_Y - GRID_Y - 2) / CELL_H))

/* Taskbar widgets. */
#define LAUNCH_X  3
/* The Start button's caption: "Inicio" out of the box, and localisable
   from CASTALIA.INI ([system] startlabel=Start / Demarrer / Avvio...). */
#define START_LABEL start_label()
/* A Windows-95 Start button: 1px raised frame + 13px castle + a gap + the
   caption + padding.  font_text_width() sizes it to fit any label/font. */
#define LAUNCH_W  (2 + 13 + 3 + font_text_width(START_LABEL) + 3)
#define CLOCK_W   (font_adv() * 5 + 6)       /* 36 -> "HH:MM" + pad        */
#define SPKR_W    12                         /* tray speaker gutter        */

static const Config *g_cfg = NULL;

static const char *start_label(void)
{
    return (g_cfg != NULL && g_cfg->startlabel[0] != '\0') ? g_cfg->startlabel
                                                           : "Inicio";
}
static int g_sel = -1;
static char g_pattern[12] = "solid";

/* TRUE while the scene cache holds a valid copy of the composed desktop
   background (backdrop + all icons, unselected).  Invalidated whenever
   the backdrop pattern or the icon set changes. */
static bool_t g_bg_valid = FALSE;

void desktop_set_pattern(const char *p)
{
    int i = 0;
    while (p[i] != '\0' && i < (int)sizeof(g_pattern) - 1) {
        g_pattern[i] = p[i];
        ++i;
    }
    g_pattern[i] = '\0';
    g_bg_valid = FALSE;
}

/* Loaded bitmap icons (one per desktop slot; .loaded = use it), plus one
   extra slot at the end for the tiled desktop WALLPAPER.  The table
   (~9 KB) is grabbed from DOS on first use: a near array would crowd the
   nearly-full DGROUP and a far static would pad the EXE with 9 KB of
   zeros.  When the allocation fails the procedural icons simply keep
   serving (and the wallpaper quietly stays off). */
#define WALL_SLOT CFG_MAX_ICONS
static IconBitmap far *g_bm = (IconBitmap far *)0;

/* The Start button no longer loads a .ICO: its badge is the crisp pixel-art
   Castalia castle, drawn procedurally by ui_start_castle() so it stays sharp
   and recolours with the theme. */

/* A full-screen GIF wallpaper decoded once into a far buffer.  Its palette
   is loaded into the free DAC window 16..191 (the UI owns 0..15, the two
   gradient ramps 192..255), and its pixels are shifted +16 to match; so a
   128-colour picture and the 16-colour UI coexist without a clash.  Only
   in Mode 13h (256 colours) - the 16-colour planar mode has no room. */
static unsigned g_gifseg = 0;
static u8 far  *g_gif    = (u8 far *)0;
static int      g_gif_w = 0, g_gif_h = 0;
static bool_t   g_gif_ok = FALSE;

static bool_t ends_gif(const char *p)
{
    int n = 0;
    while (p[n]) ++n;
    if (n < 4) return FALSE;
    return (p[n-4] == '.' &&
            (p[n-3] == 'g' || p[n-3] == 'G') &&
            (p[n-2] == 'i' || p[n-2] == 'I') &&
            (p[n-1] == 'f' || p[n-1] == 'F')) ? TRUE : FALSE;
}

static void load_gif_wallpaper(const char *path)
{
    unsigned char pal[256 * 3];
    int ncol = 0, w = 0, h = 0;
    long k, n;

    g_gif_ok = FALSE;
    if (video_is_big())                /* 16-colour mode: no palette room  */
        return;
    if (g_gif == (u8 far *)0) {         /* 64000-byte buffer, allocated once */
        unsigned seg;
        if (_dos_allocmem((unsigned)((64000UL + 15UL) / 16UL), &seg) != 0)
            return;
        g_gifseg = seg;
        g_gif = (u8 far *)MK_FP(seg, 0);
    }
    if (!gif_decode(path, g_gif, SCREEN_W, TASKBAR_Y, &w, &h, pal, &ncol))
        return;
    if (ncol > 176) ncol = 176;        /* clamp to the free DAC window     */
    if (ncol > 0)
        video_set_dac(16, ncol, pal);
    n = (long)w * h;                    /* shift pixels into slots 16..191  */
    for (k = 0; k < n; ++k) {
        int v = g_gif[k] + 16;
        if (v > 191) v = 191;
        g_gif[k] = (u8)v;
    }
    g_gif_w = w; g_gif_h = h; g_gif_ok = TRUE;
}

/* Load whichever wallpaper the path names: a GIF picture or an .ICN tile.
   A relative INI path (wallpaper=ASSETS\...) is anchored to the home
   directory - the wallpaper is reloaded after every full-screen takeover,
   by which time the Disk Cabinet may have chdir()d anywhere. */
static void apply_wallpaper(const char *path)
{
    char full[96];
    g_gif_ok = FALSE;
    if (g_bm != (IconBitmap far *)0)
        g_bm[WALL_SLOT].loaded = FALSE;
    if (path == NULL || path[0] == '\0')
        return;
    if (path[0] != '\\' && path[1] != ':') {
        sys_home_path(full, (int)sizeof(full), path);
        path = full;
    }
    if (ends_gif(path))
        load_gif_wallpaper(path);
    else if (g_bm != (IconBitmap far *)0)
        icon_load(path, &g_bm[WALL_SLOT]);
}

void desktop_init(const Config *cfg)
{
    int i;
    g_cfg = cfg;
    g_sel = -1;
    g_bg_valid = FALSE;

    strcpy(g_pattern, cfg->pattern);
    if (g_bm == (IconBitmap far *)0) {
        unsigned seg;
        unsigned paras = (unsigned)
            (((CFG_MAX_ICONS + 1) * sizeof(IconBitmap) + 15U) / 16U);
        if (_dos_allocmem(paras, &seg) == 0)
            g_bm = (IconBitmap far *)MK_FP(seg, 0);
    }
    if (g_bm == (IconBitmap far *)0)
        return;
    for (i = 0; i < CFG_MAX_ICONS; ++i) {
        g_bm[i].loaded = FALSE;
        if (i < cfg->icon_count && cfg->icons[i].icon[0] != '\0')
            icon_load(cfg->icons[i].icon, &g_bm[i]);
    }
    apply_wallpaper(cfg->wallpaper);
}

/* Swap two icon slots' loaded bitmaps in memory - dropping a dragged
   icon used to re-read every .ICN from disk via desktop_init(); now the
   drop costs two far memcpys and one cache rebuild. */
void desktop_swap_bitmaps(int a, int b)
{
    /* far: ~1 KB, touched only by the three _fmemcpy below, and DGROUP is
       the scarce segment - not the far heap. */
    static IconBitmap far tmp;
    if (g_bm == (IconBitmap far *)0)
        return;
    if (a < 0 || b < 0 || a >= CFG_MAX_ICONS || b >= CFG_MAX_ICONS)
        return;
    _fmemcpy(&tmp, &g_bm[a], sizeof(IconBitmap));
    _fmemcpy(&g_bm[a], &g_bm[b], sizeof(IconBitmap));
    _fmemcpy(&g_bm[b], &tmp, sizeof(IconBitmap));
    /* Two slots just changed their pixels without changing address, and
       icon.c keys its 2x cache on the address. */
    icon_cache_flush();
    g_bg_valid = FALSE;
}

/* Swap the tiled wallpaper at run time (Settings).  The art loads into
   the same spare slot; the cache rebuild does the rest. */
void desktop_set_wallpaper(const char *path)
{
    apply_wallpaper(path);
    g_bg_valid = FALSE;
}

/* Tile the wallpaper icon across the desktop.  Transparent pixels let
   the pattern underneath show through, so a sparse .ICN reads as a
   motif over the backdrop rather than a wall of squares.  This runs
   only when the scene cache is rebuilt (a pattern or icon change), so
   its cost never touches ordinary interaction. */
static void desktop_tile_wallpaper(void)
{
    int step = ICON_SIZE, x, y;
    if (g_bm == (IconBitmap far *)0 || !g_bm[WALL_SLOT].loaded)
        return;
    for (y = 0; y < TASKBAR_Y; y += step)
        for (x = 0; x < SCREEN_W; x += step)
            icon_draw(&g_bm[WALL_SLOT], x, y, ui_scale());
}

/* Fill the desktop area with the configured pattern (cheap accents over a
   solid fill, so frequent full redraws stay fast even at 640x480). */
static void desktop_fill_bg(void)
{
    int W = SCREEN_W, H = TASKBAR_Y, x, y;

    /* A GIF wallpaper is a full picture: blit it row by row over the whole
       desktop (any margin left by a smaller image keeps the desktop
       colour).  Composed once into the scene cache, so it is free per
       frame - a 320x200 sunset costs nothing while you work. */
    if (g_gif_ok) {
        vid_fillrect(0, 0, W, H, C_DESKTOP);
        for (y = 0; y < g_gif_h && y < H; ++y)
            vid_copy_row(0, y, g_gif + (long)y * g_gif_w,
                         (g_gif_w < W) ? g_gif_w : W);
        return;
    }

    if (strcmp(g_pattern, "gradient") == 0) {
        vid_desktop_fill(0, 0, W, H);       /* themed vertical sweep */
        desktop_tile_wallpaper();
        return;
    }

    vid_fillrect(0, 0, W, H, C_DESKTOP);

    if (strcmp(g_pattern, "dots") == 0) {
        for (y = 2; y < H; y += 8)
            for (x = 2; x < W; x += 8)
                vid_pixel(x, y, C_SHADOW);
    } else if (strcmp(g_pattern, "weave") == 0) {
        for (y = 0; y < H; y += 4)
            for (x = ((y >> 2) & 1) ? 2 : 0; x < W; x += 4)
                vid_pixel(x, y, C_DKGRAY);
    }
    /* "solid" (and anything unknown) leaves the plain fill. */
    desktop_tile_wallpaper();
}

/* Paint the desktop backdrop (the GIF wallpaper if one is loaded, else the
   themed pattern) into an arbitrary rectangle in SCREEN coordinates.  The
   About box uses this to show the real desktop wallpaper behind its banner
   instead of drawing a scene of its own - the picture lines up exactly with
   what is behind the window, so the banner reads as a window onto the
   wallpaper. */
void desktop_blit_backdrop(int x, int y, int w, int h)
{
    int row;
    if (g_gif_ok) {
        vid_fillrect(x, y, w, h, C_DESKTOP);
        for (row = 0; row < h; ++row) {
            int sy = y + row;
            int cw = w;
            if (sy < 0 || sy >= g_gif_h || x >= g_gif_w)
                continue;
            if (x + cw > g_gif_w)
                cw = g_gif_w - x;
            vid_copy_row(x, sy, g_gif + (long)sy * g_gif_w + x, cw);
        }
        return;
    }
    if (strcmp(g_pattern, "gradient") == 0)
        vid_desktop_fill(x, y, w, h);
    else
        vid_fillrect(x, y, w, h, C_DESKTOP);
}

int desktop_icon_count(void)
{
    return (g_cfg != NULL) ? g_cfg->icon_count : 0;
}

const char *desktop_icon_command(int idx)
{
    if (g_cfg == NULL || idx < 0 || idx >= g_cfg->icon_count)
        return "";
    return g_cfg->icons[idx].command;
}

const char *desktop_icon_name(int idx)
{
    if (g_cfg == NULL || idx < 0 || idx >= g_cfg->icon_count)
        return "";
    return g_cfg->icons[idx].name;
}

/* Top-left of icon cell i. */
static void cell_origin(int i, int *cx, int *cy)
{
    int col = i / ROWS_PER_COL;
    int row = i % ROWS_PER_COL;
    *cx = GRID_X + col * (CELL_W + 4);
    *cy = GRID_Y + row * CELL_H;
}

/* Draw a label centred in width w, wrapped to at most two lines. */
/* One line of a desktop icon label, drawn with a hard drop shadow the way
   Windows 95 did.  The labels sit straight on the backdrop, and against
   the lighter desktops (bureau, moncloa, sakura) - or any GIF wallpaper -
   plain white text falls under a 2.5:1 contrast ratio and stops being
   readable.  The shadow costs one extra font_draw and makes the label
   legible over ANY backdrop. */
static void label_line(int x, int y, int w, const char *s, u8 color)
{
    ui_text_center(x + 1, y + 1, w, s, C_BLACK);
    ui_text_center(x,     y,     w, s, color);
}

/* LABEL_GUTTER: keep the text clear of the cell edges, or a caption that
   exactly fills the cell touches its neighbour and the two read as one. */
/* 8, not 2.  At 2 the wrap allows 9 characters = 54px in a 56px cell, so
   the gap BETWEEN two captions (6px) came out smaller than the word space
   INSIDE one (7px) - which is exactly why "Scrap Box" and "Cardfile" read
   as a single caption.  8 caps a line at 8 characters and leaves every
   cell a >=4px margin. */
#define LABEL_GUTTER 8

static void draw_label(int x, int y, int w, const char *s, u8 color)
{
    char a[24], b[24];
    ui_wrap2(s, w - LABEL_GUTTER, w - 2, a, b, (int)sizeof(a));
    label_line(x, y, w, a, color);
    if (b[0] != '\0')
        label_line(x, y + font_h() + 1, w, b, color);
}

/* Paint one icon cell (icon + label; selected adds band and outline). */
static void draw_cell(int i, bool_t selected)
{
    int cx, cy, icon_x, label_y;
    cell_origin(i, &cx, &cy);
    icon_x  = cx + (CELL_W - ICON_SIZE) / 2;
    label_y = cy + ICON_SIZE + 2;

    /* A loaded bitmap takes precedence over the procedural icon. */
    if (g_bm != (IconBitmap far *)0 && g_bm[i].loaded)
        icon_draw(&g_bm[i], icon_x, cy, ui_scale());
    else
        ui_icon(ui_icon_for_command(g_cfg->icons[i].command), icon_x, cy);

    if (selected) {
        /* C_TITLE, not C_BLUE: menus, list rows and the caption bar all
           select in C_TITLE, so the desktop was the one place in the shell
           that used a second, brighter "selected" blue. */
        /* The Windows idiom: the label plate is the width of the TEXT,
           the icon is tinted rather than boxed, and a dotted focus rect
           marks it.  A plate spanning the whole cell made "Toolbox" three
           times wider than the word, and the hard white rectangle round
           the icon was the loudest thing on the desktop. */
        char la[24], lb[24];
        int  lw, lh2, ly;
        ui_wrap2(g_cfg->icons[i].name, CELL_W - LABEL_GUTTER, CELL_W - 2,
                 la, lb, (int)sizeof(la));
        lw  = font_text_width(la);
        if (lb[0] != '\0' && font_text_width(lb) > lw)
            lw = font_text_width(lb);
        lh2 = (lb[0] != '\0') ? font_h() * 2 + 3 : font_h() + 3;
        ly  = label_y - 1;
        vid_fillrect(cx + (CELL_W - lw) / 2 - 2, ly, lw + 4, lh2, C_TITLE);
        draw_label(cx, label_y, CELL_W, g_cfg->icons[i].name, C_WHITE);
        vid_dither_rect(icon_x, cy, ICON_SIZE, ICON_SIZE, C_TITLE);
        ui_focus_rect(cx + (CELL_W - lw) / 2 - 2, ly, lw + 4, lh2);
    } else {
        draw_label(cx, label_y, CELL_W, g_cfg->icons[i].name, C_WHITE);
    }
}

void desktop_draw(void)
{
    int i, n = desktop_icon_count();

    /* Fast path: the composed background (backdrop + unselected icons)
       lives in the scene cache, so rebuilding the desktop is ONE dword
       copy instead of gradients, icons and labels from first principles.
       Only the selected cell (if any) is composed on top per frame. */
    if (vid_cache_ok()) {
        if (!g_bg_valid) {
            desktop_fill_bg();
            for (i = 0; i < n; ++i)
                draw_cell(i, FALSE);
            vid_cache_store();
            g_bg_valid = TRUE;
        } else {
            vid_cache_restore();
        }
        if (g_sel >= 0 && g_sel < n)
            draw_cell(g_sel, TRUE);
        return;
    }

    /* No cache (the DOS block did not fit): compose from scratch. */
    desktop_fill_bg();
    for (i = 0; i < n; ++i)
        draw_cell(i, (i == g_sel) ? TRUE : FALSE);
}

/* TRUE when the scene cache holds a valid composed background, so a caller
   may restore a piece of it instead of rebuilding the desktop. */
bool_t desktop_cache_ready(void)
{
    return (vid_cache_ok() && g_bg_valid) ? TRUE : FALSE;
}

/* The part of desktop_draw() that goes ON TOP of a restored cache.  The
   caller has already put the cached background back (possibly only a
   rectangle of it), so all that is left is the selected cell. */
void desktop_draw_over_cache(void)
{
    int n = desktop_icon_count();
    if (g_sel >= 0 && g_sel < n)
        draw_cell(g_sel, TRUE);
}

static void launcher_rect(Rect *r)
{
    rect_set(r, LAUNCH_X, TASKBAR_Y + 1, LAUNCH_W, TASKBAR_H - 2);
}

/* The rectangle of taskbar window-button i (of `count`), between the
   launcher button and the clock.  Shared by the drawer and the hit test. */
static void bar_button_rect(int i, int count, Rect *r)
{
    int left = LAUNCH_X + LAUNCH_W + 2;
    int right = SCREEN_W - 3;
    int bw, maxw;
    if (g_cfg != NULL && g_cfg->clock_enabled)
        right = SCREEN_W - CLOCK_W - 5 - SPKR_W;   /* leave room for the tray */
    if (count < 1) count = 1;
    bw = (right - left) / count;
    maxw = font_adv() * 16;
    if (bw > maxw) bw = maxw;
    rect_set(r, left + i * bw, TASKBAR_Y + 2, bw - 1, TASKBAR_H - 4);
}

static void draw_taskbar_buttons(void)
{
    int n = wm_bar_count(), i;
    for (i = 0; i < n; ++i) {
        Rect r;
        char lbl[18];
        const char *t = wm_bar_title(i);
        int maxch, k;
        bar_button_rect(i, n, &r);
        if (r.w < font_adv() * 2)
            continue;                       /* too crowded to label         */
        maxch = r.w / font_adv() - 1;
        if (maxch < 1) maxch = 1;
        if (maxch > (int)sizeof(lbl) - 1) maxch = (int)sizeof(lbl) - 1;
        for (k = 0; k < maxch && t[k]; ++k) lbl[k] = t[k];
        lbl[k] = '\0';
        ui_button(&r, lbl, wm_bar_active(i));   /* pressed while focused    */
        if (wm_bar_min(i))                      /* a dot marks a minimized  */
            vid_fillrect(r.x + 2, r.y + 2, 2, 2, C_DKGRAY);
    }
}

/* Screen rectangle of taskbar button i (of count) - the minimize/restore
   zoom animations fly to and from the window's actual button. */
void desktop_bar_button_rect(int i, int count, Rect *r)
{
    bar_button_rect(i, count, r);
}

/* Taskbar window button under (x,y), or -1. */
int desktop_taskbar_button_at(int x, int y)
{
    int n = wm_bar_count(), i;
    for (i = 0; i < n; ++i) {
        Rect r;
        bar_button_rect(i, n, &r);
        if (rect_contains(&r, x, y))
            return i;
    }
    return -1;
}

/* A small system-tray speaker (~9x8).  A red slash marks it muted, exactly
   like the Windows-95 volume icon reflecting the sound setting. */
static void draw_tray_speaker(int x, int y, bool_t muted)
{
    int i;
    vid_fillrect(x,     y + 2, 2, 4, C_BLACK);   /* driver body        */
    vid_fillrect(x + 2, y + 1, 1, 6, C_BLACK);   /* cone               */
    vid_fillrect(x + 3, y,     1, 8, C_BLACK);   /* cone rim           */
    if (muted) {
        for (i = 0; i < 8; ++i)                  /* a red mute slash    */
            vid_pixel(x + 1 + i, y + i, C_RED);
    } else {
        vid_pixel(x + 5, y + 1, C_DKGRAY);       /* two sound waves    */
        vid_pixel(x + 6, y + 2, C_DKGRAY);
        vid_pixel(x + 6, y + 5, C_DKGRAY);
        vid_pixel(x + 5, y + 6, C_DKGRAY);
        vid_pixel(x + 7, y + 3, C_DKGRAY);
        vid_pixel(x + 7, y + 4, C_DKGRAY);
    }
}

void desktop_draw_taskbar(bool_t launcher_pressed)
{
    Rect lb;

    /* The bar itself. */
    vid_fillrect(0, TASKBAR_Y, SCREEN_W, TASKBAR_H, C_FACE);
    ui_raise(0, TASKBAR_Y - 1, SCREEN_W, TASKBAR_H + 1);

    /* Launcher - a Windows-95 Start button: a 1px raised frame (sunken while
       the Dominus menu is open) holding the crisp Castalia castle and the
       "Inicio" caption.  Its contents nudge down-right by a pixel when held. */
    launcher_rect(&lb);
    {
        int o = launcher_pressed ? 1 : 0;
        u8  tl = launcher_pressed ? C_SHADOW : C_HILIGHT;
        u8  br = launcher_pressed ? C_HILIGHT : C_SHADOW;
        vid_fillrect(lb.x, lb.y, lb.w, lb.h, C_FACE);
        vid_hline(lb.x, lb.y, lb.w, tl);                 /* raised bevel */
        vid_vline(lb.x, lb.y, lb.h, tl);
        vid_hline(lb.x, lb.y + lb.h - 1, lb.w, br);
        vid_vline(lb.x + lb.w - 1, lb.y, lb.h, br);
        ui_start_castle(lb.x + 2 + o, lb.y + 1 + o, 1);  /* 13x10 castle */
        font_draw(lb.x + 2 + 13 + 3 + o,
                  lb.y + (lb.h - font_h()) / 2 + o, START_LABEL, C_BLACK);
    }

    /* One button per open window. */
    draw_taskbar_buttons();

    /* Clock (sunken well, right side). */
    if (g_cfg != NULL && g_cfg->clock_enabled) {
        /* The readout only changes once a minute, but the taskbar is
           recomposed many times a second (every window tick, drag and
           menu).  Cache the formatted string and re-derive it only when
           the minute actually rolls over - an INT 21h call plus a printf
           per compose was pure waste. */
        static char buf[8] = "";
        static unsigned long last_tick = 0;
        int cx = SCREEN_W - CLOCK_W - 3;
        unsigned long now = sys_ticks();
        /* The formatted string was already cached, but the INT 21h round
           trip that fed it was not: it ran on EVERY taskbar repaint -
           about 18 a second - to notice a change that happens once a
           minute.  The BIOS tick counter is a plain memory read, so ask
           DOS at most once a second and let the cache serve the rest. */
        if (buf[0] == '\0' || now - last_tick >= 18UL) {
            struct dostime_t t;
            last_tick = now;
            _dos_gettime(&t);
            sprintf(buf, "%02u:%02u", t.hour, t.minute);
        }
        draw_tray_speaker(cx - SPKR_W + 1, TASKBAR_Y + (TASKBAR_H - 8) / 2,
                          g_cfg->sound_enabled ? FALSE : TRUE);
        vid_fillrect(cx, TASKBAR_Y + 2, CLOCK_W, TASKBAR_H - 4, C_FACE);
        ui_sink(cx, TASKBAR_Y + 2, CLOCK_W, TASKBAR_H - 4);
        ui_text_center(cx, TASKBAR_Y + 4, CLOCK_W, buf, C_BLACK);
    }
}

/* The first-run "Click here to begin" balloon: a yellow tooltip above the
   taskbar with a red arrow pointing down at the Start button's castle,
   exactly the nudge Windows 95 gave a brand-new desktop.  main.c shows it
   until the first click or key. */
void desktop_draw_start_hint(void)
{
    /* Clear of the icon column, not on top of it.  At LAUNCH_X + 6 the
       balloon's top border always cut through the third icon's label
       (the label band runs to y 165 and the balloon sat at y 162) - and
       the tail was a pure red arrow, a third accent colour on a blue
       desktop with a yellow balloon, that overshot into the taskbar.
       Now: right of the grid, with a black tail in the balloon's own
       border colour angling down to the Start castle. */
    const char *msg = "Click here to begin";
    int tw = font_text_width(msg);
    int bw = tw + 10, bh = font_h() + 6;
    int bx = GRID_X + CELL_W + 10;
    int by = TASKBAR_Y - bh - 12;
    int ox = LAUNCH_X + 2 + 6;             /* the Start castle's centre      */
    int tipy = TASKBAR_Y - 3;              /* just above the button          */
    int i, steps;
    if (bx + bw > SCREEN_W - 4) bx = SCREEN_W - 4 - bw;
    if (bx < 2) bx = 2;
    if (by < 2) by = 2;
    ui_shadow  (bx, by, bw, bh);
    vid_fillrect(bx, by, bw, bh, C_YELLOW);
    vid_rect    (bx, by, bw, bh, C_BLACK);
    font_draw(bx + 5, by + 3, msg, C_BLACK);

    /* Leader line from the balloon's bottom-left corner to the castle.
       Stepped along X, which is the LONG axis here - stepping the short
       one left a dotted trail with 12px gaps instead of a line. */
    steps = (bx + 4) - ox;
    if (steps > 0) {
        int y0 = by + bh, dy = tipy - y0;
        for (i = 0; i <= steps; ++i)
            vid_pixel(bx + 4 - i, y0 + dy * i / steps, C_BLACK);
    }
    for (i = 0; i < 3; ++i)                /* a small notch at the castle    */
        vid_hline(ox - (2 - i), tipy + i, 2 * (2 - i) + 1, C_BLACK);
}

int desktop_icon_at(int x, int y)
{
    int i, n = desktop_icon_count();
    for (i = 0; i < n; ++i) {
        int cx, cy;
        cell_origin(i, &cx, &cy);
        /* Hit area covers the icon and its label. */
        if (x >= cx && x < cx + CELL_W && y >= cy && y < cy + CELL_H)
            return i;
    }
    return -1;
}

bool_t desktop_launcher_hit(int x, int y)
{
    Rect lb;
    launcher_rect(&lb);
    return rect_contains(&lb, x, y);
}

void desktop_launcher_anchor(int *x, int *bottom)
{
    *x = LAUNCH_X;
    *bottom = TASKBAR_Y;     /* menu rises from the top of the taskbar    */
}

void desktop_select(int idx)  { g_sel = idx; }
int  desktop_selected(void)   { return g_sel; }

/* Arrow keys walk the icon grid column by column (the same shape the
   icons are laid out in), ENTER launches the selection.  The first
   arrow press with no selection lights up the first icon. */
bool_t desktop_key(int key, int *launch)
{
    int n = desktop_icon_count();
    int i = g_sel;

    *launch = -1;
    if (n <= 0)
        return FALSE;

    if (key == KEY_ENTER) {
        if (i < 0)
            return FALSE;
        *launch = i;
        return TRUE;
    }

    if (key != KEY_UP && key != KEY_DOWN &&
        key != KEY_LEFT && key != KEY_RIGHT)
        return FALSE;

    if (i < 0) {
        g_sel = 0;                     /* first arrow press: light icon 1  */
        return TRUE;
    }

    if (key == KEY_UP) {
        if (i % ROWS_PER_COL > 0) i -= 1;
    } else if (key == KEY_DOWN) {
        if (i % ROWS_PER_COL < ROWS_PER_COL - 1 && i + 1 < n) i += 1;
    } else if (key == KEY_LEFT) {
        if (i >= ROWS_PER_COL) i -= ROWS_PER_COL;
    } else {
        if (i + ROWS_PER_COL < n) i += ROWS_PER_COL;
    }
    g_sel = i;
    return TRUE;
}

/* Screen footprint of icon cell i, covering its focus outline and label
   band - so selecting/deselecting an icon can blit just this rectangle. */
void desktop_cell_rect(int i, Rect *r)
{
    int cx, cy, label_y, bottom;
    cell_origin(i, &cx, &cy);
    label_y = cy + ICON_SIZE + 2;
    bottom  = label_y - 1 + font_h() * 2 + 3;
    rect_set(r, cx, cy - 2, CELL_W, bottom - (cy - 2));
}

/* Screen footprint of the taskbar clock, so a clock tick blits just that. */
void desktop_clock_rect(Rect *r)
{
    int cx = SCREEN_W - CLOCK_W - 3;
    rect_set(r, cx, TASKBAR_Y + 2, CLOCK_W, TASKBAR_H - 4);
}

/* Clickable footprint of the tray speaker (just left of the clock). */
void desktop_tray_speaker_rect(Rect *r)
{
    int cx = SCREEN_W - CLOCK_W - 3;
    rect_set(r, cx - SPKR_W, TASKBAR_Y + 2, SPKR_W, TASKBAR_H - 4);
}
