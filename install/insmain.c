/* ======================================================================
 * insmain.c - INSTALL.EXE, the graphical installer for Castalia 92
 * ----------------------------------------------------------------------
 * A one-screen setup wizard drawn with the same video / font / ui
 * modules the desktop uses, so it looks like Castalia before Castalia
 * is even installed.  Keyboard-driven (no mouse driver needed at
 * install time): TAB walks the fields, the target path is editable,
 * SPACE toggles the AUTOEXEC.BAT PATH option, ENTER installs, ESC
 * quits.  Copies the same file set INSTALL.BAT copies, with a live
 * progress bar, and never overwrites an existing CASTALIA.INI.
 *
 * Built as a SECOND executable from the same object files - see the
 * INSTALL.EXE rule in the Makefile and install/install.lnk.  Mode 13h
 * only: the installer has no reason to need 640x480.
 * ====================================================================== */
#include <stdio.h>
#include <string.h>
#include <direct.h>    /* mkdir                                          */
#include <dos.h>       /* _dos_findfirst (the assets sweep)              */
#include "castalia.h"
#include "video.h"
#include "font.h"
#include "ui.h"
#include "keyboard.h"
#include "system.h"

#define TGT_MAX  36
#define F_PATH   0                     /* the focus ring                  */
#define F_CHECK  1
#define F_GO     2
#define F_EXIT   3

static char   g_tgt[TGT_MAX] = "C:\\CASTALIA";
static bool_t g_addpath = FALSE;
static int    g_focus   = F_PATH;
static char   g_status[42] = "";

static char g_buf[8192];               /* the copy buffer (own DGROUP)    */

/* ---- painting --------------------------------------------------------- */

static void panel_rect(Rect *r)
{
    rect_set(r, 29, 22, 262, 150);
}

static void draw_setup(void)
{
    Rect p, b;
    int x, y, lh = font_h() + 3;
    char line[44];

    panel_rect(&p);
    vid_fillrect(0, 0, SCREEN_W, SCREEN_H, C_DESKTOP);
    ui_shadow(p.x, p.y, p.w, p.h);
    ui_fill_face(p.x, p.y, p.w, p.h);
    ui_raise(p.x, p.y, p.w, p.h);

    /* Title strip with the castle crest, like any Castalia window. */
    vid_title_bar(p.x + 3, p.y + 3, p.w - 6, font_h() + 4, TRUE);
    ui_start_castle(p.x + 6, p.y + 4, 1);
    font_draw(p.x + 22, p.y + 5, "Castalia 92 Setup", C_WHITE);

    x = p.x + 10;
    y = p.y + font_h() + 14;
    font_draw(x, y, "This installs Castalia 92 onto", C_BLACK); y += lh;
    font_draw(x, y, "your hard disk.", C_BLACK);                y += lh + 4;

    /* Target path, editable while focused. */
    font_draw(x, y + 3, "Target", C_TITLE);
    {
        int wx = x + font_adv() * 7;
        int ww = p.w - (wx - p.x) - 10;
        vid_fillrect(wx, y, ww, font_h() + 5, C_WHITE);
        ui_sink(wx, y, ww, font_h() + 5);
        sprintf(line, "%s%s", g_tgt, (g_focus == F_PATH) ? "_" : "");
        font_draw(wx + 4, y + 3, line, C_BLACK);
        if (g_focus == F_PATH)
            vid_rect(wx - 1, y - 1, ww + 2, font_h() + 7, C_TITLE);
    }
    y += font_h() + 10;

    /* The AUTOEXEC.BAT PATH option. */
    sprintf(line, "[%c] Add PATH to AUTOEXEC.BAT", g_addpath ? 'X' : ' ');
    font_draw(x, y, line, (g_focus == F_CHECK) ? C_TITLE : C_BLACK);
    if (g_focus == F_CHECK)
        vid_rect(x - 3, y - 3, font_text_width(line) + 6, font_h() + 6,
                 C_TITLE);
    y += lh + 6;

    /* Install / Exit buttons. */
    rect_set(&b, p.x + 44, y, 84, font_h() + 7);
    ui_button(&b, "Install", (g_focus == F_GO) ? TRUE : FALSE);
    rect_set(&b, p.x + p.w - 44 - 84, y, 84, font_h() + 7);
    ui_button(&b, "Exit", (g_focus == F_EXIT) ? TRUE : FALSE);
    y += font_h() + 14;

    /* Status line and the key legend. */
    if (g_status[0] != '\0')
        ui_text_center(p.x, y, p.w, g_status, C_RED);
    ui_text_center(p.x, p.y + p.h - font_h() - 5, p.w,
                   "TAB - ENTER install - ESC quit", C_DKGRAY);
    vid_present();
}

/* The copy screen: file `cur` of `total` named `name` is on the wire. */
static void draw_progress(int cur, int total, const char *name)
{
    Rect p;
    int x, y, bw;
    char line[44];

    panel_rect(&p);
    vid_fillrect(0, 0, SCREEN_W, SCREEN_H, C_DESKTOP);
    ui_shadow(p.x, p.y, p.w, p.h);
    ui_fill_face(p.x, p.y, p.w, p.h);
    ui_raise(p.x, p.y, p.w, p.h);
    vid_title_bar(p.x + 3, p.y + 3, p.w - 6, font_h() + 4, TRUE);
    ui_start_castle(p.x + 6, p.y + 4, 1);
    font_draw(p.x + 22, p.y + 5, "Installing...", C_WHITE);

    x = p.x + 10;
    y = p.y + font_h() + 20;
    sprintf(line, "Copying %s", name);
    font_draw(x, y, line, C_BLACK);
    y += font_h() + 8;

    bw = p.w - 20;                     /* the bar well                     */
    vid_fillrect(x, y, bw, font_h() + 2, C_WHITE);
    ui_sink(x, y, bw, font_h() + 2);
    if (total > 0)
        vid_fillrect(x + 2, y + 2, (bw - 4) * cur / total, font_h() - 2,
                     C_TITLE);
    sprintf(line, "%d of %d", cur, total);
    ui_text_center(p.x, y + font_h() + 8, p.w, line, C_DKGRAY);
    vid_present();
}

/* ---- the file work ----------------------------------------------------- */

static bool_t file_exists(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL)
        return FALSE;
    fclose(f);
    return TRUE;
}

/* Copy src onto dst; TRUE on success (missing src is the caller's call). */
static bool_t copy_file(const char *src, const char *dst)
{
    FILE *in, *out;
    size_t n;
    in = fopen(src, "rb");
    if (in == NULL)
        return FALSE;
    out = fopen(dst, "wb");
    if (out == NULL) {
        fclose(in);
        return FALSE;
    }
    while ((n = fread(g_buf, 1, sizeof(g_buf), in)) > 0) {
        if (fwrite(g_buf, 1, n, out) != n) {
            fclose(in);
            fclose(out);
            return FALSE;
        }
    }
    fclose(in);
    fclose(out);
    return TRUE;
}

/* The fixed part of the file set (CASTALIA.INI is keep-if-present). */
static const char * const FIXED[4] =
    { "CASTALIA.EXE", "CASTALIA.INI", "CASTSHEL.BAT", "README.TXT" };

static int count_assets(void)
{
    struct find_t ff;
    unsigned rc;
    int n = 0;
    rc = _dos_findfirst("ASSETS\\ICONS\\*.*", _A_RDONLY | _A_ARCH, &ff);
    while (rc == 0) {
        ++n;
        rc = _dos_findnext(&ff);
    }
    return n;
}

/* Append the PATH line to C:\AUTOEXEC.BAT unless it already names the
   target.  1 = added, 0 = already there, -1 = could not write. */
static int append_path_line(void)
{
    FILE *f = fopen("C:\\AUTOEXEC.BAT", "r");
    char line[128], up[128], tgt_up[TGT_MAX];
    int i, found = 0;

    for (i = 0; g_tgt[i] != '\0'; ++i)
        tgt_up[i] = (char)((g_tgt[i] >= 'a' && g_tgt[i] <= 'z')
                           ? g_tgt[i] - 32 : g_tgt[i]);
    tgt_up[i] = '\0';

    if (f != NULL) {
        while (fgets(line, (int)sizeof(line), f) != NULL) {
            for (i = 0; line[i] != '\0' && i < (int)sizeof(up) - 1; ++i)
                up[i] = (char)((line[i] >= 'a' && line[i] <= 'z')
                               ? line[i] - 32 : line[i]);
            up[i] = '\0';
            if (strstr(up, tgt_up) != NULL)
                found = 1;
        }
        fclose(f);
    }
    if (found)
        return 0;
    f = fopen("C:\\AUTOEXEC.BAT", "a");
    if (f == NULL)
        return -1;
    fprintf(f, "SET PATH=%%PATH%%;%s\n", g_tgt);
    fclose(f);
    return 1;
}

/* The install proper: 1 = clean, 0 = ran but some copies failed,
   -1 = could not start (the status line carries the reason). */
static int do_install(void)
{
    char dst[TGT_MAX + 32];
    struct find_t ff;
    unsigned rc;
    int total, cur = 0, i;
    bool_t ok = TRUE;

    if (!file_exists("CASTALIA.EXE")) {
        strcpy(g_status, "CASTALIA.EXE not found here");
        return -1;
    }

    mkdir(g_tgt);
    sprintf(dst, "%s\\ASSETS", g_tgt);        mkdir(dst);
    sprintf(dst, "%s\\ASSETS\\ICONS", g_tgt); mkdir(dst);

    total = count_assets();
    for (i = 0; i < 4; ++i)
        if (file_exists(FIXED[i]))
            ++total;

    for (i = 0; i < 4; ++i) {
        if (!file_exists(FIXED[i]))
            continue;
        ++cur;
        draw_progress(cur, total, FIXED[i]);
        sprintf(dst, "%s\\%s", g_tgt, FIXED[i]);
        if (i == 1 && file_exists(dst))
            continue;                  /* keep the user's CASTALIA.INI     */
        if (!copy_file(FIXED[i], dst))
            ok = FALSE;
    }

    rc = _dos_findfirst("ASSETS\\ICONS\\*.*", _A_RDONLY | _A_ARCH, &ff);
    while (rc == 0) {
        char src[32];
        ++cur;
        draw_progress(cur, total, ff.name);
        sprintf(src, "ASSETS\\ICONS\\%s", ff.name);
        sprintf(dst, "%s\\ASSETS\\ICONS\\%s", g_tgt, ff.name);
        if (!copy_file(src, dst))
            ok = FALSE;
        rc = _dos_findnext(&ff);
    }
    return ok ? 1 : 0;
}

/* The all-done screen; any key leaves it. */
static void draw_done(bool_t ok, int path_state)
{
    Rect p;
    int x, y, lh = font_h() + 3;
    char line[44];

    panel_rect(&p);
    vid_fillrect(0, 0, SCREEN_W, SCREEN_H, C_DESKTOP);
    ui_shadow(p.x, p.y, p.w, p.h);
    ui_fill_face(p.x, p.y, p.w, p.h);
    ui_raise(p.x, p.y, p.w, p.h);
    vid_title_bar(p.x + 3, p.y + 3, p.w - 6, font_h() + 4, TRUE);
    ui_start_castle(p.x + 6, p.y + 4, 1);
    font_draw(p.x + 22, p.y + 5, "Setup complete", C_WHITE);

    x = p.x + 10;
    y = p.y + font_h() + 16;
    font_draw(x, y, ok ? "Castalia 92 is installed."
                       : "Finished, with copy errors.",
              ok ? C_BLACK : C_RED);
    y += lh + 2;
    font_draw(x, y, "Start it with:", C_BLACK); y += lh;
    sprintf(line, "  %s\\CASTALIA", g_tgt);
    font_draw(x, y, line, C_TITLE); y += lh;
    if (g_addpath) {
        if (path_state > 0)       strcpy(line, "PATH added to AUTOEXEC.BAT");
        else if (path_state == 0) strcpy(line, "AUTOEXEC.BAT already set up");
        else                      strcpy(line, "Could NOT edit AUTOEXEC.BAT");
        font_draw(x, y, line, (path_state < 0) ? C_RED : C_BLACK);
        y += lh;
        if (path_state > 0) {
            font_draw(x, y, "(takes effect on the next boot)", C_DKGRAY);
            y += lh;
        }
    }
    ui_text_center(p.x, p.y + p.h - font_h() - 5, p.w,
                   "Press any key to leave setup", C_DKGRAY);
    vid_present();
}

/* ---- keyboard ---------------------------------------------------------- */

static void edit_path(int key)
{
    int n = (int)strlen(g_tgt);
    if (key == 8) {                    /* backspace                        */
        if (n > 0)
            g_tgt[n - 1] = '\0';
        return;
    }
    if (n >= TGT_MAX - 1)
        return;
    if (key >= 'a' && key <= 'z')
        key = key - 'a' + 'A';         /* DOS paths read best in caps      */
    if ((key >= 'A' && key <= 'Z') || (key >= '0' && key <= '9') ||
        key == ':' || key == '\\' || key == '.' || key == '-' ||
        key == '_') {
        g_tgt[n]     = (char)key;
        g_tgt[n + 1] = '\0';
    }
}

int main(void)
{
    bool_t running = TRUE;

    if (!video_init("mode13h")) {
        fprintf(stderr, "INSTALL: could not enter mode 13h "
                        "(needs ~64 KB free).\n");
        return 1;
    }
    video_set_theme("classic");
    font_init();
    kb_flush();

    while (running) {
        int key;
        draw_setup();
        do {
            key = kb_poll();
            if (key == KEY_NONE)
                sys_idle();
        } while (key == KEY_NONE);

        if (key == KEY_ESC)
            break;
        if (key == 9 || key == KEY_DOWN) {          /* TAB: next field    */
            g_focus = (g_focus + 1) & 3;
        } else if (key == KEY_UP) {
            g_focus = (g_focus + 3) & 3;
        } else if (key == KEY_SPACE && g_focus == F_CHECK) {
            g_addpath = g_addpath ? FALSE : TRUE;
        } else if (key == KEY_ENTER) {
            if (g_focus == F_EXIT)
                break;
            g_status[0] = '\0';
            {
                int res = do_install();
                if (res >= 0) {        /* ran: report, clean or not      */
                    int ps = g_addpath ? append_path_line() : 0;
                    draw_done((res > 0) ? TRUE : FALSE, ps);
                    kb_flush();
                    while (kb_poll() == KEY_NONE)
                        sys_idle();
                    running = FALSE;
                }
                /* res < 0: could not start; the status line explains  */
            }
        } else if (g_focus == F_PATH) {
            edit_path(key);
        } else if (key == KEY_SPACE && g_focus == F_GO) {
            g_focus = F_GO;            /* space on Install = press Enter   */
        }
    }

    video_shutdown();
    printf("Castalia 92 Setup - back at DOS.\n");
    return 0;
}
