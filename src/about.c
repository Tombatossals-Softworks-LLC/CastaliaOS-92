/* ======================================================================
 * about.c - The animated, tabbed About box for CASTALIA/386
 * ====================================================================== */
#include <stdio.h>
#include <string.h>
#include "about.h"
#include "video.h"
#include "ui.h"
#include "font.h"
#include "system.h"
#include "keyboard.h"
#include "desktop.h"

#define T_ABOUT 0
#define T_CRED  1
#define T_HIST  2
#define T_SYS   3
#define T_BUILT 4
#define T_LIC   5
#define NTABS   6

static const char * const TABS[NTABS] =
    { "About", "Credits", "History", "System", "Built", "License" };

static int    g_tab   = T_ABOUT;
static unsigned g_phase = 0;
static int    g_scroll = 0;
static int    g_logo_hits = 0;
static bool_t g_secret = FALSE;

/* Konami: up up down down left right left right B A. */
static const int KON[10] = {
    KEY_UP, KEY_UP, KEY_DOWN, KEY_DOWN,
    KEY_LEFT, KEY_RIGHT, KEY_LEFT, KEY_RIGHT, 'b', 'a'
};
static int    g_kidx  = 0;
static bool_t g_party = FALSE;
static unsigned g_party_t = 0;

static Rect g_tabrect[NTABS];
static Rect g_logo_rect;

/* Rolling credits (loops). */
static const char * const CRED[] = {
    "Castalia 92", "",
    "created by", "DAVE ABELLAN", "",
    "licensed through", "Tombatossals", "Softworks LLC", "",
    "- - -", "",
    "a from-scratch DOS", "graphical environment", "",
    "no engine, no toolkit,", "no third-party code", "-", "just C and the",
    "bare 386 metal", "",
    "with thanks to", "the demoscene, the", "DOS pioneers,", "and you", "",
    "Anno MMXXVI", "", "", ""
};
#define NCRED (int)(sizeof(CRED) / sizeof(CRED[0]))

static const char * const BUILT[] = {
    "30,000+ lines of C",
    "65 source modules, 0 libs",
    "Open Watcom C, 386 real mode",
    /* -we, not -wx.  -wx is the warning LEVEL; the compiler prints the
       warning and exits 0.  The build was corrected everywhere else when
       that was found and this box was missed, which is why the numbers
       above and this line are now checked by ci/consistency.sh. */
    "warnings as errors (-we)",
    "",
    "hand-rolled here:",
    " VGA 13h + 12h drivers",
    " GIF/LZW + FLI/FLC video",
    " WAV, SB DAC, OPL FM synth",
    " General-MIDI voice bank",
    " ICO/ICN icon loaders",
    " window mgr + compositor",
    "",
    "target: 386SX, 1 MB, DOS 3.3"
};
#define NBUILT (int)(sizeof(BUILT) / sizeof(BUILT[0]))

/* The road so far - one line per milestone (rolls like the credits). */
static const char * const HIST[] = {
    "THE ROAD SO FAR", "",
    "0.25  the castle crest", "      and .ICO icons", "",
    "0.28  OPL FM music -", "      nine real voices", "",
    "0.30  drums on channel 10,", "      crisp SB WAV", "",
    "0.31  the exact Windows-95", "      look, dialled in", "",
    "0.32  a cascading Start", "      menu with flyouts", "",
    "0.33  real message boxes", "",
    "0.34  My Computer with", "      drive icons", "",
    "0.35  a full General-MIDI", "      voice bank", "",
    "0.36  the Castalia orb,", "      a Winamp Gramophone,",
    "      Pong, the Calendar,", "      and 2048", "",
    "0.38  reborn as Castalia 92,", "      a crisp Start castle,",
    "      pixel-art icons, a", "      brutal Oracle bench,",
    "      six new wallpapers,", "      Find File, and the",
    "      Picture Show gallery", "",
    "0.39  a tumbling wire-frame", "      cube joins the", "      Light Show", "",
    "0.40  a voxel-terrain flyover", "      lands in the Light Show", "",
    "0.41  the System Inspector,", "      rebuilt as one live", "      dashboard", "",
    "0.42  a six-test Benchmark", "      with a composite score", "",
    "0.43  the Gramophone learns", "      to play a folder", "      playlist", "",
    "0.44  a leaner app set -", "      the screensaver rolls", "      on", "",
    "0.45  Settings gains tabs", "      and a swatch for", "      every theme", "",
    "0.46  CORRIDOR - a raycast", "      maze joins the", "      screensaver", "",
    "0.47  three new themes:", "      Sunset, Forest", "      and Hot Dog", "",
    "0.48  KALEIDOSCOPE and", "      three more themes:", "      Slate, Sakura, DOS", "",
    "0.49  point at a theme -", "      the Settings strip", "      previews it live", "",
    "0.50  the Gramophone's", "      analyzer learns", "      gravity", "",
    "0.51  Covox + Sound", "      Source, and a Start", "      button in any tongue", "",
    "0.52  the DGROUP diet:", "      3.5 KB of near data", "      moved out or trimmed", "",
    "0.53  Corral, the Typing", "      Tutor, Ribbons - and", "      the 38-fix audit", "",
    "what a ride", "", "", ""
};
#define NHIST (int)(sizeof(HIST) / sizeof(HIST[0]))

static const char * const LIC[] = {
    "MIT License",
    "",
    "(C) 2026",
    "Tombatossals Softworks LLC",
    "",
    "Created by Dave Abellan.",
    "Licensed through",
    "Tombatossals Softworks LLC.",
    "",
    "Permission is granted, free",
    "of charge, to use, copy and",
    "modify this software.  It is",
    "provided \"as is\", without",
    "warranty of any kind."
};
#define NLIC (int)(sizeof(LIC) / sizeof(LIC[0]))

void about_open(void)
{
    g_tab = T_ABOUT;
    g_scroll = 0;
    g_kidx = 0;
    g_party = FALSE;
    g_logo_hits = 0;
    g_secret = FALSE;
}

/* ---- animation ------------------------------------------------------- */
bool_t about_tick(void)
{
    ++g_phase;
    if (g_party) {
        ++g_party_t;
        return TRUE;
    }
    if (g_tab == T_CRED || g_tab == T_HIST) {
        ++g_scroll;
        return TRUE;
    }
    if (g_tab == T_SYS)                 /* live tick counter               */
        return ((g_phase & 3) == 0) ? TRUE : FALSE;
    return FALSE;                       /* the About banner is static      */
}

/* ---- a deterministic star/confetti field (no RNG) -------------------- */
static int hashx(int i) { return ((i * 71 + 13) & 255); }
static int hashy(int i) { return ((i * 37 + 7) & 127); }

/* Scale-2 caption text with a deep emboss: black shadow, blue mid, white
   face - the poor man's chrome, and it costs three glyph passes. */
static void big2(int x, int y, const char *s, u8 face)
{
    int cx = x;
    while (*s) {
        int rows, row, col;
        const u8 far *g = font_glyph((unsigned char)*s, &rows);
        for (row = 0; row < rows; ++row) {
            u8 bits = g[row];
            for (col = 0; col < 8; ++col)
                if (bits & (0x80 >> col)) {
                    vid_fillrect(cx + col*2 + 2, y + row*2 + 2, 2, 2, C_BLACK);
                    vid_fillrect(cx + col*2 + 1, y + row*2 + 1, 2, 2, C_BLUE);
                    vid_fillrect(cx + col*2,     y + row*2,     2, 2, face);
                }
        }
        cx += font_adv() * 2;
        ++s;
    }
}

static void draw_confetti(const Rect *cl)
{
    int i;
    static const u8 COL[6] = { C_RED, C_YELLOW, C_GREEN, C_LTBLUE,
                               C_CYAN, C_WHITE };
    for (i = 0; i < 40; ++i) {         /* long: 255*(w-8) tops 16 bits     */
        int cx = cl->x + 4 + (int)((long)hashx(i) * (cl->w - 8) / 256);
        int cy = cl->y + ((hashy(i) + (int)g_party_t * 3 + i * 2)
                          % (cl->h - 4));
        vid_fillrect(cx, cl->y + cy - cl->y, 2, 2, COL[i % 6]);
    }
}

/* ---- tabs ------------------------------------------------------------ */
static void draw_tabs(const Rect *cl)
{
    int i, bw = (cl->w - 4) / NTABS, bh = font_h() + 4;
    for (i = 0; i < NTABS; ++i) {
        rect_set(&g_tabrect[i], cl->x + 2 + i * bw, cl->y + 2, bw - 1, bh);
        ui_tab(&g_tabrect[i], TABS[i], (i == g_tab) ? TRUE : FALSE);
    }
    ui_tab_page_top(cl->x + 2, cl->y + 2 + bh - 1, cl->w - 4,
                    (g_tab >= 0 && g_tab < NTABS) ? &g_tabrect[g_tab]
                                                  : (const Rect *)0);
}

static void page_about(const Rect *cl, int y0)
{
    Rect banner;
    int cx = cl->x, cw = cl->w, lh = font_h() + 1;
    int tw2;

    /* The banner shows the real desktop wallpaper (the default backdrop),
       not a scene of our own - a window straight onto the desktop picture,
       with the embossed marque floating over it. */
    rect_set(&banner, cl->x + 4, y0, cl->w - 8, 52);
    desktop_blit_backdrop(banner.x, banner.y, banner.w, banner.h);
    ui_sink(banner.x, banner.y, banner.w, banner.h);
    tw2 = (int)strlen(CAST_NAME) * font_adv() * 2;
    big2(banner.x + (banner.w - tw2) / 2,
         banner.y + (banner.h - font_h() * 2) / 2 - 3, CAST_NAME, C_YELLOW);
    ui_text_center(banner.x, banner.y + banner.h - font_h() - 3, banner.w,
                   CAST_TAGLINE, C_WHITE);
    rect_set(&g_logo_rect, banner.x, banner.y, banner.w, banner.h);
    y0 = banner.y + banner.h + 4;

    /* A fuller identity block beneath the banner. */
    ui_text_center(cx, y0, cw,
                   "Version " CAST_VERSION "  (build MMXXVI)", C_BLACK);
    y0 += lh;
    ui_text_center(cx, y0, cw, "a from-scratch graphical", C_DKGRAY); y0 += lh;
    ui_text_center(cx, y0, cw, "environment for DOS 386", C_DKGRAY); y0 += lh + 1;
    ui_text_center(cx, y0, cw, "created by Dave Abellan", C_TITLE);   y0 += lh;
    ui_text_center(cx, y0, cw,
                   CAST_COMPANY " LLC", C_DKGRAY);                    y0 += lh;
    ui_text_center(cx, y0, cw,
                   "30,000+ lines of C, 65 modules",
                   C_DKGRAY);                                         y0 += lh;
    if (g_secret)
        ui_text_center(cx, y0, cw, "Fiat lux!  - D.A.", C_RED);
    else
        ui_text_center(cx, y0, cw,
                       "TAB / 1-6 tabs  -  ESC closes", C_DKGRAY);
}

static void page_scroll_list(const Rect *cl, int y0, const char * const *ln,
                             int n, bool_t rolling)
{
    Rect box;
    int lh = font_h() + 2, i;
    int top = y0, boxh = cl->y + cl->h - y0 - 3;
    rect_set(&box, cl->x + 4, top, cl->w - 8, boxh);
    vid_fillrect(box.x, box.y, box.w, box.h, C_CREAM);
    ui_sink(box.x, box.y, box.w, box.h);
    for (i = 0; i < n; ++i) {
        int ty = box.y + 3 + i * lh - (rolling ? (g_scroll % (n * lh + boxh)) : 0);
        /* font_draw clips only to the screen, so a line that is only PARTLY
           inside the pad would spill over the sink bevel - into the tabs
           above or the frame below.  Draw a line only while it fits wholly
           within the pad's interior, so the roll starts and ends exactly at
           the cream box's edges. */
        if (ty >= box.y + 2 && ty + font_h() <= box.y + box.h - 3) {
            u8 col = (ln[i][0] && (ln[i][0] < 'a' || (ln[i][1] >= 'A' &&
                     ln[i][1] <= 'Z' && ln[i][0] >= 'A' && ln[i][0] <= 'Z')))
                     ? C_TITLE : C_BLACK;
            if (strcmp(ln[i], "DAVE ABELLAN") == 0) col = C_RED;
            ui_text_center(box.x, ty, box.w, ln[i], col);
        }
    }
}

static void page_system(const Rect *cl, int y0)
{
    char b[36];
    unsigned long freek = 0, totk = 0;
    int lh = font_h() + 2, x = cl->x + 8, y = y0 + 2;
    unsigned long ram = system_total_ram_kb();

    font_draw(x, y, "Live system", C_TITLE); y += lh + 2;
    sprintf(b, "RAM total : %lu.%lu MB", ram / 1024UL, (ram % 1024UL) / 103UL);
    font_draw(x, y, b, C_BLACK); y += lh;
    sprintf(b, "Convent.  : %u KB", system_conventional_kb());
    font_draw(x, y, b, C_BLACK); y += lh;
    sprintf(b, "Free conv.: %u KB", system_free_conv_kb());
    font_draw(x, y, b, C_BLACK); y += lh;
    system_disk_kb(&freek, &totk);
    sprintf(b, "Disk free : %lu MB", freek / 1024UL);
    font_draw(x, y, b, C_BLACK); y += lh;
    sprintf(b, "Video     : %s", video_is_big() ? "640x480x16"
                                                 : "320x200x256");
    font_draw(x, y, b, C_BLACK); y += lh;
    sprintf(b, "Ticks     : %lu", sys_ticks());     /* live, ~18/sec       */
    font_draw(x, y, b, C_GREEN); y += lh;
}

static void page_list(const Rect *cl, int y0, const char * const *ln, int n,
                      const char *head)
{
    int lh = font_h() + 1, x = cl->x + 8, y = y0 + 2, i;
    font_draw(x, y, head, C_TITLE); y += lh + 2;
    for (i = 0; i < n; ++i) {
        font_draw(x, y, ln[i], (ln[i][0] == ' ') ? C_DKGRAY : C_BLACK);
        y += lh;
    }
}

void about_draw(const Rect *cl)
{
    int y0;
    draw_tabs(cl);
    y0 = cl->y + font_h() + 8;
    switch (g_tab) {
    case T_ABOUT: page_about(cl, y0);                              break;
    case T_CRED:  page_scroll_list(cl, y0, CRED, NCRED, TRUE);     break;
    case T_HIST:  page_scroll_list(cl, y0, HIST, NHIST, TRUE);     break;
    case T_SYS:   page_system(cl, y0);                             break;
    case T_BUILT: page_list(cl, y0, BUILT, NBUILT, "Built with");  break;
    case T_LIC:   page_list(cl, y0, LIC, NLIC, "License");         break;
    }
    if (g_party) {
        draw_confetti(cl);
        ui_text_center(cl->x, cl->y + cl->h - font_h() - 3, cl->w,
                       "PARTY MODE - Dave was here!", C_RED);
    }
}

bool_t about_click(const Rect *cl, int mx, int my)
{
    int i;
    (void)cl;                      /* rects were cached by the last draw   */
    for (i = 0; i < NTABS; ++i)
        if (rect_contains(&g_tabrect[i], mx, my)) {
            if (g_tab != i) { g_tab = i; g_scroll = 0; }
            return TRUE;
        }
    if (g_tab == T_ABOUT && rect_contains(&g_logo_rect, mx, my)) {
        if (++g_logo_hits >= 5) { g_secret = TRUE; }
        return TRUE;
    }
    return FALSE;
}

bool_t about_key(int key)
{
    /* tab navigation */
    if (key == KEY_TAB || key == KEY_RIGHT) {
        /* right also feeds the konami; still advance the tab visibly */
    }
    /* konami tracker */
    if (key == KON[g_kidx]) {
        if (++g_kidx >= 10) {
            g_kidx = 0; g_party = TRUE; g_party_t = 0;
            return TRUE;
        }
    } else {
        g_kidx = (key == KON[0]) ? 1 : 0;
    }
    if (key == KEY_TAB) {
        g_tab = (g_tab + 1) % NTABS; g_scroll = 0; return TRUE;
    }
    if (key >= '1' && key <= '0' + NTABS) {
        g_tab = key - '1'; g_scroll = 0; return TRUE;
    }
    if (key == KEY_LEFT  && g_tab > 0)         { --g_tab; g_scroll = 0; return TRUE; }
    if (key == KEY_RIGHT && g_tab < NTABS - 1) { ++g_tab; g_scroll = 0; return TRUE; }
    return (g_kidx > 0) ? TRUE : FALSE;
}
