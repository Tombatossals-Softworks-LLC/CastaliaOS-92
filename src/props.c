/* ======================================================================
 * props.c - Windows-95 property sheets and pop-ups for CASTALIA/386
 * ----------------------------------------------------------------------
 * One small modal engine (sheet_run) drives every box here: push buttons,
 * checkboxes and radio buttons that press in under the mouse and fire on
 * release, Tab/Shift+Tab focus with the dotted cue, Space to toggle, Enter
 * for the default button and Esc to cancel - the same manners as dialog.c.
 * A sheet supplies a painter for its body and, when it needs one, a key,
 * click and idle hook for the parts that are not plain controls (the
 * calendar, the ticking clock).
 * ====================================================================== */
#include <stdio.h>
#include <string.h>
#include <dos.h>       /* _dos_findfirst, _dos_[gs]et{date,time,fileattr}  */
#include <direct.h>    /* getcwd                                          */
#include <i86.h>       /* int86                                           */
#include "props.h"
#include "video.h"
#include "ui.h"
#include "font.h"
#include "mouse.h"
#include "keyboard.h"
#include "music.h"
#include "dialog.h"    /* dialog_note_takeover                            */
#include "system.h"    /* sys_idle                                        */

/* ---- the sheet engine ------------------------------------------------- */
#define CT_BTN    0
#define CT_CHECK  1
#define CT_RADIO  2
#define PS_MAXC   10

#define PK_PASS   (-1)                 /* hook did not want the event      */
#define PK_REDRAW (-2)                 /* hook used it; repaint the sheet  */

typedef struct {
    Rect        r;
    const char *label;
    u8          kind;
    u8          on;                    /* checkbox / radio state           */
    u8          group;                 /* radios: one group per sheet here */
    int         id;                    /* buttons: the value sheet_run returns */
} Ctl;

static Ctl         g_c[PS_MAXC];
static int         g_nc, g_focus, g_press, g_pvis;
static Rect        g_box, g_close;
static int         g_th;
static const char *g_title;
static int         g_cancel_id;        /* what Esc and the close box return */
static void      (*g_paint)(void);
static int       (*g_keyfn)(int key);
static int       (*g_clickfn)(int mx, int my);
static bool_t    (*g_idlefn)(void);

static void sheet_begin(const char *title, int w, int h)
{
    g_title = title;
    g_th    = font_h() + 3;
    if (w > SCREEN_W - 4) w = SCREEN_W - 4;
    if (h > SCREEN_H - 4) h = SCREEN_H - 4;
    rect_set(&g_box, (SCREEN_W - w) / 2, (SCREEN_H - h) / 2, w, h);
    rect_set(&g_close, g_box.x + g_box.w - g_th - 2, g_box.y + 3,
             g_th - 2, g_th - 2);
    g_nc = 0;
    g_focus = 0;
    g_cancel_id = 0;
    g_paint = 0;
    g_keyfn = 0;
    g_clickfn = 0;
    g_idlefn = 0;
}

static Ctl *add_ctl(u8 kind, int x, int y, int w, int h,
                    const char *label, int id)
{
    Ctl *c;
    if (g_nc >= PS_MAXC)
        return &g_c[PS_MAXC - 1];
    c = &g_c[g_nc++];
    rect_set(&c->r, x, y, w, h);
    c->label = label;
    c->kind  = kind;
    c->on    = 0;
    c->group = 0;
    c->id    = id;
    return c;
}

/* A push button sized to the font, at (x,y). */
static Ctl *add_button(int x, int y, const char *label, int id)
{
    return add_ctl(CT_BTN, x, y, font_adv() * 8 + 2, font_h() + 5, label, id);
}

/* A checkbox or radio: the hit area covers the box and its label. */
static Ctl *add_toggle(u8 kind, int x, int y, const char *label, bool_t on)
{
    Ctl *c = add_ctl(kind, x, y, ui_check_size() + 4 + font_text_width(label),
                     ui_check_size(), label, 0);
    c->on = (u8)(on ? 1 : 0);
    return c;
}

static void draw_close(void)
{
    int i, s;
    ui_fill_face(g_close.x, g_close.y, g_close.w, g_close.h);
    ui_raise(g_close.x, g_close.y, g_close.w, g_close.h);
    s = g_close.h - 6;
    if (s < 3) s = 3;
    for (i = 0; i < s; ++i) {
        vid_pixel(g_close.x + 3 + i,         g_close.y + 3 + i, C_BLACK);
        vid_pixel(g_close.x + 3 + s - 1 - i, g_close.y + 3 + i, C_BLACK);
    }
}

static void draw_sheet(void)
{
    int i;
    ui_shadow(g_box.x, g_box.y, g_box.w, g_box.h);
    ui_fill_face(g_box.x, g_box.y, g_box.w, g_box.h);
    ui_raise(g_box.x, g_box.y, g_box.w, g_box.h);
    vid_title_bar(g_box.x + 2, g_box.y + 2, g_box.w - 4, g_th, TRUE);
    font_draw(g_box.x + 6, g_box.y + 2 + (g_th - font_h()) / 2, g_title,
              C_WHITE);
    draw_close();
    if (g_paint)
        g_paint();
    for (i = 0; i < g_nc; ++i) {
        Ctl *c = &g_c[i];
        if (c->kind == CT_BTN) {
            bool_t in = (g_pvis == i) ? TRUE : FALSE;
            if (i == g_focus)
                vid_rect(c->r.x - 2, c->r.y - 2, c->r.w + 4, c->r.h + 4,
                         C_BLACK);
            ui_button(&c->r, c->label, in);
            if (i == g_focus && !in)
                ui_focus_rect(c->r.x + 3, c->r.y + 3, c->r.w - 6, c->r.h - 6);
        } else {
            if (c->kind == CT_CHECK)
                ui_checkbox(c->r.x, c->r.y, c->on ? TRUE : FALSE, c->label);
            else
                ui_radio(c->r.x, c->r.y, c->on ? TRUE : FALSE, c->label);
            if (i == g_focus)
                ui_focus_rect(c->r.x + ui_check_size() + 2, c->r.y - 1,
                              c->r.w - ui_check_size() - 1, c->r.h + 2);
        }
    }
}

/* Act on control i (a click released over it, or Space/Enter on it).
   Returns the button id that ends the sheet, or PK_REDRAW. */
static int activate(int i)
{
    Ctl *c = &g_c[i];
    int k;
    if (c->kind == CT_BTN)
        return c->id;
    if (c->kind == CT_CHECK) {
        c->on = (u8)(c->on ? 0 : 1);
    } else {
        for (k = 0; k < g_nc; ++k)
            if (g_c[k].kind == CT_RADIO && g_c[k].group == c->group)
                g_c[k].on = 0;
        c->on = 1;
    }
    return PK_REDRAW;
}

/* The first button is the default one: Enter on a toggle presses it. */
static int default_button(void)
{
    int i;
    for (i = 0; i < g_nc; ++i)
        if (g_c[i].kind == CT_BTN)
            return i;
    return -1;
}

static int sheet_run(void)
{
    int result = PK_PASS, prev_b, prev_mx, prev_my;
    bool_t redraw = TRUE;

    dialog_note_takeover();            /* the caller repaints everything   */
    g_press = -1;
    g_pvis  = -1;
    prev_b  = mouse_buttons();         /* swallow the click that opened us */
    prev_mx = mouse_x();
    prev_my = mouse_y();

    for (;;) {
        int key, b, mx, my, want, nkey, i, r;
        bool_t held, down, up;

        music_sfx_service();
        mouse_update();
        mx = mouse_x();
        my = mouse_y();
        b  = mouse_buttons();
        held = (b & MB_LEFT) ? TRUE : FALSE;
        down = (held && !(prev_b & MB_LEFT)) ? TRUE : FALSE;
        up   = (!held && (prev_b & MB_LEFT)) ? TRUE : FALSE;

        /* Keys, drained as in every other loop of the shell. */
        nkey = 0;
        while (nkey++ < 16 && result == PK_PASS &&
               (key = kb_poll()) != KEY_NONE) {
            r = g_keyfn ? g_keyfn(key) : PK_PASS;
            if (r >= 0)              { result = r; break; }
            if (r == PK_REDRAW)      { redraw = TRUE; continue; }
            if (key == KEY_ESC) {
                result = g_cancel_id;
            } else if (key == KEY_TAB || key == KEY_BACKTAB) {
                if (g_nc > 0)
                    g_focus = (key == KEY_TAB) ? (g_focus + 1) % g_nc
                                               : (g_focus + g_nc - 1) % g_nc;
                redraw = TRUE;
            } else if (key == KEY_SPACE && g_nc > 0) {
                r = activate(g_focus);
                if (r >= 0) result = r; else redraw = TRUE;
            } else if (key == KEY_ENTER && g_nc > 0) {
                i = (g_c[g_focus].kind == CT_BTN) ? g_focus : default_button();
                if (i >= 0) result = g_c[i].id;
            } else if ((key == KEY_UP || key == KEY_DOWN) && g_nc > 0 &&
                       g_c[g_focus].kind == CT_RADIO) {
                /* Arrows walk a radio group and select as they go. */
                int step = (key == KEY_DOWN) ? 1 : g_nc - 1;
                for (i = (g_focus + step) % g_nc; i != g_focus;
                     i = (i + step) % g_nc)
                    if (g_c[i].kind == CT_RADIO &&
                        g_c[i].group == g_c[g_focus].group)
                        break;
                g_focus = i;
                (void)activate(i);
                redraw = TRUE;
            }
        }

        if (down && result == PK_PASS) {
            g_press = -1;
            for (i = 0; i < g_nc; ++i)
                if (rect_contains(&g_c[i].r, mx, my)) {
                    g_press = i;
                    g_focus = i;
                    redraw = TRUE;
                    break;
                }
            if (g_press < 0 && rect_contains(&g_close, mx, my))
                g_press = PS_MAXC;     /* the close box                    */
            if (g_press < 0 && g_clickfn) {
                r = g_clickfn(mx, my);
                if (r >= 0) result = r;
                else if (r == PK_REDRAW) redraw = TRUE;
            }
        }

        want = -1;
        if (held && g_press >= 0 && g_press < g_nc &&
            g_c[g_press].kind == CT_BTN && rect_contains(&g_c[g_press].r, mx, my))
            want = g_press;
        if (want != g_pvis) { g_pvis = want; redraw = TRUE; }

        if (up && result == PK_PASS) {
            if (g_press == PS_MAXC && rect_contains(&g_close, mx, my)) {
                result = g_cancel_id;
            } else if (g_press >= 0 && g_press < g_nc &&
                       rect_contains(&g_c[g_press].r, mx, my)) {
                r = activate(g_press);
                if (r >= 0) result = r; else redraw = TRUE;
            }
            g_press = -1;
        }

        if (g_idlefn && g_idlefn())
            redraw = TRUE;

        if (redraw) {
            mouse_erase();
            draw_sheet();
            vid_blit_rect(g_box.x, g_box.y, g_box.w + 4, g_box.h + 4);
            mouse_draw();
            redraw = FALSE;
            prev_mx = mx; prev_my = my;
        } else if (mx != prev_mx || my != prev_my) {
            mouse_erase();
            mouse_draw();
            prev_mx = mx; prev_my = my;
        }
        prev_b = b;
        if (result != PK_PASS)
            break;
        sys_idle();
    }
    (void)mouse_take_lpresses();       /* no phantom click on the desktop  */
    (void)mouse_take_rpresses();
    return result;
}

/* ---- small helpers ----------------------------------------------------- */

static char upc(char c)
{
    return (char)((c >= 'a' && c <= 'z') ? c - 32 : c);
}

/* "1,234,567" - DOS-era property sheets spelled sizes out in full. */
static void commas(char *out, unsigned long v)
{
    char tmp[16];
    int n, i, j = 0;
    sprintf(tmp, "%lu", v);
    n = (int)strlen(tmp);
    for (i = 0; i < n; ++i) {
        out[j++] = tmp[i];
        if ((n - 1 - i) % 3 == 0 && i < n - 1)
            out[j++] = ',';
    }
    out[j] = '\0';
}

/* "12,345 bytes (12.0 KB)", or MB above 10 MB. */
static void size_text(char *out, unsigned long bytes)
{
    char c[16];
    commas(c, bytes);
    if (bytes >= 10UL * 1024UL * 1024UL)
        sprintf(out, "%s bytes (%lu MB)", c, bytes / (1024UL * 1024UL));
    else
        sprintf(out, "%s bytes (%lu.%lu KB)", c, bytes / 1024UL,
                (bytes % 1024UL) * 10UL / 1024UL);
}

static const char * const MON[12] = {
    "Jan", "Feb", "Mar", "Apr", "May", "Jun",
    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};

/* A FAT date/time stamp as "1 Oct 2026  13:24". */
static void stamp_text(char *out, unsigned d, unsigned t)
{
    unsigned m = (d >> 5) & 15;
    if (m < 1 || m > 12) m = 1;
    sprintf(out, "%u %s %u  %02u:%02u", d & 31, MON[m - 1], 1980 + (d >> 9),
            t >> 11, (t >> 5) & 63);
}

/* What a 1995 shell would call this kind of file. */
static const char *type_name(const char *name, bool_t dir)
{
    static char other[16];
    const char *dot = strrchr(name, '.');
    static const char * const T[][2] = {
        { "EXE", "Application" },        { "COM", "Application" },
        { "BAT", "MS-DOS Batch File" },  { "TXT", "Text Document" },
        { "INI", "Configuration Settings" },
        { "SYS", "System file" },        { "GIF", "GIF Image" },
        { "ICN", "Castalia Icon" },      { "ICO", "Icon" },
        { "WAV", "Wave Sound" },         { "MID", "MIDI Sequence" },
        { "FLC", "FLIC Animation" },     { "FLI", "FLIC Animation" },
        { "DOC", "Document" },           { "BMP", "Bitmap Image" },
        { "ZIP", "ZIP Archive" },        { "DAT", "Data file" },
        { "HLP", "Help file" },          { "LOG", "Log file" }
    };
    int i;
    if (dir)
        return "File Folder";
    if (dot == NULL || dot[1] == '\0')
        return "File";
    for (i = 0; i < (int)(sizeof(T) / sizeof(T[0])); ++i)
        if (strcmp(dot + 1, T[i][0]) == 0)
            return T[i][1];
    sprintf(other, "%.3s File", dot + 1);
    return other;
}

/* A label at x and its value in a column further right. */
static void field(int x, int vx, int y, const char *label, const char *val)
{
    font_draw(x, y, label, C_BLACK);
    font_draw_n(vx, y, val, (g_box.x + g_box.w - 8 - vx) / font_adv(),
                C_BLACK);
}

/* The etched separator a property sheet draws between its sections. */
static void rule(int x, int y, int w)
{
    vid_hline(x, y,     w, C_SHADOW);
    vid_hline(x, y + 1, w, C_HILIGHT);
}

/* ======================================================================
 * The context menu.
 * ==================================================================== */
int popup_menu(int x, int y, const char * const *items, int n)
{
    int lh = font_h() + 4, w = 0, h, i, sel = -1, result = -2;
    int prev_b, prev_mx, prev_my, start_mx, start_my;
    bool_t redraw = TRUE, armed = FALSE;
    Rect box;

    if (n < 1)
        return -1;
    for (i = 0; i < n; ++i) {
        int tw = font_text_width(items[i]);
        if (tw > w) w = tw;
    }
    w += font_adv() * 4;
    h = 4;
    for (i = 0; i < n; ++i)
        h += (strcmp(items[i], "-") == 0) ? 6 : lh;
    if (x + w > SCREEN_W - 2) x = SCREEN_W - 2 - w;
    if (y + h > SCREEN_H - 2) y = SCREEN_H - 2 - h;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    rect_set(&box, x, y, w, h);

    dialog_note_takeover();
    prev_b  = mouse_buttons();
    prev_mx = start_mx = mouse_x();
    prev_my = start_my = mouse_y();
    for (;;) {
        int key, b, mx, my, hit = -1, yy, nkey = 0;

        mouse_update();
        mx = mouse_x();
        my = mouse_y();
        b  = mouse_buttons();

        /* Which item is under the pointer (separators are not items). */
        yy = box.y + 2;
        for (i = 0; i < n; ++i) {
            int ih = (strcmp(items[i], "-") == 0) ? 6 : lh;
            if (ih == lh && mx >= box.x && mx < box.x + box.w &&
                my >= yy && my < yy + ih)
                hit = i;
            yy += ih;
        }
        if (hit >= 0 && hit != sel && (mx != prev_mx || my != prev_my)) {
            sel = hit;
            redraw = TRUE;
        }

        while (nkey++ < 16 && result == -2 && (key = kb_poll()) != KEY_NONE) {
            if (key == KEY_ESC) {
                result = -1;
            } else if (key == KEY_ENTER || key == KEY_SPACE) {
                if (sel >= 0) result = sel;
            } else if (key == KEY_DOWN || key == KEY_UP) {
                int k, step = (key == KEY_DOWN) ? 1 : n - 1;
                for (k = 0; k < n; ++k) {
                    sel = (sel < 0) ? 0 : (sel + step) % n;
                    if (strcmp(items[sel], "-") != 0)
                        break;
                }
                redraw = TRUE;
            }
        }

        /* A press outside dismisses; a release on an item picks it, so
           press-drag-release works the way Windows' menus do - but only
           once the pointer has moved or a button went down inside: the
           menu opens under the pointer, and letting go of the very
           right-click that opened it must not pick the first item. */
        if (mx != start_mx || my != start_my)
            armed = TRUE;
        if (result == -2 && (b & (MB_LEFT | MB_RIGHT)) &&
            !(prev_b & (MB_LEFT | MB_RIGHT))) {
            if (rect_contains(&box, mx, my)) armed = TRUE;
            else                             result = -1;
        }
        if (result == -2 && armed && !(b & (MB_LEFT | MB_RIGHT)) &&
            (prev_b & (MB_LEFT | MB_RIGHT)) && hit >= 0)
            result = hit;

        if (redraw) {
            mouse_erase();
            ui_shadow(box.x, box.y, box.w, box.h);
            ui_fill_face(box.x, box.y, box.w, box.h);
            ui_raise(box.x, box.y, box.w, box.h);
            yy = box.y + 2;
            for (i = 0; i < n; ++i) {
                if (strcmp(items[i], "-") == 0) {
                    rule(box.x + 3, yy + 2, box.w - 6);
                    yy += 6;
                    continue;
                }
                if (i == sel)
                    vid_fillrect(box.x + 2, yy, box.w - 4, lh, C_TITLE);
                font_draw(box.x + font_adv() * 2, yy + 2, items[i],
                          (i == sel) ? C_WHITE : C_BLACK);
                yy += lh;
            }
            vid_blit_rect(box.x, box.y, box.w + 4, box.h + 4);
            mouse_draw();
            redraw = FALSE;
        } else if (mx != prev_mx || my != prev_my) {
            mouse_erase();
            mouse_draw();
        }
        prev_mx = mx; prev_my = my;
        prev_b = b;
        if (result != -2)
            break;
        sys_idle();
    }
    (void)mouse_take_lpresses();
    (void)mouse_take_rpresses();
    return result;
}

/* ======================================================================
 * File Properties.
 * ==================================================================== */
static char          f_name[13], f_where[68], f_type[24], f_size[40],
                     f_when[24];
static bool_t        f_dir;
static int           f_icon;

static void file_paint(void)
{
    int x = g_box.x + 8, y = g_box.y + g_th + 8, lh = font_h() + 3;
    int vx = x + font_adv() * 10;
    ui_icon(f_icon, x, y);
    font_draw(vx, y + (ICON_SIZE - font_h()) / 2, f_name, C_BLACK);
    y += ICON_SIZE + 4;
    rule(x, y, g_box.w - 16);
    y += 5;
    field(x, vx, y, "Type:", f_type);       y += lh;
    field(x, vx, y, "Location:", f_where);  y += lh;
    if (!f_dir) {
        field(x, vx, y, "Size:", f_size);   y += lh;
    }
    rule(x, y + 1, g_box.w - 16);
    y += 6;
    field(x, vx, y, "Modified:", f_when);   y += lh;
    rule(x, y + 1, g_box.w - 16);
    y += 6;
    font_draw(x, y, "Attributes:", C_BLACK);
}

bool_t props_file(const char *name)
{
    struct find_t ff;
    unsigned want, now;
    int i, x, y, lh = font_h() + 3, cs, r;
    Ctl *ro, *hi, *ar, *sy;

    if (_dos_findfirst(name, _A_NORMAL | _A_RDONLY | _A_HIDDEN | _A_SYSTEM |
                       _A_SUBDIR | _A_ARCH, &ff) != 0) {
        dialog_message("Properties", "DOS cannot find", name);
        return FALSE;
    }
    for (i = 0; i < 12 && ff.name[i]; ++i) f_name[i] = ff.name[i];
    f_name[i] = '\0';
    f_dir = (ff.attrib & _A_SUBDIR) ? TRUE : FALSE;
    f_icon = f_dir ? ICON_FOLDER : ui_icon_for_command(f_name);
    strncpy(f_type, type_name(f_name, f_dir), sizeof(f_type) - 1);
    f_type[sizeof(f_type) - 1] = '\0';
    if (getcwd(f_where, (int)sizeof(f_where)) == NULL)
        f_where[0] = '\0';
    size_text(f_size, (unsigned long)ff.size);
    stamp_text(f_when, ff.wr_date, ff.wr_time);
    now = ff.attrib & (_A_RDONLY | _A_HIDDEN | _A_SYSTEM | _A_ARCH);

    cs = ui_check_size();
    sheet_begin("Properties", font_adv() * 36,
                g_th + ICON_SIZE + lh * (f_dir ? 4 : 5) + cs * 2 +
                font_h() + 50);
    g_paint = file_paint;
    x = g_box.x + 8;
    y = g_box.y + g_th + 8 + ICON_SIZE + 9 + lh * (f_dir ? 2 : 3) + 6 + lh + 6
        + font_h() + 3;
    ro = add_toggle(CT_CHECK, x + font_adv(),       y, "Read-only",
                    (now & _A_RDONLY) ? TRUE : FALSE);
    hi = add_toggle(CT_CHECK, x + font_adv() * 17,  y, "Hidden",
                    (now & _A_HIDDEN) ? TRUE : FALSE);
    ar = add_toggle(CT_CHECK, x + font_adv(),       y + cs + 3, "Archive",
                    (now & _A_ARCH) ? TRUE : FALSE);
    sy = add_toggle(CT_CHECK, x + font_adv() * 17,  y + cs + 3, "System",
                    (now & _A_SYSTEM) ? TRUE : FALSE);
    y = g_box.y + g_box.h - font_h() - 11;
    (void)add_button(g_box.x + g_box.w - font_adv() * 18 - 14, y, "OK", 1);
    (void)add_button(g_box.x + g_box.w - font_adv() * 9 - 8, y, "Cancel", 0);
    g_focus = 4;                       /* OK                               */
    music_sfx(880, 2);

    r = sheet_run();
    if (r != 1)
        return FALSE;
    want = (ro->on ? _A_RDONLY : 0) | (hi->on ? _A_HIDDEN : 0) |
           (ar->on ? _A_ARCH : 0)   | (sy->on ? _A_SYSTEM : 0);
    if (want == now)
        return FALSE;
    /* INT 21h/4301h: on a folder the directory bit must stay out of CX. */
    if (_dos_setfileattr(name, want) != 0) {
        dialog_message("Properties", "Could not change the", "attributes.");
        return FALSE;
    }
    return TRUE;
}

/* ======================================================================
 * Drive Properties, with the tilted pie.
 * ==================================================================== */
static char          d_letter;
static char          d_label[13], d_kind[20];
static unsigned long d_free, d_total;
static bool_t        d_ready;

/* INT 21h AX=4408h: removable or fixed; 4409h DX bit 12: remote. */
static const char *drive_kind(char letter)
{
    union REGS r;
    r.x.ax = 0x4409;
    r.h.bl = (unsigned char)(letter - 'A' + 1);
    int86(0x21, &r, &r);
    if (!r.x.cflag && (r.x.dx & 0x1000))
        return "Network Drive";
    r.x.ax = 0x4408;
    r.h.bl = (unsigned char)(letter - 'A' + 1);
    int86(0x21, &r, &r);
    if (r.x.cflag)
        return "CD-ROM or other";
    return (r.x.ax == 0) ? "Removable Disk" : "Local Disk";
}

/* sin(deg) x1024 for 0..360, Bhaskara's integer approximation (no FPU). */
static long isin(int deg)
{
    long d, v;
    int neg = 0;
    while (deg < 0) deg += 360;
    deg %= 360;
    if (deg > 180) { deg -= 180; neg = 1; }
    d = deg;
    v = 4096L * d * (180 - d) / (40500L - d * (180 - d));
    return neg ? -v : v;
}

/* One horizontal slice of the pie, coloured run by run.  The "used"
   sector runs clockwise from twelve o'clock through `deg` degrees; a point
   is inside it by two cross-product tests against the start and end rays
   (for a sector of 180 or less - a bigger one is the complement of the
   small FREE sector, tested the same way). */
static void pie_row(int cx, int y, int dy, int hw, int rx, int ry,
                    int deg, u8 used, u8 free_c)
{
    long ex = isin(deg), ey = isin(90 - deg);  /* end ray, x1024 */
    long py = -(long)dy * rx / (ry ? ry : 1);  /* back to a circle */
    int dx, run0 = -hw;
    u8 cur = 0, col;
    for (dx = -hw; dx <= hw + 1; ++dx) {
        if (dx <= hw) {
            long px = dx;
            bool_t in;
            if (deg >= 360)      in = TRUE;
            else if (deg <= 0)   in = FALSE;
            else if (deg <= 180) in = (px >= 0 && px * ey - py * ex <= 0)
                                      ? TRUE : FALSE;
            else                 in = (ex * py - ey * px <= 0 && px <= 0)
                                      ? FALSE : TRUE;
            col = in ? used : free_c;
        } else {
            col = (u8)(cur + 1);         /* force the last run out        */
        }
        if (dx == -hw) {
            cur = col;
        } else if (col != cur) {
            vid_hline(cx + run0, y, dx - run0, cur);
            run0 = dx;
            cur = col;
        }
    }
}

static void pie(int cx, int cy, int rx, int ry, int depth, int deg)
{
    int k, dy;
    for (k = depth; k >= 0; --k) {         /* the rim first, then the top */
        for (dy = (k > 0) ? 0 : -ry; dy <= ry; ++dy) {
            /* (the rim shows only the near half) */
            long t = (long)rx * rx * ((long)ry * ry - (long)dy * dy) /
                     ((long)ry * ry);
            int hw = 0;
            while ((long)(hw + 1) * (hw + 1) <= t) ++hw;
            pie_row(cx, cy + dy + k, dy, hw, rx, ry, deg,
                    (u8)(k ? C_TITLE : C_BLUE), (u8)(k ? C_DKYELLOW : C_YELLOW));
        }
    }
}

static void drive_paint(void)
{
    int x = g_box.x + 8, y = g_box.y + g_th + 8, lh = font_h() + 3;
    int vx = x + font_adv() * 11, sq = font_h() - 2;
    char b[40], c[16];
    ui_icon(ICON_HDD, x, y);
    sprintf(b, "%s (%c:)", d_label[0] ? d_label : "Drive", d_letter);
    font_draw(vx, y + (ICON_SIZE - font_h()) / 2, b, C_BLACK);
    y += ICON_SIZE + 4;
    rule(x, y, g_box.w - 16);
    y += 5;
    field(x, vx, y, "Type:", d_kind);              y += lh;
    field(x, vx, y, "File system:", "FAT");        y += lh;
    rule(x, y + 1, g_box.w - 16);
    y += 6;
    if (!d_ready) {
        font_draw(x, y, "The drive is not ready.", C_RED);
        return;
    }
    vid_fillrect(x, y + 1, sq, sq, C_BLUE);
    vid_rect(x, y + 1, sq, sq, C_BLACK);
    commas(c, (d_total - d_free) * 1024UL);
    sprintf(b, "%s bytes", c);
    field(x + sq + 4, vx, y, "Used:", b);          y += lh;
    vid_fillrect(x, y + 1, sq, sq, C_YELLOW);
    vid_rect(x, y + 1, sq, sq, C_BLACK);
    commas(c, d_free * 1024UL);
    sprintf(b, "%s bytes", c);
    field(x + sq + 4, vx, y, "Free:", b);          y += lh;
    rule(x, y + 1, g_box.w - 16);
    y += 6;
    commas(c, d_total * 1024UL);
    sprintf(b, "%s bytes", c);
    field(x, vx, y, "Capacity:", b);               y += lh + 4;
    {
        int rx = font_adv() * 6, ry = rx / 3, depth = ry / 2 + 2;
        int deg = (d_total > 0UL)
                  ? (int)((d_total - d_free) * 360UL / d_total) : 0;
        pie(g_box.x + g_box.w / 2, y + ry, rx, ry, depth, deg);
    }
}

void props_drive(char letter)
{
    struct find_t ff;
    char spec[8];
    union REGS r;
    int i, j, y;

    d_letter = upc(letter);
    strcpy(d_kind, drive_kind(d_letter));
    d_label[0] = '\0';
    sprintf(spec, "%c:\\*.*", d_letter);
    if (_dos_findfirst(spec, _A_VOLID, &ff) == 0) {
        /* The label comes back 8.3-shaped: "CASTALIA.DSK" means "CASTALIADSK". */
        for (i = j = 0; ff.name[i] && j < 12; ++i)
            if (ff.name[i] != '.')
                d_label[j++] = ff.name[i];
        d_label[j] = '\0';
    }
    r.h.ah = 0x36;
    r.h.dl = (unsigned char)(d_letter - 'A' + 1);
    int86(0x21, &r, &r);
    d_ready = (r.x.ax != 0xFFFF) ? TRUE : FALSE;
    if (d_ready) {
        unsigned long bpc = (unsigned long)r.x.ax * (unsigned long)r.x.cx;
        d_free  = bpc * (unsigned long)r.x.bx / 1024UL;
        d_total = bpc * (unsigned long)r.x.dx / 1024UL;
    }

    sheet_begin("Properties", font_adv() * 36,
                g_th + ICON_SIZE + (font_h() + 3) * 6 + font_adv() * 4 +
                font_h() + 44);
    g_paint = drive_paint;
    g_cancel_id = 1;
    y = g_box.y + g_box.h - font_h() - 11;
    (void)add_button(g_box.x + g_box.w - font_adv() * 9 - 8, y, "OK", 1);
    music_sfx(880, 2);
    (void)sheet_run();
}

/* ======================================================================
 * Date/Time Properties.
 * ==================================================================== */
#define DF_DATE 0
#define DF_HOUR 1
#define DF_MIN  2
#define DF_SEC  3

static int    t_day, t_mon, t_year, t_h, t_m, t_s, t_field;
static bool_t t_time_edited, t_date_edited;
static Rect   t_cell0, t_clock, t_up, t_dn, t_prev, t_next;
static int    t_cw, t_chh;

static const char * const MONTH[12] = {
    "January", "February", "March", "April", "May", "June", "July",
    "August", "September", "October", "November", "December"
};

static int days_in(int m, int y)
{
    static const int D[12] = { 31,28,31,30,31,30,31,31,30,31,30,31 };
    if (m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0))
        return 29;
    return D[m - 1];
}

/* 0 = Sunday, Zeller's congruence. */
static int weekday(int d, int m, int y)
{
    if (m < 3) { m += 12; --y; }
    return (d + (13 * (m + 1)) / 5 + y + y / 4 - y / 100 + y / 400 + 6) % 7;
}

static void clamp_day(void)
{
    int n = days_in(t_mon, t_year);
    if (t_day > n) t_day = n;
    if (t_day < 1) t_day = 1;
}

static void add_months(int k)
{
    t_mon += k;
    while (t_mon > 12) { t_mon -= 12; ++t_year; }
    while (t_mon < 1)  { t_mon += 12; --t_year; }
    if (t_year < 1980) { t_year = 1980; t_mon = 1; }
    if (t_year > 2099) { t_year = 2099; t_mon = 12; }
    clamp_day();
    t_date_edited = TRUE;
}

static void add_days(int k)
{
    t_day += k;
    while (t_day < 1) {
        add_months(-1);
        t_day += days_in(t_mon, t_year);
    }
    while (t_day > days_in(t_mon, t_year)) {
        t_day -= days_in(t_mon, t_year);
        add_months(1);
    }
    t_date_edited = TRUE;
}

static void dt_paint(void)
{
    int x = g_box.x + 8, y = g_box.y + g_th + 6, c, r, d, first, n;
    int gw = t_cw * 7 + 8;
    char b[24];
    static const char WD[] = "SMTWTFS";

    /* Date group: month/year header with the two flip arrows, then the
       grid.  Windows 95 had spin boxes here; arrows on the header read
       the same at this size. */
    ui_groupbox(x, y, gw, t_chh * 8 + font_h() + 10, "Date");
    y += font_h() + 4;
    rect_set(&t_prev, x + 4, y, font_h() + 2, font_h() + 2);
    rect_set(&t_next, x + gw - font_h() - 6, y, font_h() + 2, font_h() + 2);
    ui_button(&t_prev, "<", FALSE);
    ui_button(&t_next, ">", FALSE);
    sprintf(b, "%s %d", MONTH[t_mon - 1], t_year);
    ui_text_center(x, y + 1, gw, b, C_BLACK);
    y += t_chh + 2;
    for (c = 0; c < 7; ++c) {
        char s[2];
        s[0] = WD[c]; s[1] = '\0';
        ui_text_center(x + 4 + c * t_cw, y, t_cw, s, C_DKGRAY);
    }
    y += t_chh;
    rect_set(&t_cell0, x + 4, y, t_cw, t_chh);
    vid_fillrect(x + 4, y, t_cw * 7, t_chh * 6, C_WHITE);
    ui_sink(x + 3, y - 1, t_cw * 7 + 2, t_chh * 6 + 2);
    first = weekday(1, t_mon, t_year);
    n = days_in(t_mon, t_year);
    for (d = 1; d <= n; ++d) {
        int cell = first + d - 1;
        int cx = x + 4 + (cell % 7) * t_cw, cy = y + (cell / 7) * t_chh;
        r = (d == t_day) ? 1 : 0;
        if (r)
            vid_fillrect(cx, cy, t_cw, t_chh, C_TITLE);
        sprintf(b, "%d", d);
        ui_text_center(cx, cy + 1, t_cw, b, r ? C_WHITE : C_BLACK);
        if (r && t_field == DF_DATE)
            ui_focus_rect(cx, cy, t_cw, t_chh);
    }

    /* Time group: the digits, the field being set underlined, and the
       spin arrows beside them. */
    x += gw + 6;
    y = g_box.y + g_th + 6;
    {
        int tw = g_box.x + g_box.w - 8 - x;
        int dx;
        ui_groupbox(x, y, tw, t_chh * 4 + font_h(), "Time");
        rect_set(&t_clock, x + 6, y + font_h() + 8, font_adv() * 8 + 6,
                 font_h() + 6);
        vid_fillrect(t_clock.x, t_clock.y, t_clock.w, t_clock.h, C_WHITE);
        ui_sink(t_clock.x, t_clock.y, t_clock.w, t_clock.h);
        sprintf(b, "%02d:%02d:%02d", t_h, t_m, t_s);
        font_draw(t_clock.x + 3, t_clock.y + 3, b, C_BLACK);
        if (t_field != DF_DATE) {
            dx = t_clock.x + 3 + (t_field - 1) * 3 * font_adv();
            vid_fillrect(dx, t_clock.y + 3, font_adv() * 2, font_h(), C_TITLE);
            sprintf(b, "%02d", t_field == DF_HOUR ? t_h :
                               t_field == DF_MIN ? t_m : t_s);
            font_draw(dx, t_clock.y + 3, b, C_WHITE);
        }
        rect_set(&t_up, t_clock.x + t_clock.w + 2, t_clock.y,
                 font_adv() + 4, t_clock.h / 2);
        rect_set(&t_dn, t_up.x, t_clock.y + t_clock.h / 2, t_up.w,
                 t_clock.h - t_clock.h / 2);
        ui_button(&t_up, "", FALSE);
        ui_arrow(&t_up, TRUE);
        ui_button(&t_dn, "", FALSE);
        ui_arrow(&t_dn, FALSE);
        font_draw(x + 6, t_clock.y + t_clock.h + 6, "Tab: field",
                  C_DKGRAY);
        font_draw(x + 6, t_clock.y + t_clock.h + 6 + font_h() + 2,
                  "Arrows: set", C_DKGRAY);
    }
}

static void spin(int k)
{
    if (t_field == DF_HOUR) t_h = (t_h + 24 + k) % 24;
    if (t_field == DF_MIN)  t_m = (t_m + 60 + k) % 60;
    if (t_field == DF_SEC)  t_s = (t_s + 60 + k) % 60;
    t_time_edited = TRUE;
}

static int dt_key(int key)
{
    if (key == KEY_TAB || key == KEY_BACKTAB) {
        t_field = (key == KEY_TAB) ? (t_field + 1) % 4 : (t_field + 3) % 4;
        return PK_REDRAW;
    }
    if (key == KEY_PGUP) { add_months(-1); return PK_REDRAW; }
    if (key == KEY_PGDN) { add_months(1);  return PK_REDRAW; }
    if (t_field == DF_DATE) {
        if (key == KEY_LEFT)  { add_days(-1); return PK_REDRAW; }
        if (key == KEY_RIGHT) { add_days(1);  return PK_REDRAW; }
        if (key == KEY_UP)    { add_days(-7); return PK_REDRAW; }
        if (key == KEY_DOWN)  { add_days(7);  return PK_REDRAW; }
    } else {
        if (key == KEY_UP)    { spin(1);  return PK_REDRAW; }
        if (key == KEY_DOWN)  { spin(-1); return PK_REDRAW; }
        if (key == KEY_LEFT)  { t_field = (t_field > DF_HOUR) ? t_field - 1
                                                              : DF_SEC;
                                return PK_REDRAW; }
        if (key == KEY_RIGHT) { t_field = (t_field < DF_SEC) ? t_field + 1
                                                             : DF_HOUR;
                                return PK_REDRAW; }
    }
    return PK_PASS;
}

static int dt_click(int mx, int my)
{
    if (rect_contains(&t_prev, mx, my)) { add_months(-1); return PK_REDRAW; }
    if (rect_contains(&t_next, mx, my)) { add_months(1);  return PK_REDRAW; }
    if (rect_contains(&t_up, mx, my))   { if (t_field == DF_DATE) t_field = DF_HOUR;
                                          spin(1);  return PK_REDRAW; }
    if (rect_contains(&t_dn, mx, my))   { if (t_field == DF_DATE) t_field = DF_HOUR;
                                          spin(-1); return PK_REDRAW; }
    if (rect_contains(&t_clock, mx, my)) {
        int f = (mx - t_clock.x - 3) / (3 * font_adv());
        t_field = DF_HOUR + ((f < 0) ? 0 : (f > 2) ? 2 : f);
        return PK_REDRAW;
    }
    if (mx >= t_cell0.x && mx < t_cell0.x + t_cw * 7 &&
        my >= t_cell0.y && my < t_cell0.y + t_chh * 6) {
        int cell = (my - t_cell0.y) / t_chh * 7 + (mx - t_cell0.x) / t_cw;
        int d = cell - weekday(1, t_mon, t_year) + 1;
        if (d >= 1 && d <= days_in(t_mon, t_year)) {
            t_day = d;
            t_field = DF_DATE;
            t_date_edited = TRUE;
            return PK_REDRAW;
        }
    }
    return PK_PASS;
}

/* Until the user touches the time it keeps ticking, as Windows' did. */
static bool_t dt_idle(void)
{
    struct dostime_t tm;
    if (t_time_edited)
        return FALSE;
    _dos_gettime(&tm);
    if ((int)tm.second == t_s && (int)tm.minute == t_m)
        return FALSE;
    t_h = tm.hour; t_m = tm.minute; t_s = tm.second;
    return TRUE;
}

bool_t props_datetime(void)
{
    struct dosdate_t dd;
    struct dostime_t tm;
    int y;

    _dos_getdate(&dd);
    _dos_gettime(&tm);
    t_day = dd.day; t_mon = dd.month; t_year = (int)dd.year;
    t_h = tm.hour;  t_m = tm.minute;  t_s = tm.second;
    t_field = DF_DATE;
    t_time_edited = t_date_edited = FALSE;
    t_cw  = font_adv() * 3;
    t_chh = font_h() + 2;

    sheet_begin("Date/Time Properties", t_cw * 7 + font_adv() * 17 + 30,
                g_th + t_chh * 8 + font_h() * 2 + 40);
    g_paint   = dt_paint;
    g_keyfn   = dt_key;
    g_clickfn = dt_click;
    g_idlefn  = dt_idle;
    y = g_box.y + g_box.h - font_h() - 11;
    (void)add_button(g_box.x + g_box.w - font_adv() * 18 - 14, y, "OK", 1);
    (void)add_button(g_box.x + g_box.w - font_adv() * 9 - 8, y, "Cancel", 0);
    music_sfx(880, 2);

    if (sheet_run() != 1)
        return FALSE;
    /* MS-DOS 3.3 and later carry these through to the CMOS clock. */
    if (t_date_edited) {
        dd.day = (unsigned char)t_day;
        dd.month = (unsigned char)t_mon;
        dd.year = (unsigned short)t_year;
        if (_dos_setdate(&dd) != 0) {
            dialog_message("Date/Time", "DOS refused that date.", NULL);
            return FALSE;
        }
    }
    if (t_time_edited) {
        tm.hour = (unsigned char)t_h;
        tm.minute = (unsigned char)t_m;
        tm.second = (unsigned char)t_s;
        tm.hsecond = 0;
        if (_dos_settime(&tm) != 0) {
            dialog_message("Date/Time", "DOS refused that time.", NULL);
            return FALSE;
        }
    }
    return (t_date_edited || t_time_edited) ? TRUE : FALSE;
}

/* ======================================================================
 * Shut Down.
 * ==================================================================== */
static void sd_paint(void)
{
    int x = g_box.x + 8, y = g_box.y + g_th + 8;
    ui_icon(ICON_EXIT, x, y);
    font_draw(x + ICON_SIZE + 10, y + 2, "Are you sure you want to:",
              C_BLACK);
}

int shutdown_dialog(void)
{
    int x, y, cs = ui_check_size(), r, i;

    sheet_begin("Shut Down Castalia", font_adv() * 36,
                g_th + font_h() * 4 + (cs + 5) * 3 + 30);
    g_paint = sd_paint;
    x = g_box.x + 8 + ICON_SIZE + 10;
    y = g_box.y + g_th + 8 + font_h() + 8;
    (void)add_toggle(CT_RADIO, x, y, "Shut down the computer?", TRUE);
    (void)add_toggle(CT_RADIO, x, y + cs + 5, "Restart the computer?", FALSE);
    (void)add_toggle(CT_RADIO, x, y + (cs + 5) * 2, "Return to MS-DOS?",
                     FALSE);
    y = g_box.y + g_box.h - font_h() - 11;
    (void)add_button(g_box.x + g_box.w / 2 - font_adv() * 9 - 4, y, "Yes", 1);
    (void)add_button(g_box.x + g_box.w / 2 + 4, y, "No", 0);
    g_focus = 3;                       /* Yes                              */
    music_sfx(880, 2);

    r = sheet_run();
    if (r != 1)
        return SD_CANCEL;
    for (i = 0; i < 3; ++i)
        if (g_c[i].on)
            return (i == 0) ? SD_OFF : (i == 1) ? SD_RESTART : SD_DOS;
    return SD_OFF;
}
